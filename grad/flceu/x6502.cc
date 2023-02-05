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

// Several undocumented instructions AND with the high byte
// of the address, plus one. Computes that expression.
static Fluint8 WeirdHiByte(Fluint16 aa, Fluint8 r) {
  Fluint8 hi = (aa - Fluint16(r)).Hi();
  return hi + Fluint8(0x01);
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
  reg_PC = Fluint16(0);
  reg_A = reg_X = reg_Y = reg_S = reg_P = reg_PI = Fluint8(0);
  jammed = 0;
  count = 0;
  IRQlow = 0;
  DB = 0;
  timestamp = 0;
  MapIRQHook = nullptr;
}

void X6502::Power() {
  count = tcount = IRQlow = 0;
  reg_PC = Fluint16(0);
  reg_A = reg_X = reg_Y = reg_P = reg_PI = Fluint8(0);
  reg_S = Fluint8(0xFD);
  DB = jammed = 0;

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
        reg_PC = Fluint16(hi, lo);
        jammed = 0;
        reg_PI = reg_P = I_FLAG;
        IRQlow &= ~FCEU_IQRESET;
      } else if (IRQlow & FCEU_IQNMI2) {
        IRQlow &= ~FCEU_IQNMI2;
        IRQlow |= FCEU_IQNMI;
      } else if (IRQlow & FCEU_IQNMI) {
        if (!jammed) {
          ADDCYC(7);
          PUSH16(reg_PC);
          const Fluint8 pnb = Fluint8::AndWith<(uint8_t)~B_FLAG8>(reg_P);
          PUSH(Fluint8::OrWith<U_FLAG8>(pnb));
          reg_P = Fluint8::OrWith<I_FLAG8>(reg_P);
          Fluint8 lo = RdMem(Fluint16(0xFFFA));
          Fluint8 hi = RdMem(Fluint16(0xFFFB));
          reg_PC = Fluint16(hi, lo);
          IRQlow &= ~FCEU_IQNMI;
        }
      } else {
        const Fluint8 fpi = Fluint8::AndWith<I_FLAG8>(reg_PI);
        if (fpi.ToInt() == 0 && !jammed) {
          ADDCYC(7);
          PUSH16(reg_PC);
          const Fluint8 pnb = Fluint8::AndWith<(uint8_t)~B_FLAG8>(reg_P);
          PUSH(Fluint8::OrWith<U_FLAG8>(pnb));
          reg_P = Fluint8::OrWith<I_FLAG8>(reg_P);
          Fluint8 lo = RdMem(Fluint16(0xFFFE));
          Fluint8 hi = RdMem(Fluint16(0xFFFF));
          reg_PC = Fluint16(hi, lo);
        }
      }
      IRQlow &= ~(FCEU_IQTEMP);
      if (count <= 0) {
        reg_PI = reg_P;
        return;
        // Should increase accuracy without a
        // major speed hit.
      }
    }

    reg_PI = reg_P;
    // Get the next instruction.

    const Fluint8 b1 = RdMem(reg_PC);
    // printf("Read %x -> opcode %02x\n", reg_PC, b1);

    ADDCYC(CycTable[b1.ToInt()]);

    int32 temp = tcount;
    tcount = 0;
    if (MapIRQHook) MapIRQHook(fc, temp);
    fc->sound->SoundCPUHook(temp);
    reg_PC++;

    // XXX dispatching on the instruction byte is (probably?)
    // cheating
    Fluint8::Cheat();

    // XXX DO NOT SUBMIT
    #if 0
    static int64 trace_cycles = 0;
    if (trace_cycles++ < 100000) {
      printf("%04x:%02x  %02x.%02x.%02x.%02x.%02x\n",
             reg_PC.ToInt(), b1,
             reg_A.ToInt(), reg_X.ToInt(), reg_Y.ToInt(), reg_S.ToInt(), reg_P.ToInt());
    }
    #endif

    inst_histo[b1.ToInt()]++;

    switch (b1.ToInt()) {
      case 0x00: { /* BRK */
        reg_PC++;
        PUSH16(reg_PC);
        PUSH(Fluint8::OrWith<(uint8_t)(U_FLAG8 | B_FLAG8)>(reg_P));
        reg_P = Fluint8::OrWith<I_FLAG8>(reg_P);
        reg_PI = Fluint8::OrWith<I_FLAG8>(reg_PI);
        Fluint8 lo = RdMem(Fluint16(0xFFFE));
        Fluint8 hi = RdMem(Fluint16(0xFFFF));
        reg_PC = Fluint16(hi, lo);
        break;
      }

      case 0x40: /* RTI */
        reg_P = POP();
        /* reg_PI=reg_P; This is probably incorrect, so it's commented out. */
        reg_PI = reg_P;
        reg_PC = POP16();
        break;

      case 0x60: /* RTS */
        reg_PC = POP16();
        reg_PC++;
        break;

      case 0x48:
        /* PHA */
        PUSH(reg_A);
        break;
      case 0x08:
        /* PHP */
        PUSH(Fluint8::OrWith<(uint8_t)(U_FLAG8 | B_FLAG8)>(reg_P));
        break;
      case 0x68:
        /* PLA */
        reg_A = POP();
        X_ZN(reg_A);
        break;

      case 0x28:
        /* PLP */
        reg_P = POP();
        break;
      case 0x4C: {
        /* JMP ABSOLUTE */
        // XXX use pc directly
        Fluint16 ptmp(reg_PC);
        Fluint8 lo = RdMem(ptmp);
        Fluint8 hi = RdMem(ptmp + Fluint8(0x01));
        reg_PC = Fluint16(hi, lo);
        break;
      }

      case 0x6C: {
        /* JMP INDIRECT */
        Fluint16 tmp = GetAB();
        Fluint8 lo = RdMem(tmp);
        Fluint8 hi = RdMem(((tmp + Fluint8(0x01)) & Fluint16(0x00FF)) |
                           (tmp & Fluint16(0xFF00)));
        reg_PC = Fluint16(hi, lo);
        break;
      }

      case 0x20: {
        /* JSR */
        Fluint16 opc(reg_PC);
        Fluint16 opc1 = opc + Fluint8(0x01);
        Fluint8 lo = RdMem(opc);
        PUSH16(opc1);
        Fluint8 hi = RdMem(opc1);
        reg_PC = Fluint16(hi, lo);
        break;
      }
      case 0xAA: /* TAX */
        reg_X = reg_A;
        X_ZN(reg_A);
        break;

      case 0x8A: /* TXA */
        reg_A = reg_X;
        X_ZN(reg_A);
        break;

      case 0xA8: /* TAY */
        reg_Y = reg_A;
        X_ZN(reg_A);
        break;
      case 0x98: /* TYA */
        reg_A = reg_Y;
        X_ZN(reg_A);
        break;

      case 0xBA: /* TSX */
        reg_X = reg_S;
        X_ZN(reg_X);
        break;
      case 0x9A: /* TXS */
        reg_S = reg_X;
        break;

      case 0xCA: /* DEX */
        reg_X--;
        X_ZN(reg_X);
        break;
      case 0x88: /* DEY */
        reg_Y--;
        X_ZN(reg_Y);
        break;

      case 0xE8: /* INX */
        reg_X++;
        X_ZN(reg_X);
        break;
      case 0xC8: /* INY */
        reg_Y++;
        X_ZN(reg_Y);
        break;

      case 0x18: /* CLC */
        reg_P = Fluint8::AndWith<(uint8_t)~C_FLAG8>(reg_P);
        break;
      case 0xD8: /* CLD */
        reg_P = Fluint8::AndWith<(uint8_t)~D_FLAG8>(reg_P);
        break;
      case 0x58: /* CLI */
        reg_P = Fluint8::AndWith<(uint8_t)~I_FLAG8>(reg_P);
        break;
      case 0xB8: /* CLV */
        reg_P = Fluint8::AndWith<(uint8_t)~V_FLAG8>(reg_P);
        break;

      case 0x38: /* SEC */
        reg_P = Fluint8::OrWith<C_FLAG8>(reg_P);
        break;
      case 0xF8: /* SED */
        reg_P = Fluint8::OrWith<D_FLAG8>(reg_P);
        break;
      case 0x78: /* SEI */
        reg_P = Fluint8::OrWith<I_FLAG8>(reg_P);
        break;

      case 0xEA: /* NOP */ break;


    case 0x0A: RMW_A([this](Fluint8 x) { return ASL(x); }); break;
    case 0x06: RMW_ZP([this](Fluint8 x) { return ASL(x); }); break;
    case 0x16: RMW_ZPX([this](Fluint8 x) { return ASL(x); }); break;
    case 0x0E: RMW_AB([this](Fluint8 x) { return ASL(x); }); break;
    case 0x1E: RMW_ABX([this](Fluint8 x) { return ASL(x); }); break;

    case 0xC6: RMW_ZP([this](Fluint8 x) { return DEC(x); }); break;
    case 0xD6: RMW_ZPX([this](Fluint8 x) { return DEC(x); }); break;
    case 0xCE: RMW_AB([this](Fluint8 x) { return DEC(x); }); break;
    case 0xDE: RMW_ABX([this](Fluint8 x) { return DEC(x); }); break;

    case 0xE6: RMW_ZP([this](Fluint8 x) { return INC(x); }); break;
    case 0xF6: RMW_ZPX([this](Fluint8 x) { return INC(x); }); break;
    case 0xEE: RMW_AB([this](Fluint8 x) { return INC(x); }); break;
    case 0xFE: RMW_ABX([this](Fluint8 x) { return INC(x); }); break;

    case 0x4A: RMW_A([this](Fluint8 x) { return LSR(x); }); break;
    case 0x46: RMW_ZP([this](Fluint8 x) { return LSR(x); }); break;
    case 0x56: RMW_ZPX([this](Fluint8 x) { return LSR(x); }); break;
    case 0x4E: RMW_AB([this](Fluint8 x) { return LSR(x); }); break;
    case 0x5E: RMW_ABX([this](Fluint8 x) { return LSR(x); }); break;

    case 0x2A: RMW_A([this](Fluint8 x) { return ROL(x); }); break;
    case 0x26: RMW_ZP([this](Fluint8 x) { return ROL(x); }); break;
    case 0x36: RMW_ZPX([this](Fluint8 x) { return ROL(x); }); break;
    case 0x2E: RMW_AB([this](Fluint8 x) { return ROL(x); }); break;
    case 0x3E: RMW_ABX([this](Fluint8 x) { return ROL(x); }); break;

    case 0x6A: RMW_A([this](Fluint8 x) { return ROR(x); }); break;
    case 0x66: RMW_ZP([this](Fluint8 x) { return ROR(x); }); break;
    case 0x76: RMW_ZPX([this](Fluint8 x) { return ROR(x); }); break;
    case 0x6E: RMW_AB([this](Fluint8 x) { return ROR(x); }); break;
    case 0x7E: RMW_ABX([this](Fluint8 x) { return ROR(x); }); break;

    case 0x69: LD_IM([this](Fluint8 x) { ADC(x); }); break;
    case 0x65: LD_ZP([this](Fluint8 x) { ADC(x); }); break;
    case 0x75: LD_ZPX([this](Fluint8 x) { ADC(x); }); break;
    case 0x6D: LD_AB([this](Fluint8 x) { ADC(x); }); break;
    case 0x7D: LD_ABX([this](Fluint8 x) { ADC(x); }); break;
    case 0x79: LD_ABY([this](Fluint8 x) { ADC(x); }); break;
    case 0x61: LD_IX([this](Fluint8 x) { ADC(x); }); break;
    case 0x71: LD_IY([this](Fluint8 x) { ADC(x); }); break;

    case 0x29: LD_IM([this](Fluint8 x) { AND(x); }); break;
    case 0x25: LD_ZP([this](Fluint8 x) { AND(x); }); break;
    case 0x35: LD_ZPX([this](Fluint8 x) { AND(x); }); break;
    case 0x2D: LD_AB([this](Fluint8 x) { AND(x); }); break;
    case 0x3D: LD_ABX([this](Fluint8 x) { AND(x); }); break;
    case 0x39: LD_ABY([this](Fluint8 x) { AND(x); }); break;
    case 0x21: LD_IX([this](Fluint8 x) { AND(x); }); break;
    case 0x31: LD_IY([this](Fluint8 x) { AND(x); }); break;

    case 0x24: LD_ZP([this](Fluint8 x) { BIT(x); }); break;
    case 0x2C: LD_AB([this](Fluint8 x) { BIT(x); }); break;


    case 0xC9: LD_IM([this](Fluint8 x) { CMPL(reg_A, x); }); break;
    case 0xC5: LD_ZP([this](Fluint8 x) { CMPL(reg_A, x); }); break;
    case 0xD5: LD_ZPX([this](Fluint8 x) { CMPL(reg_A, x); }); break;
    case 0xCD: LD_AB([this](Fluint8 x) { CMPL(reg_A, x); }); break;
    case 0xDD: LD_ABX([this](Fluint8 x) { CMPL(reg_A, x); }); break;
    case 0xD9: LD_ABY([this](Fluint8 x) { CMPL(reg_A, x); }); break;
    case 0xC1: LD_IX([this](Fluint8 x) { CMPL(reg_A, x); }); break;
    case 0xD1: LD_IY([this](Fluint8 x) { CMPL(reg_A, x); }); break;

    case 0xE0: LD_IM([this](Fluint8 x) { CMPL(reg_X, x); }); break;
    case 0xE4: LD_ZP([this](Fluint8 x) { CMPL(reg_X, x); }); break;
    case 0xEC: LD_AB([this](Fluint8 x) { CMPL(reg_X, x); }); break;

    case 0xC0: LD_IM([this](Fluint8 x) { CMPL(reg_Y, x); }); break;
    case 0xC4: LD_ZP([this](Fluint8 x) { CMPL(reg_Y, x); }); break;
    case 0xCC: LD_AB([this](Fluint8 x) { CMPL(reg_Y, x); }); break;

    case 0x49: LD_IM([this](Fluint8 x) { EOR(x); }); break;
    case 0x45: LD_ZP([this](Fluint8 x) { EOR(x); }); break;
    case 0x55: LD_ZPX([this](Fluint8 x) { EOR(x); }); break;
    case 0x4D: LD_AB([this](Fluint8 x) { EOR(x); }); break;
    case 0x5D: LD_ABX([this](Fluint8 x) { EOR(x); }); break;
    case 0x59: LD_ABY([this](Fluint8 x) { EOR(x); }); break;
    case 0x41: LD_IX([this](Fluint8 x) { EOR(x); }); break;
    case 0x51: LD_IY([this](Fluint8 x) { EOR(x); }); break;

    case 0xA9: LD_IM([this](Fluint8 x) { LDA(x); }); break;
    case 0xA5: LD_ZP([this](Fluint8 x) { LDA(x); }); break;
    case 0xB5: LD_ZPX([this](Fluint8 x) { LDA(x); }); break;
    case 0xAD: LD_AB([this](Fluint8 x) { LDA(x); }); break;
    case 0xBD: LD_ABX([this](Fluint8 x) { LDA(x); }); break;
    case 0xB9: LD_ABY([this](Fluint8 x) { LDA(x); }); break;
    case 0xA1: LD_IX([this](Fluint8 x) { LDA(x); }); break;
    case 0xB1: LD_IY([this](Fluint8 x) { LDA(x); }); break;

    case 0xA2: LD_IM([this](Fluint8 x) { LDX(x); }); break;
    case 0xA6: LD_ZP([this](Fluint8 x) { LDX(x); }); break;
    case 0xB6: LD_ZPY([this](Fluint8 x) { LDX(x); }); break;
    case 0xAE: LD_AB([this](Fluint8 x) { LDX(x); }); break;
    case 0xBE: LD_ABY([this](Fluint8 x) { LDX(x); }); break;

    case 0xA0: LD_IM([this](Fluint8 x) { LDY(x); }); break;
    case 0xA4: LD_ZP([this](Fluint8 x) { LDY(x); }); break;
    case 0xB4: LD_ZPX([this](Fluint8 x) { LDY(x); }); break;
    case 0xAC: LD_AB([this](Fluint8 x) { LDY(x); }); break;
    case 0xBC: LD_ABX([this](Fluint8 x) { LDY(x); }); break;

    case 0x09: LD_IM([this](Fluint8 x) { ORA(x); }); break;
    case 0x05: LD_ZP([this](Fluint8 x) { ORA(x); }); break;
    case 0x15: LD_ZPX([this](Fluint8 x) { ORA(x); }); break;
    case 0x0D: LD_AB([this](Fluint8 x) { ORA(x); }); break;
    case 0x1D: LD_ABX([this](Fluint8 x) { ORA(x); }); break;
    case 0x19: LD_ABY([this](Fluint8 x) { ORA(x); }); break;
    case 0x01: LD_IX([this](Fluint8 x) { ORA(x); }); break;
    case 0x11: LD_IY([this](Fluint8 x) { ORA(x); }); break;

    case 0xEB: /* (undocumented) */
    case 0xE9: LD_IM([this](Fluint8 x) { SBC(x); }); break;
    case 0xE5: LD_ZP([this](Fluint8 x) { SBC(x); }); break;
    case 0xF5: LD_ZPX([this](Fluint8 x) { SBC(x); }); break;
    case 0xED: LD_AB([this](Fluint8 x) { SBC(x); }); break;
    case 0xFD: LD_ABX([this](Fluint8 x) { SBC(x); }); break;
    case 0xF9: LD_ABY([this](Fluint8 x) { SBC(x); }); break;
    case 0xE1: LD_IX([this](Fluint8 x) { SBC(x); }); break;
    case 0xF1: LD_IY([this](Fluint8 x) { SBC(x); }); break;


    case 0x85: ST_ZP([this](Fluint16 AA) { return reg_A; }); break;
    case 0x95: ST_ZPX([this](Fluint16 AA) { return reg_A; }); break;
    case 0x8D: ST_AB([this](Fluint16 AA) { return reg_A; }); break;
    case 0x9D: ST_ABX([this](Fluint16 AA) { return reg_A; }); break;
    case 0x99: ST_ABY([this](Fluint16 AA) { return reg_A; }); break;
    case 0x81: ST_IX([this](Fluint16 AA) { return reg_A; }); break;
    case 0x91: ST_IY([this](Fluint16 AA) { return reg_A; }); break;

    case 0x86: ST_ZP([this](Fluint16 AA) { return reg_X; }); break;
    case 0x96: ST_ZPY([this](Fluint16 AA) { return reg_X; }); break;
    case 0x8E: ST_AB([this](Fluint16 AA) { return reg_X; }); break;

    case 0x84: ST_ZP([this](Fluint16 AA) { return reg_Y; }); break;
    case 0x94: ST_ZPX([this](Fluint16 AA) { return reg_Y; }); break;
    case 0x8C: ST_AB([this](Fluint16 AA) { return reg_Y; }); break;

    // PERF Since we are extracting a single
    // bit, can make something like HasBit
    // instead of using the full generality
    // of IsZero.

    /* BCC */
    case 0x90:
      JR(Fluint8::IsZero(Fluint8::AndWith<C_FLAG8>(reg_P)));
      break;

    /* BCS */
    case 0xB0:
      JR(Fluint8::IsntZero(Fluint8::AndWith<C_FLAG8>(reg_P)));
      break;

    /* BEQ */
    case 0xF0:
      JR(Fluint8::IsntZero(Fluint8::AndWith<Z_FLAG8>(reg_P)));
      break;

    /* BNE */
    case 0xD0:
      JR(Fluint8::IsZero(Fluint8::AndWith<Z_FLAG8>(reg_P)));
      break;

    /* BMI */
    case 0x30:
      JR(Fluint8::IsntZero(Fluint8::AndWith<N_FLAG8>(reg_P)));
      break;

    /* BPL */
    case 0x10:
      JR(Fluint8::IsZero(Fluint8::AndWith<N_FLAG8>(reg_P)));
      break;

    /* BVC */
    case 0x50:
      JR(Fluint8::IsZero(Fluint8::AndWith<V_FLAG8>(reg_P)));
      break;

    /* BVS */
    case 0x70:
      JR(Fluint8::IsntZero(Fluint8::AndWith<V_FLAG8>(reg_P)));
      break;

    /* Here comes the undocumented instructions block.  Note that this
       implementation may be "wrong".  If so, please tell me.
    */

      /* AAC */
    case 0x2B:
    case 0x0B:
      LD_IM([this](Fluint8 x) {
          AND(x);
          reg_P = Fluint8::AndWith<(uint8_t)~C_FLAG8>(reg_P);
          reg_P |= Fluint8::RightShift<7>(reg_A);
        });
      break;

      /* AAX */
    case 0x87: ST_ZP([this](Fluint16 AA) { return reg_A & reg_X; }); break;
    case 0x97: ST_ZPY([this](Fluint16 AA) { return reg_A & reg_X; }); break;
    case 0x8F: ST_AB([this](Fluint16 AA) { return reg_A & reg_X; }); break;
    case 0x83: ST_IX([this](Fluint16 AA) { return reg_A & reg_X; }); break;

    /* ARR - ARGH, MATEY! */
    case 0x6B: {
      LD_IM([this](Fluint8 x) {
          AND(x);
          reg_P =
            Fluint8::PlusNoOverflow(
                Fluint8::AndWith<(uint8_t)~V_FLAG8>(reg_P),
                Fluint8::AndWith<V_FLAG8>(reg_A ^
                                          Fluint8::RightShift<1>(reg_A)));
          Fluint8 arrtmp = Fluint8::RightShift<7>(reg_A);
          reg_A = Fluint8::RightShift<1>(reg_A);
          reg_A |= Fluint8::LeftShift<7>(Fluint8::AndWith<C_FLAG8>(reg_P));
          reg_P = Fluint8::AndWith<(uint8_t)~C_FLAG8>(reg_P);
          reg_P |= arrtmp;
          X_ZN(reg_A);
        });
      break;
    }

    /* ASR */
    case 0x4B:
      LD_IM([this](Fluint8 x) { AND(x); LSRA(); });
      break;

    /* ATX(OAL) Is this(OR with $EE) correct? Blargg did some test
       and found the constant to be OR with is $FF for NES

       (but of course OR with FF is degenerate! -tom7) */
    case 0xAB:
      LD_IM([this](Fluint8 x) {
          reg_A = Fluint8::OrWith<0xFF>(reg_A);
          AND(x);
          reg_X = reg_A;
        });
      break;

    /* AXS */
    case 0xCB:
      LD_IM([this](Fluint8 x) { AXS(x); });
      break;

      /* DCP */
    case 0xC7: RMW_ZP([this](Fluint8 x) {
        x = DEC(x);
        CMPL(reg_A, x);
        return x;
      });
      break;
    case 0xD7: RMW_ZPX([this](Fluint8 x) {
        x = DEC(x);
        CMPL(reg_A, x);
        return x;
      });
      break;
    case 0xCF: RMW_AB([this](Fluint8 x) {
        x = DEC(x);
        CMPL(reg_A, x);
        return x;
      });
      break;
    case 0xDF: RMW_ABX([this](Fluint8 x) {
        x = DEC(x);
        CMPL(reg_A, x);
        return x;
      });
      break;
    case 0xDB: RMW_ABY([this](Fluint8 x) {
        x = DEC(x);
        CMPL(reg_A, x);
        return x;
      });
      break;
    case 0xC3: RMW_IX([this](Fluint8 x) {
        x = DEC(x);
        CMPL(reg_A, x);
        return x;
      });
      break;
    case 0xD3: RMW_IY([this](Fluint8 x) {
        x = DEC(x);
        CMPL(reg_A, x);
        return x;
      });
      break;

      /* ISB */
    case 0xE7: RMW_ZP([this](Fluint8 x) {
        x = INC(x);
        return SBC(x);
      });
      break;
    case 0xF7: RMW_ZPX([this](Fluint8 x) {
        x = INC(x);
        return SBC(x);
      });
      break;
    case 0xEF: RMW_AB([this](Fluint8 x) {
        x = INC(x);
        return SBC(x);
      });
      break;
    case 0xFF: RMW_ABX([this](Fluint8 x) {
        x = INC(x);
        return SBC(x);
      });
      break;
    case 0xFB: RMW_ABY([this](Fluint8 x) {
        x = INC(x);
        return SBC(x);
      });
      break;
    case 0xE3: RMW_IX([this](Fluint8 x) {
        x = INC(x);
        return SBC(x);
      });
      break;
    case 0xF3: RMW_IY([this](Fluint8 x) {
        x = INC(x);
        return SBC(x);
      });
      break;

    /* DOP */
    case 0x04: reg_PC++; break;
    case 0x14: reg_PC++; break;
    case 0x34: reg_PC++; break;
    case 0x44: reg_PC++; break;
    case 0x54: reg_PC++; break;
    case 0x64: reg_PC++; break;
    case 0x74: reg_PC++; break;

    case 0x80: reg_PC++; break;
    case 0x82: reg_PC++; break;
    case 0x89: reg_PC++; break;
    case 0xC2: reg_PC++; break;
    case 0xD4: reg_PC++; break;
    case 0xE2: reg_PC++; break;
    case 0xF4: reg_PC++; break;

    /* KIL */

    case 0x02:
    case 0x12:
    case 0x22:
    case 0x32:
    case 0x42:
    case 0x52:
    case 0x62:
    case 0x72:
    case 0x92:
    case 0xB2:
    case 0xD2:
    case 0xF2:
      ADDCYC(0xFF);
      jammed = 1;
      reg_PC--;
      break;

    /* LAR */
    case 0xBB:
      RMW_ABY([this](Fluint8 x) {
          reg_S &= x;
          reg_A = reg_X = reg_S;
          X_ZN(reg_X);
          return x;
        });
      break;

      /* LAX */
    case 0xA7: LD_ZP([this](Fluint8 x) { LDA(x); LDX(x); }); break;
    case 0xB7: LD_ZPY([this](Fluint8 x) { LDA(x); LDX(x); }); break;
    case 0xAF: LD_AB([this](Fluint8 x) { LDA(x); LDX(x); }); break;
    case 0xBF: LD_ABY([this](Fluint8 x) { LDA(x); LDX(x); }); break;
    case 0xA3: LD_IX([this](Fluint8 x) { LDA(x); LDX(x); }); break;
    case 0xB3: LD_IY([this](Fluint8 x) { LDA(x); LDX(x); }); break;

    /* NOP */
    case 0x1A:
    case 0x3A:
    case 0x5A:
    case 0x7A:
    case 0xDA:
    case 0xFA:
      break;

      /* RLA */
    case 0x27: RMW_ZP([this](Fluint8 x) {
        x = ROL(x);
        AND(x);
        return x;
      });
      break;
    case 0x37: RMW_ZPX([this](Fluint8 x) {
        x = ROL(x);
        AND(x);
        return x;
      });
      break;
    case 0x2F: RMW_AB([this](Fluint8 x) {
        x = ROL(x);
        AND(x);
        return x;
      });
      break;
    case 0x3F: RMW_ABX([this](Fluint8 x) {
        x = ROL(x);
        AND(x);
        return x;
      });
      break;
    case 0x3B: RMW_ABY([this](Fluint8 x) {
        x = ROL(x);
        AND(x);
        return x;
      });
      break;
    case 0x23: RMW_IX([this](Fluint8 x) {
        x = ROL(x);
        AND(x);
        return x;
      });
      break;
    case 0x33: RMW_IY([this](Fluint8 x) {
        x = ROL(x);
        AND(x);
        return x;
      });
      break;

      /* RRA */
    case 0x67: RMW_ZP([this](Fluint8 x) {
        x = ROR(x);
        return ADC(x);
      });
      break;
    case 0x77: RMW_ZPX([this](Fluint8 x) {
        x = ROR(x);
        return ADC(x);
      });
      break;
    case 0x6F: RMW_AB([this](Fluint8 x) {
        x = ROR(x);
        return ADC(x);
      });
      break;
    case 0x7F: RMW_ABX([this](Fluint8 x) {
        x = ROR(x);
        return ADC(x);
      });
      break;
    case 0x7B: RMW_ABY([this](Fluint8 x) {
        x = ROR(x);
        return ADC(x);
      });
      break;
    case 0x63: RMW_IX([this](Fluint8 x) {
        x = ROR(x);
        return ADC(x);
      });
      break;
    case 0x73: RMW_IY([this](Fluint8 x) {
        x = ROR(x);
        return ADC(x);
      });
      break;

      /* SLO */
    case 0x07: RMW_ZP([this](Fluint8 x) {
        x = ASL(x);
        ORA(x);
        return x;
      });
      break;
    case 0x17: RMW_ZPX([this](Fluint8 x) {
        x = ASL(x);
        ORA(x);
        return x;
      });
      break;
    case 0x0F: RMW_AB([this](Fluint8 x) {
        x = ASL(x);
        ORA(x);
        return x;
      });
      break;
    case 0x1F: RMW_ABX([this](Fluint8 x) {
        x = ASL(x);
        ORA(x);
        return x;
      });
      break;
    case 0x1B: RMW_ABY([this](Fluint8 x) {
        x = ASL(x);
        ORA(x);
        return x;
      });
      break;
    case 0x03: RMW_IX([this](Fluint8 x) {
        x = ASL(x);
        ORA(x);
        return x;
      });
      break;
    case 0x13: RMW_IY([this](Fluint8 x) {
        x = ASL(x);
        ORA(x);
        return x;
      });
      break;

      /* SRE */
    case 0x47: RMW_ZP([this](Fluint8 x) {
        x = LSR(x);
        EOR(x);
        return x;
      });
      break;
    case 0x57: RMW_ZPX([this](Fluint8 x) {
        x = LSR(x);
        EOR(x);
        return x;
      });
      break;
    case 0x4F: RMW_AB([this](Fluint8 x) {
        x = LSR(x);
        EOR(x);
        return x;
      });
      break;
    case 0x5F: RMW_ABX([this](Fluint8 x) {
        x = LSR(x);
        EOR(x);
        return x;
      });
      break;
    case 0x5B: RMW_ABY([this](Fluint8 x) {
        x = LSR(x);
        EOR(x);
        return x;
      });
      break;
    case 0x43: RMW_IX([this](Fluint8 x) {
        x = LSR(x);
        EOR(x);
        return x;
      });
      break;
    case 0x53: RMW_IY([this](Fluint8 x) {
        x = LSR(x);
        EOR(x);
        return x;
      });
      break;

    /* AXA - SHA */
    case 0x93:
      ST_IY([this](Fluint16 AA) {
          return reg_A & reg_X & WeirdHiByte(AA, reg_Y);
        });
      break;
    case 0x9F:
      ST_ABY([this](Fluint16 AA) {
          return reg_A & reg_X & WeirdHiByte(AA, reg_Y);
        });
      break;

    /* SYA */
    case 0x9C:
      ST_ABX([this](Fluint16 AA) {
          return reg_Y & WeirdHiByte(AA, reg_X);
      });
      break;

    /* SXA */
    case 0x9E:
      ST_ABY([this](Fluint16 AA) {
          return reg_X & WeirdHiByte(AA, reg_Y);
      });
      break;

    /* XAS */
    case 0x9B:
      reg_S = reg_A & reg_X;
      ST_ABY([this](Fluint16 AA) {
          return reg_S & WeirdHiByte(AA, reg_Y);
      });
      break;

    /* TOP */
    case 0x0C:
      LD_AB([this](Fluint8 x) { });
      break;
    case 0x1C:
    case 0x3C:
    case 0x5C:
    case 0x7C:
    case 0xDC:
    case 0xFC:
      LD_ABX([this](Fluint8 x) { });
      break;

    /* XAA - BIG QUESTION MARK HERE */
    case 0x8B:
      reg_A = Fluint8::OrWith<0xEE>(reg_A);
      reg_A &= reg_X;
      LD_IM([this](Fluint8 x) { AND(x); });
      break;
    }
  }
}
