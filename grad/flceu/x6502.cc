/* FCE Ultra - NES/Famicom Emulator
 *
 * Copyright notice for this file:
 *  Copyright (C) 2002 Xodnizel
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include <string.h>
#include "types.h"
#include "x6502.h"
#include "fceu.h"
#include "sound.h"

#include "tracing.h"
#include "threadutil.h"

#define RUN_ALL_INSTRUCTIONS 1
#define FAST_INSTRUCTION_DISPATCH 0

static constexpr int NUM_THREADS = 8;

X6502::X6502(FC *fc) : fc(fc) {
  CHECK(fc != nullptr);
}

uint8 X6502::DMR(uint32 A) {
  ADDCYC(1);
  return (DB = fc->fceu->ARead[A](fc, A));
}

void X6502::DMW(uint32 A, uint8 V) {
  ADDCYC(1);
  fc->fceu->BWrite[A](fc, A, V);
}

static constexpr uint8 CycTable[256] = {
    /*0x00*/ 7, 6, 2, 8, 3, 3, 5, 5, 3, 2, 2, 2, 4, 4, 6, 6,
    /*0x10*/ 2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
    /*0x20*/ 6, 6, 2, 8, 3, 3, 5, 5, 4, 2, 2, 2, 4, 4, 6, 6,
    /*0x30*/ 2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
    /*0x40*/ 6, 6, 2, 8, 3, 3, 5, 5, 3, 2, 2, 2, 3, 4, 6, 6,
    /*0x50*/ 2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
    /*0x60*/ 6, 6, 2, 8, 3, 3, 5, 5, 4, 2, 2, 2, 5, 4, 6, 6,
    /*0x70*/ 2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
    /*0x80*/ 2, 6, 2, 6, 3, 3, 3, 3, 2, 2, 2, 2, 4, 4, 4, 4,
    /*0x90*/ 2, 6, 2, 6, 4, 4, 4, 4, 2, 5, 2, 5, 5, 5, 5, 5,
    /*0xA0*/ 2, 6, 2, 6, 3, 3, 3, 3, 2, 2, 2, 2, 4, 4, 4, 4,
    /*0xB0*/ 2, 5, 2, 5, 4, 4, 4, 4, 2, 4, 2, 4, 4, 4, 4, 4,
    /*0xC0*/ 2, 6, 2, 8, 3, 3, 5, 5, 2, 2, 2, 2, 4, 4, 6, 6,
    /*0xD0*/ 2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
    /*0xE0*/ 2, 6, 3, 8, 3, 3, 5, 5, 2, 2, 2, 2, 4, 4, 6, 6,
    /*0xF0*/ 2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
};

void X6502::IRQBegin(int w) {
  IRQlow |= w;
}

void X6502::IRQEnd(int w) {
  IRQlow &= ~w;
}

void X6502::TriggerNMI() {
  IRQlow |= FCEU_IQNMI;
}

void X6502::TriggerNMI2() {
  IRQlow |= FCEU_IQNMI2;
}

void X6502::Reset() {
  IRQlow = FCEU_IQRESET;
}


// Initializes the 6502 CPU.
void X6502::Init() {
  // Initialize the CPU fields.
  // (Don't memset; we have non-CPU members now!)
  tcount = 0;
  cpu.reg_PC = Fluint16(0x0000);
  cpu.reg_A = cpu.reg_X = cpu.reg_Y = cpu.reg_S = cpu.reg_P = cpu.reg_PI =
    Fluint8(0x00);
  cpu.cycles = Fluint16(0x0000);
  cpu.jammed = Fluint8(0x00);
  cpu.active = Fluint8(0x01);
  cpu.parent = this;
  count = 0;
  IRQlow = 0;
  DB = 0;
  timestamp = 0;
  MapIRQHook = nullptr;
}

void X6502::Power() {
  count = tcount = IRQlow = 0;
  cpu.reg_PC = Fluint16(0x0000);
  cpu.reg_A = cpu.reg_X = cpu.reg_Y = cpu.reg_P = cpu.reg_PI = Fluint8(0x00);
  cpu.cycles = Fluint16(0x0000);
  cpu.reg_S = Fluint8(0xFD);
  DB = 0;
  cpu.jammed = Fluint8(0x00);
  cpu.active = Fluint8(0x01);
  cpu.parent = this;

  timestamp = 0;
  ClearMemTrace();
  ClearInstHisto();
  Reset();
}

void X6502::Run(int32 cycles) {
  if (fc->fceu->PAL) {
    cycles *= 15;  // 15*4=60
  } else {
    cycles *= 16;  // 16*4=64
  }

  count += cycles;

  RunLoop();
}

void X6502::RunLoop() {
  while (count > 0) {

    if (IRQlow) {
      if (IRQlow & FCEU_IQRESET) {
        Fluint8 lo = RdMem(Fluint16(0xFFFC));
        Fluint8 hi = RdMem(Fluint16(0xFFFD));
        cpu.reg_PC = Fluint16(hi, lo);
        cpu.jammed = Fluint8(0x00);
        cpu.reg_PI = cpu.reg_P = I_FLAG;
        IRQlow &= ~FCEU_IQRESET;
      } else if (IRQlow & FCEU_IQNMI2) {
        IRQlow &= ~FCEU_IQNMI2;
        IRQlow |= FCEU_IQNMI;
      } else if (IRQlow & FCEU_IQNMI) {
        if (!cpu.jammed.ToInt()) {
          ADDCYC(7);
          cpu.PUSH16(cpu.reg_PC);
          const Fluint8 pnb = Fluint8::AndWith<(uint8_t)~B_FLAG8>(cpu.reg_P);
          cpu.PUSH(Fluint8::OrWith<U_FLAG8>(pnb));
          cpu.reg_P = Fluint8::OrWith<I_FLAG8>(cpu.reg_P);
          Fluint8 lo = RdMem(Fluint16(0xFFFA));
          Fluint8 hi = RdMem(Fluint16(0xFFFB));
          cpu.reg_PC = Fluint16(hi, lo);
          IRQlow &= ~FCEU_IQNMI;
        }
      } else {
        const Fluint8 fpi = Fluint8::AndWith<I_FLAG8>(cpu.reg_PI);
        if (fpi.ToInt() == 0 && !cpu.jammed.ToInt()) {
          ADDCYC(7);
          cpu.PUSH16(cpu.reg_PC);
          const Fluint8 pnb = Fluint8::AndWith<(uint8_t)~B_FLAG8>(cpu.reg_P);
          cpu.PUSH(Fluint8::OrWith<U_FLAG8>(pnb));
          cpu.reg_P = Fluint8::OrWith<I_FLAG8>(cpu.reg_P);
          Fluint8 lo = RdMem(Fluint16(0xFFFE));
          Fluint8 hi = RdMem(Fluint16(0xFFFF));
          cpu.reg_PC = Fluint16(hi, lo);
        }
      }
      IRQlow &= ~(FCEU_IQTEMP);
      if (count <= 0) {
        cpu.reg_PI = cpu.reg_P;
        return;
        // Should increase accuracy without a
        // major speed hit.
      }
    }

    cpu.reg_PI = cpu.reg_P;
    // Get the next instruction.

    const Fluint8 opcode = RdMem(cpu.reg_PC);
    // printf("Read %x -> opcode %02x\n", reg_PC, b1);

    ADDCYC(CycTable[opcode.ToInt()]);

    int32 temp = tcount;
    tcount = 0;
    if (MapIRQHook) MapIRQHook(fc, temp);
    fc->sound->SoundCPUHook(temp);
    cpu.reg_PC++;


    // One cpu state per instruction (we execute them all
    // in parallel).
    std::array<CPU, 256> cpus;
    for (int i = 0; i < 256; i++) {
      cpus[i] = cpu;
      cpus[i].active = Fluint8::Eq(Fluint8(i), opcode);
      cpus[i].parent = this;
    }

    inst_histo[opcode.ToInt()]++;

    auto RunInstruction = [&](uint8 op) {
    switch (op) {
    case 0x00: cpus[0x00].BRK(); break;
    case 0x40: cpus[0x40].RTI(); break;
    case 0x60: cpus[0x60].RTS(); break;
    case 0x48: cpus[0x48].PHA(); break;
    case 0x08: cpus[0x08].PHP(); break;
    case 0x68: cpus[0x68].PLA(); break;
    case 0x28: cpus[0x28].PLP(); break;
    case 0x4C: cpus[0x4C].JMPABS(); break;
    case 0x6C: cpus[0x6C].JMPIND(); break;
    case 0x20: cpus[0x20].JSR(); break;

    case 0xAA: cpus[0xAA].TAX(); break;
    case 0x8A: cpus[0x8A].TXA(); break;

    case 0xA8: cpus[0xA8].TAY(); break;
    case 0x98: cpus[0x98].TYA(); break;

    case 0xBA: cpus[0xBA].TSX(); break;
    case 0x9A: cpus[0x9A].TXS(); break;

    case 0xCA: cpus[0xCA].DEX(); break;
    case 0x88: cpus[0x88].DEY(); break;
    case 0xE8: cpus[0xE8].INX(); break;
    case 0xC8: cpus[0xC8].INY(); break;

    case 0xEA: /* NOP */ break;

    case 0x18: cpus[0x18].CLC(); break;
    case 0xD8: cpus[0xD8].CLD(); break;
    case 0x58: cpus[0x58].CLI(); break;
    case 0xB8: cpus[0xB8].CLV(); break;

    case 0x38: cpus[0x38].SEC(); break;
    case 0xF8: cpus[0xF8].SED(); break;
    case 0x78: cpus[0x78].SEI(); break;

    case 0x0A: cpus[0x0A].RMW_A([](CPU *cpu, Fluint8 x) { return cpu->ASL(x); }); break;
    case 0x06: cpus[0x06].RMW_ZP([](CPU *cpu, Fluint8 x) { return cpu->ASL(x); }); break;
    case 0x16: cpus[0x16].RMW_ZPX([](CPU *cpu, Fluint8 x) { return cpu->ASL(x); }); break;
    case 0x0E: cpus[0x0E].RMW_AB([](CPU *cpu, Fluint8 x) { return cpu->ASL(x); }); break;
    case 0x1E: cpus[0x1E].RMW_ABX([](CPU *cpu, Fluint8 x) { return cpu->ASL(x); }); break;

    case 0xC6: cpus[0xC6].RMW_ZP([](CPU *cpu, Fluint8 x) { return cpu->DEC(x); }); break;
    case 0xD6: cpus[0xD6].RMW_ZPX([](CPU *cpu, Fluint8 x) { return cpu->DEC(x); }); break;
    case 0xCE: cpus[0xCE].RMW_AB([](CPU *cpu, Fluint8 x) { return cpu->DEC(x); }); break;
    case 0xDE: cpus[0xDE].RMW_ABX([](CPU *cpu, Fluint8 x) { return cpu->DEC(x); }); break;

    case 0xE6: cpus[0xE6].RMW_ZP([](CPU *cpu, Fluint8 x) { return cpu->INC(x); }); break;
    case 0xF6: cpus[0xF6].RMW_ZPX([](CPU *cpu, Fluint8 x) { return cpu->INC(x); }); break;
    case 0xEE: cpus[0xEE].RMW_AB([](CPU *cpu, Fluint8 x) { return cpu->INC(x); }); break;
    case 0xFE: cpus[0xFE].RMW_ABX([](CPU *cpu, Fluint8 x) { return cpu->INC(x); }); break;

    case 0x4A: cpus[0x4A].RMW_A([](CPU *cpu, Fluint8 x) { return cpu->LSR(x); }); break;
    case 0x46: cpus[0x46].RMW_ZP([](CPU *cpu, Fluint8 x) { return cpu->LSR(x); }); break;
    case 0x56: cpus[0x56].RMW_ZPX([](CPU *cpu, Fluint8 x) { return cpu->LSR(x); }); break;
    case 0x4E: cpus[0x4E].RMW_AB([](CPU *cpu, Fluint8 x) { return cpu->LSR(x); }); break;
    case 0x5E: cpus[0x5E].RMW_ABX([](CPU *cpu, Fluint8 x) { return cpu->LSR(x); }); break;

    case 0x2A: cpus[0x2A].RMW_A([](CPU *cpu, Fluint8 x) { return cpu->ROL(x); }); break;
    case 0x26: cpus[0x26].RMW_ZP([](CPU *cpu, Fluint8 x) { return cpu->ROL(x); }); break;
    case 0x36: cpus[0x36].RMW_ZPX([](CPU *cpu, Fluint8 x) { return cpu->ROL(x); }); break;
    case 0x2E: cpus[0x2E].RMW_AB([](CPU *cpu, Fluint8 x) { return cpu->ROL(x); }); break;
    case 0x3E: cpus[0x3E].RMW_ABX([](CPU *cpu, Fluint8 x) { return cpu->ROL(x); }); break;

    case 0x6A: cpus[0x6A].RMW_A([](CPU *cpu, Fluint8 x) { return cpu->ROR(x); }); break;
    case 0x66: cpus[0x66].RMW_ZP([](CPU *cpu, Fluint8 x) { return cpu->ROR(x); }); break;
    case 0x76: cpus[0x76].RMW_ZPX([](CPU *cpu, Fluint8 x) { return cpu->ROR(x); }); break;
    case 0x6E: cpus[0x6E].RMW_AB([](CPU *cpu, Fluint8 x) { return cpu->ROR(x); }); break;
    case 0x7E: cpus[0x7E].RMW_ABX([](CPU *cpu, Fluint8 x) { return cpu->ROR(x); }); break;

    case 0x69: cpus[0x69].LD_IM([](CPU *cpu, Fluint8 x) { cpu->ADC(x); }); break;
    case 0x65: cpus[0x65].LD_ZP([](CPU *cpu, Fluint8 x) { cpu->ADC(x); }); break;
    case 0x75: cpus[0x75].LD_ZPX([](CPU *cpu, Fluint8 x) { cpu->ADC(x); }); break;
    case 0x6D: cpus[0x6D].LD_AB([](CPU *cpu, Fluint8 x) { cpu->ADC(x); }); break;
    case 0x7D: cpus[0x7D].LD_ABX([](CPU *cpu, Fluint8 x) { cpu->ADC(x); }); break;
    case 0x79: cpus[0x79].LD_ABY([](CPU *cpu, Fluint8 x) { cpu->ADC(x); }); break;
    case 0x61: cpus[0x61].LD_IX([](CPU *cpu, Fluint8 x) { cpu->ADC(x); }); break;
    case 0x71: cpus[0x71].LD_IY([](CPU *cpu, Fluint8 x) { cpu->ADC(x); }); break;

    case 0x29: cpus[0x29].LD_IM([](CPU *cpu, Fluint8 x) { cpu->AND(x); }); break;
    case 0x25: cpus[0x25].LD_ZP([](CPU *cpu, Fluint8 x) { cpu->AND(x); }); break;
    case 0x35: cpus[0x35].LD_ZPX([](CPU *cpu, Fluint8 x) { cpu->AND(x); }); break;
    case 0x2D: cpus[0x2D].LD_AB([](CPU *cpu, Fluint8 x) { cpu->AND(x); }); break;
    case 0x3D: cpus[0x3D].LD_ABX([](CPU *cpu, Fluint8 x) { cpu->AND(x); }); break;
    case 0x39: cpus[0x39].LD_ABY([](CPU *cpu, Fluint8 x) { cpu->AND(x); }); break;
    case 0x21: cpus[0x21].LD_IX([](CPU *cpu, Fluint8 x) { cpu->AND(x); }); break;
    case 0x31: cpus[0x31].LD_IY([](CPU *cpu, Fluint8 x) { cpu->AND(x); }); break;

    case 0x24: cpus[0x24].LD_ZP([](CPU *cpu, Fluint8 x) { cpu->BIT(x); }); break;
    case 0x2C: cpus[0x2C].LD_AB([](CPU *cpu, Fluint8 x) { cpu->BIT(x); }); break;


    case 0xC9: cpus[0xC9].LD_IM([](CPU *cpu, Fluint8 x) { cpu->CMPL(cpu->reg_A, x); }); break;
    case 0xC5: cpus[0xC5].LD_ZP([](CPU *cpu, Fluint8 x) { cpu->CMPL(cpu->reg_A, x); }); break;
    case 0xD5: cpus[0xD5].LD_ZPX([](CPU *cpu, Fluint8 x) { cpu->CMPL(cpu->reg_A, x); }); break;
    case 0xCD: cpus[0xCD].LD_AB([](CPU *cpu, Fluint8 x) { cpu->CMPL(cpu->reg_A, x); }); break;
    case 0xDD: cpus[0xDD].LD_ABX([](CPU *cpu, Fluint8 x) { cpu->CMPL(cpu->reg_A, x); }); break;
    case 0xD9: cpus[0xD9].LD_ABY([](CPU *cpu, Fluint8 x) { cpu->CMPL(cpu->reg_A, x); }); break;
    case 0xC1: cpus[0xC1].LD_IX([](CPU *cpu, Fluint8 x) { cpu->CMPL(cpu->reg_A, x); }); break;
    case 0xD1: cpus[0xD1].LD_IY([](CPU *cpu, Fluint8 x) { cpu->CMPL(cpu->reg_A, x); }); break;

    case 0xE0: cpus[0xE0].LD_IM([](CPU *cpu, Fluint8 x) { cpu->CMPL(cpu->reg_X, x); }); break;
    case 0xE4: cpus[0xE4].LD_ZP([](CPU *cpu, Fluint8 x) { cpu->CMPL(cpu->reg_X, x); }); break;
    case 0xEC: cpus[0xEC].LD_AB([](CPU *cpu, Fluint8 x) { cpu->CMPL(cpu->reg_X, x); }); break;

    case 0xC0: cpus[0xC0].LD_IM([](CPU *cpu, Fluint8 x) { cpu->CMPL(cpu->reg_Y, x); }); break;
    case 0xC4: cpus[0xC4].LD_ZP([](CPU *cpu, Fluint8 x) { cpu->CMPL(cpu->reg_Y, x); }); break;
    case 0xCC: cpus[0xCC].LD_AB([](CPU *cpu, Fluint8 x) { cpu->CMPL(cpu->reg_Y, x); }); break;

    case 0x49: cpus[0x49].LD_IM([](CPU *cpu, Fluint8 x) { cpu->EOR(x); }); break;
    case 0x45: cpus[0x45].LD_ZP([](CPU *cpu, Fluint8 x) { cpu->EOR(x); }); break;
    case 0x55: cpus[0x55].LD_ZPX([](CPU *cpu, Fluint8 x) { cpu->EOR(x); }); break;
    case 0x4D: cpus[0x4D].LD_AB([](CPU *cpu, Fluint8 x) { cpu->EOR(x); }); break;
    case 0x5D: cpus[0x5D].LD_ABX([](CPU *cpu, Fluint8 x) { cpu->EOR(x); }); break;
    case 0x59: cpus[0x59].LD_ABY([](CPU *cpu, Fluint8 x) { cpu->EOR(x); }); break;
    case 0x41: cpus[0x41].LD_IX([](CPU *cpu, Fluint8 x) { cpu->EOR(x); }); break;
    case 0x51: cpus[0x51].LD_IY([](CPU *cpu, Fluint8 x) { cpu->EOR(x); }); break;

    case 0xA9: cpus[0xA9].LD_IM([](CPU *cpu, Fluint8 x) { cpu->LDA(x); }); break;
    case 0xA5: cpus[0xA5].LD_ZP([](CPU *cpu, Fluint8 x) { cpu->LDA(x); }); break;
    case 0xB5: cpus[0xB5].LD_ZPX([](CPU *cpu, Fluint8 x) { cpu->LDA(x); }); break;
    case 0xAD: cpus[0xAD].LD_AB([](CPU *cpu, Fluint8 x) { cpu->LDA(x); }); break;
    case 0xBD: cpus[0xBD].LD_ABX([](CPU *cpu, Fluint8 x) { cpu->LDA(x); }); break;
    case 0xB9: cpus[0xB9].LD_ABY([](CPU *cpu, Fluint8 x) { cpu->LDA(x); }); break;
    case 0xA1: cpus[0xA1].LD_IX([](CPU *cpu, Fluint8 x) { cpu->LDA(x); }); break;
    case 0xB1: cpus[0xB1].LD_IY([](CPU *cpu, Fluint8 x) { cpu->LDA(x); }); break;

    case 0xA2: cpus[0xA2].LD_IM([](CPU *cpu, Fluint8 x) { cpu->LDX(x); }); break;
    case 0xA6: cpus[0xA6].LD_ZP([](CPU *cpu, Fluint8 x) { cpu->LDX(x); }); break;
    case 0xB6: cpus[0xB6].LD_ZPY([](CPU *cpu, Fluint8 x) { cpu->LDX(x); }); break;
    case 0xAE: cpus[0xAE].LD_AB([](CPU *cpu, Fluint8 x) { cpu->LDX(x); }); break;
    case 0xBE: cpus[0xBE].LD_ABY([](CPU *cpu, Fluint8 x) { cpu->LDX(x); }); break;

    case 0xA0: cpus[0xA0].LD_IM([](CPU *cpu, Fluint8 x) { cpu->LDY(x); }); break;
    case 0xA4: cpus[0xA4].LD_ZP([](CPU *cpu, Fluint8 x) { cpu->LDY(x); }); break;
    case 0xB4: cpus[0xB4].LD_ZPX([](CPU *cpu, Fluint8 x) { cpu->LDY(x); }); break;
    case 0xAC: cpus[0xAC].LD_AB([](CPU *cpu, Fluint8 x) { cpu->LDY(x); }); break;
    case 0xBC: cpus[0xBC].LD_ABX([](CPU *cpu, Fluint8 x) { cpu->LDY(x); }); break;

    case 0x09: cpus[0x09].LD_IM([](CPU *cpu, Fluint8 x) { cpu->ORA(x); }); break;
    case 0x05: cpus[0x05].LD_ZP([](CPU *cpu, Fluint8 x) { cpu->ORA(x); }); break;
    case 0x15: cpus[0x15].LD_ZPX([](CPU *cpu, Fluint8 x) { cpu->ORA(x); }); break;
    case 0x0D: cpus[0x0D].LD_AB([](CPU *cpu, Fluint8 x) { cpu->ORA(x); }); break;
    case 0x1D: cpus[0x1D].LD_ABX([](CPU *cpu, Fluint8 x) { cpu->ORA(x); }); break;
    case 0x19: cpus[0x19].LD_ABY([](CPU *cpu, Fluint8 x) { cpu->ORA(x); }); break;
    case 0x01: cpus[0x01].LD_IX([](CPU *cpu, Fluint8 x) { cpu->ORA(x); }); break;
    case 0x11: cpus[0x11].LD_IY([](CPU *cpu, Fluint8 x) { cpu->ORA(x); }); break;

    // Undocumented; same as E9
    case 0xEB: cpus[0xEB].LD_IM([](CPU *cpu, Fluint8 x) { cpu->SBC(x); }); break;
    case 0xE9: cpus[0xE9].LD_IM([](CPU *cpu, Fluint8 x) { cpu->SBC(x); }); break;
    case 0xE5: cpus[0xE5].LD_ZP([](CPU *cpu, Fluint8 x) { cpu->SBC(x); }); break;
    case 0xF5: cpus[0xF5].LD_ZPX([](CPU *cpu, Fluint8 x) { cpu->SBC(x); }); break;
    case 0xED: cpus[0xED].LD_AB([](CPU *cpu, Fluint8 x) { cpu->SBC(x); }); break;
    case 0xFD: cpus[0xFD].LD_ABX([](CPU *cpu, Fluint8 x) { cpu->SBC(x); }); break;
    case 0xF9: cpus[0xF9].LD_ABY([](CPU *cpu, Fluint8 x) { cpu->SBC(x); }); break;
    case 0xE1: cpus[0xE1].LD_IX([](CPU *cpu, Fluint8 x) { cpu->SBC(x); }); break;
    case 0xF1: cpus[0xF1].LD_IY([](CPU *cpu, Fluint8 x) { cpu->SBC(x); }); break;


    case 0x85: cpus[0x85].ST_ZP([](CPU *cpu, Fluint16 AA) { return cpu->reg_A; }); break;
    case 0x95: cpus[0x95].ST_ZPX([](CPU *cpu, Fluint16 AA) { return cpu->reg_A; }); break;
    case 0x8D: cpus[0x8D].ST_AB([](CPU *cpu, Fluint16 AA) { return cpu->reg_A; }); break;
    case 0x9D: cpus[0x9D].ST_ABX([](CPU *cpu, Fluint16 AA) { return cpu->reg_A; }); break;
    case 0x99: cpus[0x99].ST_ABY([](CPU *cpu, Fluint16 AA) { return cpu->reg_A; }); break;
    case 0x81: cpus[0x81].ST_IX([](CPU *cpu, Fluint16 AA) { return cpu->reg_A; }); break;
    case 0x91: cpus[0x91].ST_IY([](CPU *cpu, Fluint16 AA) { return cpu->reg_A; }); break;

    case 0x86: cpus[0x86].ST_ZP([](CPU *cpu, Fluint16 AA) { return cpu->reg_X; }); break;
    case 0x96: cpus[0x96].ST_ZPY([](CPU *cpu, Fluint16 AA) { return cpu->reg_X; }); break;
    case 0x8E: cpus[0x8E].ST_AB([](CPU *cpu, Fluint16 AA) { return cpu->reg_X; }); break;

    case 0x84: cpus[0x84].ST_ZP([](CPU *cpu, Fluint16 AA) { return cpu->reg_Y; }); break;
    case 0x94: cpus[0x94].ST_ZPX([](CPU *cpu, Fluint16 AA) { return cpu->reg_Y; }); break;
    case 0x8C: cpus[0x8C].ST_AB([](CPU *cpu, Fluint16 AA) { return cpu->reg_Y; }); break;

    case 0x90: cpus[0x90].BCC(); break;
    case 0xB0: cpus[0xB0].BCS(); break;
    case 0xF0: cpus[0xF0].BEQ(); break;
    case 0xD0: cpus[0xD0].BNE(); break;
    case 0x30: cpus[0x30].BMI(); break;
    case 0x10: cpus[0x10].BPL(); break;
    case 0x50: cpus[0x50].BVC(); break;
    case 0x70: cpus[0x70].BVS(); break;

    // ** Undocumented instructions **
    case 0x2B: cpus[0x2B].LD_IM([](CPU *cpu, Fluint8 x) { cpu->AAC(x); }); break;
    case 0x0B: cpus[0x0B].LD_IM([](CPU *cpu, Fluint8 x) { cpu->AAC(x); }); break;


    /* AAX */
    case 0x87: cpus[0x87].ST_ZP([](CPU *cpu, Fluint16 AA) { return cpu->reg_A & cpu->reg_X; }); break;
    case 0x97: cpus[0x97].ST_ZPY([](CPU *cpu, Fluint16 AA) { return cpu->reg_A & cpu->reg_X; }); break;
    case 0x8F: cpus[0x8F].ST_AB([](CPU *cpu, Fluint16 AA) { return cpu->reg_A & cpu->reg_X; }); break;
    case 0x83: cpus[0x83].ST_IX([](CPU *cpu, Fluint16 AA) { return cpu->reg_A & cpu->reg_X; }); break;

    case 0x6B:
      cpus[0x6B].LD_IM([](CPU *cpu, Fluint8 x) { cpu->ARR(x); });
      break;

    /* ASR */
    case 0x4B:
      cpus[0x4B].LD_IM([](CPU *cpu, Fluint8 x) { cpu->AND(x); cpu->LSRA(); });
      break;

    case 0xAB:
      cpus[0xAB].LD_IM([](CPU *cpu, Fluint8 x) { cpu->ATX(x); });
      break;

    case 0xCB:
      cpus[0xCB].LD_IM([](CPU *cpu, Fluint8 x) { cpu->AXS(x); });
      break;

    /* DCP */
    case 0xC7:
      cpus[0xC7].RMW_ZP([](CPU *cpu, Fluint8 x) { return cpu->DCP(x); });
      break;
    case 0xD7:
      cpus[0xD7].RMW_ZPX([](CPU *cpu, Fluint8 x) { return cpu->DCP(x); });
      break;
    case 0xCF:
      cpus[0xCF].RMW_AB([](CPU *cpu, Fluint8 x) { return cpu->DCP(x); });
      break;
    case 0xDF:
      cpus[0xDF].RMW_ABX([](CPU *cpu, Fluint8 x) { return cpu->DCP(x); });
      break;
    case 0xDB:
      cpus[0xDB].RMW_ABY([](CPU *cpu, Fluint8 x) { return cpu->DCP(x); });
      break;
    case 0xC3:
      cpus[0xC3].RMW_IX([](CPU *cpu, Fluint8 x) { return cpu->DCP(x); });
      break;
    case 0xD3:
      cpus[0xD3].RMW_IY([](CPU *cpu, Fluint8 x) { return cpu->DCP(x); });
      break;

    /* ISB */
    case 0xE7:
      cpus[0xE7].RMW_ZP([](CPU *cpu, Fluint8 x) { return cpu->ISB(x); });
      break;
    case 0xF7:
      cpus[0xF7].RMW_ZPX([](CPU *cpu, Fluint8 x) { return cpu->ISB(x); });
      break;
    case 0xEF:
      cpus[0xEF].RMW_AB([](CPU *cpu, Fluint8 x) { return cpu->ISB(x); });
      break;
    case 0xFF:
      cpus[0xFF].RMW_ABX([](CPU *cpu, Fluint8 x) { return cpu->ISB(x); });
      break;
    case 0xFB:
      cpus[0xFB].RMW_ABY([](CPU *cpu, Fluint8 x) { return cpu->ISB(x); });
      break;
    case 0xE3:
      cpus[0xE3].RMW_IX([](CPU *cpu, Fluint8 x) { return cpu->ISB(x); });
      break;
    case 0xF3:
      cpus[0xF3].RMW_IY([](CPU *cpu, Fluint8 x) { return cpu->ISB(x); });
      break;

    /* DOP */
    case 0x04: cpus[0x04].reg_PC++; break;
    case 0x14: cpus[0x14].reg_PC++; break;
    case 0x34: cpus[0x34].reg_PC++; break;
    case 0x44: cpus[0x44].reg_PC++; break;
    case 0x54: cpus[0x54].reg_PC++; break;
    case 0x64: cpus[0x64].reg_PC++; break;
    case 0x74: cpus[0x74].reg_PC++; break;

    case 0x80: cpus[0x80].reg_PC++; break;
    case 0x82: cpus[0x82].reg_PC++; break;
    case 0x89: cpus[0x89].reg_PC++; break;
    case 0xC2: cpus[0xC2].reg_PC++; break;
    case 0xD4: cpus[0xD4].reg_PC++; break;
    case 0xE2: cpus[0xE2].reg_PC++; break;
    case 0xF4: cpus[0xF4].reg_PC++; break;

    case 0x02: cpus[0x02].KIL(); break;
    case 0x12: cpus[0x12].KIL(); break;
    case 0x22: cpus[0x22].KIL(); break;
    case 0x32: cpus[0x32].KIL(); break;
    case 0x42: cpus[0x42].KIL(); break;
    case 0x52: cpus[0x52].KIL(); break;
    case 0x62: cpus[0x62].KIL(); break;
    case 0x72: cpus[0x72].KIL(); break;
    case 0x92: cpus[0x92].KIL(); break;
    case 0xB2: cpus[0xB2].KIL(); break;
    case 0xD2: cpus[0xD2].KIL(); break;
    case 0xF2: cpus[0xF2].KIL(); break;

    case 0xBB:
      cpus[0xBB].RMW_ABY([](CPU *cpu, Fluint8 x) { return cpu->LAR(x); });
      break;

      /* LAX */
    case 0xA7: cpus[0xA7].LD_ZP([](CPU *cpu, Fluint8 x) { cpu->LAX(x); }); break;
    case 0xB7: cpus[0xB7].LD_ZPY([](CPU *cpu, Fluint8 x) { cpu->LAX(x); }); break;
    case 0xAF: cpus[0xAF].LD_AB([](CPU *cpu, Fluint8 x) { cpu->LAX(x); }); break;
    case 0xBF: cpus[0xBF].LD_ABY([](CPU *cpu, Fluint8 x) { cpu->LAX(x); }); break;
    case 0xA3: cpus[0xA3].LD_IX([](CPU *cpu, Fluint8 x) { cpu->LAX(x); }); break;
    case 0xB3: cpus[0xB3].LD_IY([](CPU *cpu, Fluint8 x) { cpu->LAX(x); }); break;

    /* NOP */
    case 0x1A: break;
    case 0x3A: break;
    case 0x5A: break;
    case 0x7A: break;
    case 0xDA: break;
    case 0xFA: break;

    /* RLA */
    case 0x27:
      cpus[0x27].RMW_ZP([](CPU *cpu, Fluint8 x) { return cpu->RLA(x); });
      break;
    case 0x37:
      cpus[0x37].RMW_ZPX([](CPU *cpu, Fluint8 x) { return cpu->RLA(x); });
      break;
    case 0x2F:
      cpus[0x2F].RMW_AB([](CPU *cpu, Fluint8 x) { return cpu->RLA(x); });
      break;
    case 0x3F:
      cpus[0x3F].RMW_ABX([](CPU *cpu, Fluint8 x) { return cpu->RLA(x); });
      break;
    case 0x3B:
      cpus[0x3B].RMW_ABY([](CPU *cpu, Fluint8 x) { return cpu->RLA(x); });
      break;
    case 0x23:
      cpus[0x23].RMW_IX([](CPU *cpu, Fluint8 x) { return cpu->RLA(x); });
      break;
    case 0x33:
      cpus[0x33].RMW_IY([](CPU *cpu, Fluint8 x) { return cpu->RLA(x); });
      break;

    /* RRA */
    case 0x67:
      cpus[0x67].RMW_ZP([](CPU *cpu, Fluint8 x) { return cpu->RRA(x); });
      break;
    case 0x77:
      cpus[0x77].RMW_ZPX([](CPU *cpu, Fluint8 x) { return cpu->RRA(x); });
      break;
    case 0x6F:
      cpus[0x6F].RMW_AB([](CPU *cpu, Fluint8 x) { return cpu->RRA(x); });
      break;
    case 0x7F:
      cpus[0x7F].RMW_ABX([](CPU *cpu, Fluint8 x) { return cpu->RRA(x); });
      break;
    case 0x7B:
      cpus[0x7B].RMW_ABY([](CPU *cpu, Fluint8 x) { return cpu->RRA(x); });
      break;
    case 0x63:
      cpus[0x63].RMW_IX([](CPU *cpu, Fluint8 x) { return cpu->RRA(x); });
      break;
    case 0x73:
      cpus[0x73].RMW_IY([](CPU *cpu, Fluint8 x) { return cpu->RRA(x); });
      break;

    /* SLO */
    case 0x07:
      cpus[0x07].RMW_ZP([](CPU *cpu, Fluint8 x) { return cpu->SLO(x); });
      break;
    case 0x17:
      cpus[0x17].RMW_ZPX([](CPU *cpu, Fluint8 x) { return cpu->SLO(x); });
      break;
    case 0x0F:
      cpus[0x0F].RMW_AB([](CPU *cpu, Fluint8 x) { return cpu->SLO(x); });
      break;
    case 0x1F:
      cpus[0x1F].RMW_ABX([](CPU *cpu, Fluint8 x) { return cpu->SLO(x); });
      break;
    case 0x1B:
      cpus[0x1B].RMW_ABY([](CPU *cpu, Fluint8 x) { return cpu->SLO(x); });
      break;
    case 0x03:
      cpus[0x03].RMW_IX([](CPU *cpu, Fluint8 x) { return cpu->SLO(x); });
      break;
    case 0x13:
      cpus[0x13].RMW_IY([](CPU *cpu, Fluint8 x) { return cpu->SLO(x); });
      break;

    /* SRE */
    case 0x47:
      cpus[0x47].RMW_ZP([](CPU *cpu, Fluint8 x) { return cpu->SRE(x); });
      break;
    case 0x57:
      cpus[0x57].RMW_ZPX([](CPU *cpu, Fluint8 x) { return cpu->SRE(x); });
      break;
    case 0x4F:
      cpus[0x4F].RMW_AB([](CPU *cpu, Fluint8 x) { return cpu->SRE(x); });
      break;
    case 0x5F:
      cpus[0x5F].RMW_ABX([](CPU *cpu, Fluint8 x) { return cpu->SRE(x); });
      break;
    case 0x5B:
      cpus[0x5B].RMW_ABY([](CPU *cpu, Fluint8 x) { return cpu->SRE(x); });
      break;
    case 0x43:
      cpus[0x43].RMW_IX([](CPU *cpu, Fluint8 x) { return cpu->SRE(x); });
      break;
    case 0x53:
      cpus[0x53].RMW_IY([](CPU *cpu, Fluint8 x) { return cpu->SRE(x); });
      break;

    /* AXA - SHA */
    case 0x93:
      cpus[0x93].ST_IY([](CPU *cpu, Fluint16 AA) { return cpu->AXA(AA); });
      break;
    case 0x9F:
      cpus[0x9F].ST_ABY([](CPU *cpu, Fluint16 AA) { return cpu->AXA(AA); });
      break;

    /* SYA */
    case 0x9C:
      cpus[0x9C].ST_ABX([](CPU *cpu, Fluint16 AA) { return cpu->SYA(AA); });
      break;

    /* SXA */
    case 0x9E:
      cpus[0x9E].ST_ABY([](CPU *cpu, Fluint16 AA) { return cpu->SXA(AA); });
      break;

    /* XAS */
    case 0x9B:
      cpus[0x9B].XAS();
      break;

    /* TOP */
    case 0x0C:
      cpus[0x0C].LD_AB([](CPU *cpu, Fluint8 x) { });
      break;

    case 0x1C: cpus[0x1C].LD_ABX([](CPU *cpu, Fluint8 x) { }); break;
    case 0x3C: cpus[0x3C].LD_ABX([](CPU *cpu, Fluint8 x) { }); break;
    case 0x5C: cpus[0x5C].LD_ABX([](CPU *cpu, Fluint8 x) { }); break;
    case 0x7C: cpus[0x7C].LD_ABX([](CPU *cpu, Fluint8 x) { }); break;
    case 0xDC: cpus[0xDC].LD_ABX([](CPU *cpu, Fluint8 x) { }); break;
    case 0xFC: cpus[0xFC].LD_ABX([](CPU *cpu, Fluint8 x) { }); break;

    case 0x8B: cpus[0x8B].XAA(); break;

    default:
      CHECK(false) << "Unimplemented";
    }
    };

    #if RUN_ALL_INSTRUCTIONS
    auto RunOne = [&](int idx) {
        // Run the corresponding instruction.
        RunInstruction(idx);

        #if !FAST_INSTRUCTION_DISPATCH
        #define MAYBE_CLEAR_REG8(reg) \
          cpus[idx]. reg = Fluint8::If(cpus[idx].active, cpus[idx]. reg )
        #define MAYBE_CLEAR_REG16(reg) \
          cpus[idx]. reg = Fluint16::If(cpus[idx].active, cpus[idx]. reg )

        // Zero the registers if this instruction wasn't active,
        // so that we can sum them in serial below.
        MAYBE_CLEAR_REG8(reg_A);
        MAYBE_CLEAR_REG8(reg_X);
        MAYBE_CLEAR_REG8(reg_Y);
        MAYBE_CLEAR_REG8(reg_S);
        MAYBE_CLEAR_REG8(reg_P);
        MAYBE_CLEAR_REG8(reg_PI);
        MAYBE_CLEAR_REG8(jammed);
        MAYBE_CLEAR_REG16(reg_PC);
        MAYBE_CLEAR_REG16(cycles);

        #endif
      };
    ParallelComp(256, RunOne, NUM_THREADS);
    #else
    // (Dispatching on the instruction byte is cheating.)
    Fluint8::Cheat();
    RunInstruction(opcode.ToInt());
    #endif

    // Now copy the result of the actual instruction back.
    #if FAST_INSTRUCTION_DISPATCH
    Fluint8::Cheat();
    cpu = cpus[opcode.ToInt()];
    // Maybe we should make a distinction between one of these sub-CPUs
    // and the main one?
    cpu.active = Fluint8(0x01);
    ADDCYC(cpu.cycles.ToInt());
    cpu.cycles = Fluint16(0);

    #else
    // Real way. We basically sum up all the component instructions,
    // but all of the inactive ones have been zeroed above.

    #define CLEARREG8(reg) cpu. reg = Fluint8(0x00)
    #define CLEARREG16(reg) cpu. reg = Fluint16(0x00)


    #define COPYREG8(idx, reg) do {                                     \
        cpu. reg = Fluint8::PlusNoOverflow(cpu. reg , cpus[idx]. reg ); \
      } while (false)

    // PlusNoByteOverflow is safe here because at most one
    // term will be nonzero.
    #define COPYREG16(idx, reg) do {                                        \
        cpu. reg = Fluint16::PlusNoByteOverflow(cpu. reg, cpus[idx]. reg ); \
      } while (false)

    CLEARREG8(reg_A);
    CLEARREG8(reg_X);
    CLEARREG8(reg_Y);
    CLEARREG8(reg_S);
    CLEARREG8(reg_P);
    CLEARREG8(reg_PI);
    CLEARREG8(jammed);
    CLEARREG16(reg_PC);
    // But not clearing cycles...

    for (int i = 0; i < 256; i++) {
      COPYREG8(i, reg_A);
      COPYREG8(i, reg_X);
      COPYREG8(i, reg_Y);
      COPYREG8(i, reg_S);
      COPYREG8(i, reg_P);
      COPYREG8(i, reg_PI);
      COPYREG8(i, jammed);

      COPYREG16(i, reg_PC);
      // The cycles field is the count consumed by that particular
      // instruction; we're adding it rather than overwriting since
      // it's supposed to be cumulative. Note also that memory handlers
      // (if executed) could have updated this for the main CPU.
      // ADDCYC(Fluint16::If(cpus[i].active, cpus[i].cycles).ToInt());
      ADDCYC(cpus[i].cycles.ToInt());
    }

    #endif
  }
}
