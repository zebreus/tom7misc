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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef _X6502_H
#define _X6502_H

#include <cstdint>
#include <bit>

#include "tracing.h"
#include "fceu.h"
#include "fc.h"

#include "hfluint8.h"
#include "hfluint16.h"

struct X6502 {
  // Initialize with fc pointer, since memory reads/writes
  // trigger callbacks.
  explicit X6502(FC *fc);

  // PERF: Cheap but unnecessary histogram of instructions
  // used.
  int64 inst_histo[256] = {};
  void ClearInstHisto() {
    for (int i = 0; i < 256; i++) inst_histo[i] = 0;
  }

  /* Temporary cycle counter */
  int32 tcount;

  // XXX make sure if you change something you also change its
  // size in state.cc.

  // I think this is set for some instructions that halt the
  // processor until an interrupt.
  uint8 jammed;

  uint16_t GetPC() const { return cpu.reg_PC.ToInt(); }
  uint8 GetA() const { return cpu.reg_A.ToInt(); }
  uint8 GetX() const { return cpu.reg_X.ToInt(); }
  uint8 GetY() const { return cpu.reg_Y.ToInt(); }
  uint8 GetS() const { return cpu.reg_S.ToInt(); }
  uint8 GetP() const { return cpu.reg_P.ToInt(); }
  uint8 GetPI() const { return cpu.reg_PI.ToInt(); }

  int32 count;
  /* Simulated IRQ pin held low (or is it high?).
     And other junk hooked on for speed reasons. */
  uint32 IRQlow;
  /* Data bus "cache" for reads from certain areas */
  uint8 DB;

  void Run(int32 cycles);
  void RunLoop();

  void Init();
  void Reset();
  void Power();

  void TriggerNMI();
  void TriggerNMI2();

  void IRQBegin(int w);
  void IRQEnd(int w);

  // DMA read and write, I think. Like normal memory read/write,
  // but consumes a CPU cycle. These are (only) used externally.
  uint8 DMR(uint32 A);
  void DMW(uint32 A, uint8 V);

  uint32 timestamp = 0;

  void (*MapIRQHook)(FC *, int) = nullptr;

  // TODO: compute some hash of memory reads/writes so that we can
  // ensure that we're preserving the order for them
  uint64_t mem_trace = 0;
  inline void ClearMemTrace() { mem_trace = 0; }
  inline void TraceRead(uint16_t AA) {
    // XXX improve this hash!
    mem_trace ^= AA;
    mem_trace = std::rotr(mem_trace, 31);
    mem_trace = mem_trace * 5 + 0xe6546b64;
  }

  inline void TraceWrite(uint16_t AA, uint8_t VV) {
    // XXX improve this hash!
    mem_trace ^= AA;
    mem_trace = std::rotr(mem_trace, 41);
    mem_trace += VV;
    mem_trace = mem_trace * 5 + 0xe6546b64;
  }

  struct CPU {
    // Program counter.
    hfluint16 reg_PC;
    // A is accumulator, X and Y other general purpose registes.
    // S is the stack pointer. Stack grows "down" (push decrements).
    // P is processor flags (from msb to lsb: NVUBDIZC).
    // PI is another copy of the processor flags, maybe having
    // to do with interrupt handling, which I don't understand.
    hfluint8 reg_A, reg_X, reg_Y, reg_S, reg_P, reg_PI;
    // Cycle count for a single instruction.
    hfluint16 cycles;

    hfluint8 jammed;
    // 1 if this instruction is active, otherwise 0.
    hfluint8 active;

    X6502 *parent;

    void AddCycle(hfluint8 x) {
      cycles += x;
    }

    // These local versions of memory I/O only perform the operation
    // if active is 0x01 (passing this to the native RdMemIf etc.).
    inline hfluint8 RdMem(hfluint16 A) {
      return parent->RdMemIf(active, A);
    }

    inline void WrMem(hfluint16 A, hfluint8 V) {
      parent->WrMemIf(active, A, V);
    }

    // We could make this non-conditional if we had a local
    // memory bus (DB), but I don't think that is more
    // authentic.
    inline hfluint8 RdRAM(hfluint16 A) {
      return parent->RdRAMIf(active, A);
    }

    inline void WrRAM(hfluint16 A, hfluint8 V) {
      parent->WrRAMIf(active, A, V);
    }

    inline void BRK() {
      reg_PC++;
      PUSH16(reg_PC);
      PUSH(hfluint8::OrWith<(uint8_t)(U_FLAG8 | B_FLAG8)>(reg_P));
      reg_P = hfluint8::OrWith<I_FLAG8>(reg_P);
      reg_PI = hfluint8::OrWith<I_FLAG8>(reg_PI);
      hfluint8 lo = RdMem(hfluint16(0xFFFE));
      hfluint8 hi = RdMem(hfluint16(0xFFFF));
      reg_PC = hfluint16(hi, lo);
    }

    inline void RTI() {
      reg_P = POP();
      /* reg_PI=reg_P; This is probably incorrect, so it's commented out. */
      reg_PI = reg_P;
      reg_PC = POP16();
    }

    inline void RTS() {
      reg_PC = POP16();
      reg_PC++;
    }

    inline void PHA() {
      PUSH(reg_A);
    }

    inline void PLA() {
      reg_A = POP();
      X_ZN(reg_A);
    }

    inline void PHP() {
      PUSH(hfluint8::OrWith<(uint8_t)(U_FLAG8 | B_FLAG8)>(reg_P));
    }

    inline void PLP() {
      reg_P = POP();
    }

    inline void JMPABS() {
      /* JMP ABSOLUTE */
      // XXX use pc directly
      hfluint16 ptmp(reg_PC);
      hfluint8 lo = RdMem(ptmp);
      hfluint8 hi = RdMem(ptmp + hfluint8(0x01));
      reg_PC = hfluint16(hi, lo);
    }

    inline void JMPIND() {
      /* JMP INDIRECT */
      hfluint16 tmp = GetAB();
      hfluint8 lo = RdMem(tmp);
      hfluint8 hi = RdMem(((tmp + hfluint8(0x01)) & hfluint16(0x00FF)) |
                         (tmp & hfluint16(0xFF00)));
      reg_PC = hfluint16(hi, lo);
    }

    inline void JSR() {
      /* JSR */
      hfluint16 opc(reg_PC);
      hfluint16 opc1 = opc + hfluint8(0x01);
      hfluint8 lo = RdMem(opc);
      PUSH16(opc1);
      hfluint8 hi = RdMem(opc1);
      reg_PC = hfluint16(hi, lo);
    }

    inline void ARR(hfluint8 x) {
      /* ARR - ARGH, MATEY! */
      AND(x);
      reg_P =
        hfluint8::PlusNoOverflow(
            hfluint8::AndWith<(uint8_t)~V_FLAG8>(reg_P),
            hfluint8::AndWith<V_FLAG8>(reg_A ^
                                      hfluint8::RightShift<1>(reg_A)));
      hfluint8 arrtmp = hfluint8::RightShift<7>(reg_A);
      reg_A = hfluint8::RightShift<1>(reg_A);
      reg_A |= hfluint8::LeftShift<7>(hfluint8::AndWith<C_FLAG8>(reg_P));
      reg_P = hfluint8::AndWith<(uint8_t)~C_FLAG8>(reg_P);
      reg_P |= arrtmp;
      X_ZN(reg_A);
    }

    inline void TAX() {
      reg_X = reg_A;
      X_ZN(reg_A);
    }

    inline void TXA() {
      reg_A = reg_X;
      X_ZN(reg_A);
    }

    inline void TAY() {
      reg_Y = reg_A;
      X_ZN(reg_A);
    }

    inline void TYA() {
      reg_A = reg_Y;
      X_ZN(reg_A);
    }

    inline void TSX() {
      reg_X = reg_S;
      X_ZN(reg_X);
    }

    inline void TXS() {
      reg_S = reg_X;
    }

    inline void DEX() {
      reg_X--;
      X_ZN(reg_X);
    }

    inline void DEY() {
      reg_Y--;
      X_ZN(reg_Y);
    }

    inline void INX() {
      reg_X++;
      X_ZN(reg_X);
    }

    inline void INY() {
      reg_Y++;
      X_ZN(reg_Y);
    }

    inline void CLC() {
      reg_P = hfluint8::AndWith<(uint8_t)~C_FLAG8>(reg_P);
    }

    inline void CLD() {
      reg_P = hfluint8::AndWith<(uint8_t)~D_FLAG8>(reg_P);
    }

    inline void CLI() {
      reg_P = hfluint8::AndWith<(uint8_t)~I_FLAG8>(reg_P);
    }

    inline void CLV() {
      reg_P = hfluint8::AndWith<(uint8_t)~V_FLAG8>(reg_P);
    }

    inline void SEC() {
      reg_P = hfluint8::OrWith<C_FLAG8>(reg_P);
    }

    inline void SED() {
      reg_P = hfluint8::OrWith<D_FLAG8>(reg_P);
    }

    inline void SEI() {
      reg_P = hfluint8::OrWith<I_FLAG8>(reg_P);
    }

    // PERF For the branch instructions, since we are extracting a
    // single bit, can make something like HasBit instead of using the
    // full generality of IsZero.
    inline void BCC() {
      JR(hfluint8::IsZero(hfluint8::AndWith<C_FLAG8>(reg_P)));
    }

    inline void BCS() {
      JR(hfluint8::IsntZero(hfluint8::AndWith<C_FLAG8>(reg_P)));
    }

    inline void BEQ() {
      JR(hfluint8::IsntZero(hfluint8::AndWith<Z_FLAG8>(reg_P)));
    }

    inline void BNE() {
      JR(hfluint8::IsZero(hfluint8::AndWith<Z_FLAG8>(reg_P)));
    }

    inline void BMI() {
      JR(hfluint8::IsntZero(hfluint8::AndWith<N_FLAG8>(reg_P)));
    }

    inline void BPL() {
      JR(hfluint8::IsZero(hfluint8::AndWith<N_FLAG8>(reg_P)));
    }

    inline void BVC() {
      JR(hfluint8::IsZero(hfluint8::AndWith<V_FLAG8>(reg_P)));
    }

    inline void BVS() {
      JR(hfluint8::IsntZero(hfluint8::AndWith<V_FLAG8>(reg_P)));
    }

    inline void AAC(hfluint8 x) {
      AND(x);
      reg_P = hfluint8::AndWith<(uint8_t)~C_FLAG8>(reg_P);
      reg_P |= hfluint8::RightShift<7>(reg_A);
    }

    inline void ATX(hfluint8 x) {
      /* ATX(OAL) Is this(OR with $EE) correct? Blargg did some test
         and found the constant to be OR with is $FF for NES

         (but of course OR with FF is degenerate! -tom7) */
      reg_A = hfluint8::OrWith<0xFF>(reg_A);
      AND(x);
      reg_X = reg_A;
    }

    inline hfluint8 DCP(hfluint8 x) {
      x = DEC(x);
      CMPL(reg_A, x);
      return x;
    }

    inline hfluint8 ISB(hfluint8 x) {
      x = INC(x);
      return SBC(x);
    }

    inline void KIL() {
      AddCycle(hfluint8(0xFF));
      jammed = hfluint8(0x01);
      reg_PC--;
    }

    inline hfluint8 LAR(hfluint8 x) {
      reg_S &= x;
      reg_A = reg_X = reg_S;
      X_ZN(reg_X);
      return x;
    }

    inline void LAX(hfluint8 x) {
      LDA(x);
      LDX(x);
    }

    inline hfluint8 RLA(hfluint8 x) {
      x = ROL(x);
      AND(x);
      return x;
    }

    inline hfluint8 RRA(hfluint8 x) {
      x = ROR(x);
      return ADC(x);
    }

    inline hfluint8 SLO(hfluint8 x) {
      x = ASL(x);
      ORA(x);
      return x;
    }

    inline hfluint8 SRE(hfluint8 x) {
      x = LSR(x);
      EOR(x);
      return x;
    }

    inline hfluint8 AXA(hfluint16 AA) {
      return reg_A & reg_X & WeirdHiByte(AA, reg_Y);
    }

    inline hfluint8 SYA(hfluint16 AA) {
      return reg_Y & WeirdHiByte(AA, reg_X);
    }

    inline hfluint8 SXA(hfluint16 AA) {
      return reg_X & WeirdHiByte(AA, reg_Y);
    }

    inline void PUSH(hfluint8 v) {
      WrRAM(hfluint16(0x100) + reg_S, v);
      reg_S--;
    }

    inline void PUSH16(hfluint16 vv) {
      PUSH(vv.Hi());
      PUSH(vv.Lo());
    }

    inline hfluint8 POP() {
      reg_S++;
      return RdRAM(hfluint16(0x100) + reg_S);
    }

    inline hfluint16 POP16() {
      hfluint8 lo = POP();
      hfluint8 hi = POP();
      return hfluint16(hi, lo);
    }

    // Several undocumented instructions AND with the high byte
    // of the address, plus one. Computes that expression.
    static hfluint8 WeirdHiByte(hfluint16 aa, hfluint8 r) {
      hfluint8 hi = (aa - hfluint16(r)).Hi();
      return hi + hfluint8(0x01);
    }

    /* Indexed Indirect */
    hfluint16 GetIX() {
      hfluint8 tmp = RdMem(reg_PC);
      reg_PC++;
      tmp += reg_X;
      hfluint8 lo = RdRAM(hfluint16(tmp));
      tmp++;
      hfluint8 hi = RdRAM(hfluint16(tmp));
      return hfluint16(hi, lo);
    }

    // Zero Page
    hfluint8 GetZP() {
      hfluint8 ret = RdMem(reg_PC);
      reg_PC++;
      return ret;
    }

    /* Zero Page Indexed */
    hfluint8 GetZPI(hfluint8 i) {
      hfluint8 ret = i + RdMem(reg_PC);
      reg_PC++;
      return ret;
    }

    /* Absolute */
    hfluint16 GetAB() {
      hfluint8 lo = RdMem(reg_PC);
      reg_PC++;
      hfluint8 hi = RdMem(reg_PC);
      reg_PC++;
      return hfluint16(hi, lo);
    }

    /* Absolute Indexed (for writes and rmws) */
    hfluint16 GetABIWR(hfluint8 i) {
      hfluint16 rt = GetAB();
      hfluint16 target = rt + i;
      (void)RdMem((target & hfluint16(0x00FF)) |
                  (rt & hfluint16(0xFF00)));
      return target;
    }

    void XAS() {
      // Can this be done inside the ST_ABY, making this
      // undocumented instruction more normal-seeming?
      // Should be okay unless read/write hooks care
      // about the stack pointer?
      // (No test coverage for this...)
      reg_S = reg_A & reg_X;
      ST_ABY([](CPU *cpu, hfluint16 AA) {
          return cpu->reg_S & cpu->WeirdHiByte(AA, cpu->reg_Y);
      });
    }

    void XAA() {
      // XXX similar...
      reg_A = hfluint8::OrWith<0xEE>(reg_A);
      reg_A &= reg_X;
      LD_IM([](CPU *cpu, hfluint8 x) { cpu->AND(x); });
    }

    /* Absolute Indexed (for reads) */
    hfluint16 GetABIRD(hfluint8 i) {
      hfluint16 tmp = GetAB();
      hfluint16 ret = tmp + i;
      hfluint8 cc = hfluint16::RightShift<8>((ret ^ tmp) & hfluint16(0x100)).Lo();
      (void)parent->RdMemIf(hfluint8::BooleanAnd(cc, active),
                            ret ^ hfluint16(0x100));

      AddCycle(cc);

      return ret;
    }

    /* Indirect Indexed (for reads) */
    hfluint16 GetIYRD() {
      hfluint8 tmp(RdMem(reg_PC));
      reg_PC++;
      hfluint8 lo = RdRAM(hfluint16(tmp));
      hfluint8 hi = RdRAM(hfluint16(tmp + hfluint8(0x01)));
      hfluint16 rt(hi, lo);
      hfluint16 ret = rt + reg_Y;
      hfluint8 cc = hfluint16::RightShift<8>((ret ^ rt) & hfluint16(0x100)).Lo();
      (void)parent->RdMemIf(hfluint8::BooleanAnd(cc, active),
                            ret ^ hfluint16(0x100));

      AddCycle(cc);

      return ret;
    }


    /* Indirect Indexed(for writes and rmws) */
    hfluint16 GetIYWR() {
      hfluint8 tmp(RdMem(reg_PC));
      reg_PC++;
      hfluint8 lo = RdRAM(hfluint16(tmp));
      hfluint8 hi = RdRAM(hfluint16(tmp + hfluint8(0x01)));
      hfluint16 rt(hi, lo);
      hfluint16 ret = rt + reg_Y;
      (void)RdMem((ret & hfluint16(0x00FF)) |
                  (rt & hfluint16(0xFF00)));
      return ret;
    }

    // Implements the ZNTable (zero flag and negative flag),
    // returning 0, Z_FLAG, or N_FLAG.
    hfluint8 ZnFlags(hfluint8 zort) {
      static_assert(N_FLAG8 == 0x80, "This requires the negative flag "
                    "to be the same as the sign bit.");
      static_assert(Z_FLAG8 == 0x02, "This expects the zero flag to "
                    "have a specific value, although this would be "
                    "easily remedied.");
      hfluint8 zf = hfluint8::LeftShift1Under128(hfluint8::IsZero(zort));
      hfluint8 nf = hfluint8::AndWith<N_FLAG8>(zort);
      // Can't overflow because these are two different bits.
      hfluint8 res = hfluint8::PlusNoOverflow(nf, zf);

      return res;
    }

    void X_ZN(hfluint8 zort) {
      reg_P = hfluint8::AndWith<(uint8_t)~(Z_FLAG8 | N_FLAG8)>(reg_P);
      // We just masked out the bits, so this can't overflow.
      reg_P = hfluint8::PlusNoOverflow(reg_P, ZnFlags(zort));
    }

    void X_ZNT(hfluint8 zort) {
      reg_P |= ZnFlags(zort);
    }

    void LDA(hfluint8 x) {
      reg_A = x;
      X_ZN(reg_A);
    }
    void LDX(hfluint8 x) {
      reg_X = x;
      X_ZN(reg_X);
    }
    void LDY(hfluint8 x) {
      reg_Y = x;
      X_ZN(reg_Y);
    }

    void AND(hfluint8 x) {
      reg_A &= x;
      X_ZN(reg_A);
    }

    void BIT(hfluint8 x) {
      reg_P = hfluint8::AndWith<(uint8_t)~(Z_FLAG8 | V_FLAG8 | N_FLAG8)>(reg_P);
      // PERF: AddNoOverflow
      /* PERF can simplify this ... just use iszero? */
      reg_P |= hfluint8::AndWith<Z_FLAG8>(ZnFlags(x & reg_A));
      reg_P |= hfluint8::AndWith<(uint8_t)(V_FLAG8 | N_FLAG8)>(x);
    }

    void EOR(hfluint8 x) {
      reg_A ^= x;
      X_ZN(reg_A);
    }
    void ORA(hfluint8 x) {
      reg_A |= x;
      X_ZN(reg_A);
    }

    void CMPL(hfluint8 a1, hfluint8 a2) {
      auto [carry, diff] = hfluint8::SubtractWithCarry(a1, a2);
      X_ZN(diff);
      reg_P =
        hfluint8::PlusNoOverflow(
            hfluint8::AndWith<(uint8_t)~C_FLAG8>(reg_P),
            hfluint8::XorWith<C_FLAG8>(
                hfluint8::AndWith<C_FLAG8>(carry)));
    }

    // Input must be 0x01 or 0x00.
    void JR(hfluint8 cond) {
      {
        uint8_t cc = cond.ToInt();
        CHECK(cc == 0x00 || cc == 0x01) << cc;
      }

      // Signed displacement. We'll only use it if cond is true.
      hfluint8 disp =
        parent->RdMemIf(hfluint8::BooleanAnd(cond, active), reg_PC);

      // Program counter incremented whether the branch is
      // taken or not (which makes sense as we would not
      // want to execute the displacement byte!)
      reg_PC++;

      AddCycle(cond);

      hfluint16 old_pc = reg_PC;

      // Only modify the PC if condition is true. We have to
      // sign extend it to 16 bits first (but this does nothing
      // to zero).
      reg_PC += hfluint16::SignExtend(hfluint8::If(cond, disp));

      // Additional cycle is taken if this crosses a "page" boundary.
      // Note this does nothing if the cond is false, as old_pc = reg_PC
      // in that case.
      AddCycle(hfluint16::IsntZero((old_pc ^ reg_PC) & hfluint16(0x100)));
    }

    hfluint8 ASL(hfluint8 x) {
      reg_P = hfluint8::AndWith<(uint8_t)~C_FLAG8>(reg_P);
      reg_P |= hfluint8::RightShift<7>(x);
      x = hfluint8::LeftShift<1>(x);
      X_ZN(x);
      return x;
    }

    hfluint8 LSR(hfluint8 x) {
      reg_P = hfluint8::AndWith<(uint8_t)~(C_FLAG8 | N_FLAG8 | Z_FLAG8)>(reg_P);
      reg_P |= hfluint8::AndWith<1>(x);
      x = hfluint8::RightShift<1>(x);
      X_ZNT(x);
      return x;
    }

    hfluint8 DEC(hfluint8 x) {
      x--;
      X_ZN(x);
      return x;
    }

    hfluint8 INC(hfluint8 x) {
      x++;
      X_ZN(x);
      return x;
    }

    hfluint8 ROL(hfluint8 x) {
      hfluint8 l = hfluint8::RightShift<7>(x);
      x = hfluint8::LeftShift<1>(x);
      // PERF PlusNoOverflow
      x |= hfluint8::AndWith<C_FLAG8>(reg_P);
      reg_P = hfluint8::AndWith<(uint8_t)~(Z_FLAG8 | N_FLAG8 | C_FLAG8)>(reg_P);
      reg_P |= l;
      X_ZNT(x);
      return x;
    }

    hfluint8 ROR(hfluint8 x) {
      hfluint8 l = hfluint8::AndWith<1>(x);
      x = hfluint8::RightShift<1>(x);
      x |= hfluint8::LeftShift<7>(hfluint8::AndWith<C_FLAG8>(reg_P));
      reg_P = hfluint8::AndWith<(uint8_t)~(Z_FLAG8 | N_FLAG8 | C_FLAG8)>(reg_P);
      reg_P |= l;
      X_ZNT(x);
      return x;
    }

    template<class F>
    void ST_ZP(F rf) {
      hfluint16 AA(GetZP());
      WrRAM(AA, rf(this, AA));
    }

    template<class F>
    void ST_ZPX(F rf) {
      hfluint16 AA(GetZPI(reg_X));
      WrRAM(AA, rf(this, AA));
    }

    template<class F>
    void ST_ZPY(F rf) {
      hfluint16 AA(GetZPI(reg_Y));
      WrRAM(AA, rf(this, AA));
    }

    template<class F>
    void ST_AB(F rf) {
      hfluint16 AA = GetAB();
      WrMem(AA, rf(this, AA));
    }

    template<class F>
    void ST_ABI(hfluint8 reg, F rf) {
      hfluint16 AA = GetABIWR(reg);
      WrMem(AA, rf(this, AA));
    }

    template<class F>
    void ST_ABX(F rf) {
      return ST_ABI(reg_X, rf);
    }

    template<class F>
    void ST_ABY(F rf) {
      return ST_ABI(reg_Y, rf);
    }

    template<class F>
    void ST_IX(F rf) {
      hfluint16 AA = GetIX();
      WrMem(AA, rf(this, AA));
    }

    template<class F>
    void ST_IY(F rf) {
      hfluint16 AA = GetIYWR();
      WrMem(AA, rf(this, AA));
    }

    template<class F>
    void LD_IY(F op) {
      const hfluint16 AA = GetIYRD();
      const hfluint8 x = RdMem(AA);
      op(this, x);
    }

    template<class F>
    void LD_IX(F op) {
      const hfluint16 AA = GetIX();
      const hfluint8 x = RdMem(AA);
      op(this, x);
    }

    template<class F>
    void LD_AB(F op) {
      const hfluint16 AA = GetAB();
      const hfluint8 x = RdMem(AA);
      op(this, x);
    }

    template<class F>
    void LD_ABI(hfluint8 reg, F op) {
      const hfluint16 AA = GetABIRD(reg);
      const hfluint8 x = RdMem(AA);
      op(this, x);
    }
    template<class F> void LD_ABX(F op) { LD_ABI(reg_X, op); }
    template<class F> void LD_ABY(F op) { LD_ABI(reg_Y, op); }

    template<class F>
    void LD_ZPY(F op) {
      const hfluint16 AA(GetZPI(reg_Y));
      const hfluint8 x = RdRAM(AA);
      op(this, x);
    }

    template<class F>
    void LD_ZPX(F op) {
      const hfluint16 AA(GetZPI(reg_X));
      const hfluint8 x = RdRAM(AA);
      op(this, x);
    }

    template<class F>
    void LD_ZP(F op) {
      const hfluint16 AA(GetZP());
      const hfluint8 x = RdRAM(AA);
      op(this, x);
    }

    template<class F>
    void LD_IM(F op) {
      const hfluint8 x = RdMem(reg_PC);
      reg_PC++;
      op(this, x);
    }

    template<class F>
    void RMW_ZPX(F op) {
      const hfluint16 AA(GetZPI(reg_X));
      hfluint8 x = RdRAM(AA);
      x = op(this, x);
      WrRAM(AA, x);
    }

    template<class F>
    void RMW_ZP(F op) {
      const hfluint16 AA(GetZP());
      hfluint8 x = RdRAM(AA);
      x = op(this, x);
      WrRAM(AA, x);
    }

    template<class F>
    void RMW_IY(F op) {
      (void)GetIX();
      const hfluint16 AA = GetIYWR();
      hfluint8 x = RdMem(AA);
      WrMem(AA, x);
      x = op(this, x);
      WrMem(AA, x);
    }

    template<class F>
    void RMW_IX(F op) {
      const hfluint16 AA = GetIX();
      hfluint8 x = RdMem(AA);
      WrMem(AA, x);
      x = op(this, x);
      WrMem(AA, x);
    }

    template<class F>
    void RMW_ABI(hfluint8 reg, F op) {
      const hfluint16 AA = GetABIWR(reg);
      hfluint8 x = RdMem(AA);
      WrMem(AA, x);
      x = op(this, x);
      WrMem(AA, x);
    }

    template<class F> void RMW_ABX(F op) { RMW_ABI(reg_X, op); }
    template<class F> void RMW_ABY(F op) { RMW_ABI(reg_Y, op); }

    template<class F>
    void RMW_AB(F op) {
      const hfluint16 AA = GetAB();
      hfluint8 x = RdMem(AA);
      WrMem(AA, x);
      x = op(this, x);
      WrMem(AA, x);
    }

    template<class F>
    void RMW_A(F op) {
      hfluint8 x = reg_A;
      x = op(this, x);
      reg_A = x;
    }

    hfluint8 ADC(hfluint8 x) {
      static_assert(C_FLAG8 == 0x01, "we assume this is the one's place");
      const hfluint8 p_carry_bit = hfluint8::AndWith<C_FLAG8>(reg_P);
      auto [carry1, sum1] = hfluint8::AddWithCarry(reg_A, x);
      auto [carry2, sum] = hfluint8::AddWithCarry(sum1, p_carry_bit);

      // Since p_carry_bit is at most 1, these can't both overflow.
      hfluint8 carry = hfluint8::PlusNoOverflow(carry1, carry2);

      // uint32 l = reg_A.ToInt() + (x).ToInt() + p_carry_bit.ToInt();
      reg_P = hfluint8::AndWith<
        (uint8_t)~(Z_FLAG8 | C_FLAG8 | N_FLAG8 | V_FLAG8)>(reg_P);
      // The overflow is for signed arithmetic. It tells us if we've
      // added two positive numbers but got a negative one, or added two
      // negative numbers but got a positive one. (If the signs are
      // different, overflow is not possible.) This is computed from the
      // sign bits.
      hfluint8 aaa = hfluint8::XorWith<0x80>(
          hfluint8::AndWith<0x80>(reg_A ^ x));
      hfluint8 bbb = hfluint8::AndWith<0x80>(reg_A ^ sum);
      static_assert(V_FLAG8 == 0x40);

      CHECK((reg_P.ToInt() & (V_FLAG8 | C_FLAG8)) == 0);
      // Sets overflow bit, which was cleared above.
      reg_P = hfluint8::PlusNoOverflow(reg_P, hfluint8::RightShift<1>(aaa & bbb));
      // Sets carry bit, which was cleared above.
      reg_P = hfluint8::PlusNoOverflow(reg_P, carry);
      reg_A = sum;
      // PERF since we already cleared Z and N flags, can use
      // PlusNoOverflow
      X_ZNT(reg_A);
      return x;
    }

    hfluint8 SBC(hfluint8 x) {
      static_assert(C_FLAG8 == 0x01, "we assume this is the one's place");
      // On 6502, the borrow flag is !Carry.
      hfluint8 p_ncarry_bit = hfluint8::XorWith<C_FLAG8>(
          hfluint8::AndWith<C_FLAG8>(reg_P));

      auto [carry1, diff1] = hfluint8::SubtractWithCarry(reg_A, x);
      auto [carry2, diff] = hfluint8::SubtractWithCarry(diff1, p_ncarry_bit);

      // As in ADC.
      hfluint8 carry = hfluint8::PlusNoOverflow(carry1, carry2);

      // uint32 l = reg_A.ToInt() - x.ToInt() - p_ncarry_bit.ToInt();
      reg_P = hfluint8::AndWith<
        (uint8_t)~(Z_FLAG8 | C_FLAG8 | N_FLAG8 | V_FLAG8)>(reg_P);
      // As above, detect overflow by looking at sign bits.
      hfluint8 aaa = reg_A ^ diff;
      hfluint8 bbb = reg_A ^ x;
      hfluint8 overflow = hfluint8::AndWith<0x80>(aaa & bbb);
      static_assert(V_FLAG8 == 0x40);

      CHECK((reg_P.ToInt() & (V_FLAG8 | C_FLAG8)) == 0);
      // V_FLAG8 bit is cleared above.
      reg_P = hfluint8::PlusNoOverflow(reg_P, hfluint8::RightShift<1>(overflow));
      // C_FLAG8 bit is cleared above.
      reg_P = hfluint8::PlusNoOverflow(
          reg_P,
          hfluint8::XorWith<C_FLAG8>(hfluint8::AndWith<C_FLAG8>(carry)));
      reg_A = diff;
      // PERF since we already cleared Z and N flags, can use
      // PlusNoOverflow here too
      X_ZNT(reg_A);
      return x;
    }

    void LSRA() {
      /* For undocumented instructions, maybe for other things later... */
      reg_P = hfluint8::AndWith<(uint8_t)~(C_FLAG8 | N_FLAG8 | Z_FLAG8)>(reg_P);
      reg_P |= hfluint8::AndWith<1>(reg_A);
      reg_A = hfluint8::RightShift<1>(reg_A);
      X_ZNT(reg_A);
    }

    /* Special undocumented operation.  Very similar to CMP. */
    void AXS(hfluint8 x) {
      auto [carry, diff] =
        hfluint8::SubtractWithCarry(reg_A & reg_X, x);
      // uint32 t = (reg_A & reg_X).ToInt() - x.ToInt();
      X_ZN(diff);
      reg_P =
        hfluint8::PlusNoOverflow(
            hfluint8::AndWith<(uint8_t)~C_FLAG8>(reg_P),
            hfluint8::XorWith<C_FLAG8>(
                hfluint8::AndWith<C_FLAG8>(carry)));
      reg_X = diff;
    }

    // End CPU
  };

  CPU cpu;

private:

  void ADDCYC(int x) {
    this->tcount += x;
    this->count -= x * 48;
    timestamp += x;
  }

  // normal memory read, which calls hooks. We trace the sequence
  // of these to make sure we're not dropping or reordering them
  // (which often does not matter to games, but could).
  inline hfluint8 RdMem(hfluint16 A) {
    return RdMemIf(hfluint8(0x01), A);
  }

  inline hfluint8 RdMemIf(hfluint8 cond, hfluint16 A) {
    const uint8_t cc = cond.ToInt();
    CHECK(cc == 0x00 || cc == 0x01) << cc;
    if (cc == 0x01) {
      uint16_t AA = A.ToInt();
      TraceRead(AA);
      DB = fc->fceu->ARead[AA](fc, AA);
      return hfluint8(DB);
    } else {
      // Arbitrary
      return hfluint8(0x00);
    }
  }

  // normal memory write
  inline void WrMem(hfluint16 A, hfluint8 V) {
    WrMemIf(hfluint8(0x01), A, V);
  }

  inline void WrMemIf(hfluint8 cond, hfluint16 A, hfluint8 V) {
    const uint8_t cc = cond.ToInt();
    CHECK(cc == 0x00 || cc == 0x01) << cc;
    if (cc == 0x01) {
      uint16_t AA = A.ToInt();
      uint8_t VV = V.ToInt();
      TraceWrite(AA, VV);
      fc->fceu->BWrite[AA](fc, AA, VV);
    } else {
      // No effect
    }
  }

  inline hfluint8 RdRAM(hfluint16 A) {
    return RdRAMIf(hfluint8(0x01), A);
  }

  inline hfluint8 RdRAMIf(hfluint8 cond, hfluint16 A) {
    const uint8_t cc = cond.ToInt();
    CHECK(cc == 0x00 || cc == 0x01) << cc;
    if (cc == 0x01) {
      DB = fc->fceu->RAM[A.ToInt()];
      return hfluint8(DB);
    } else {
      // Arbitrary
      return hfluint8(0x00);
    }
  }

  inline void WrRAM(hfluint16 A, hfluint8 V) {
    return WrRAMIf(hfluint8(0x01), A, V);
  }

  inline void WrRAMIf(hfluint8 cond, hfluint16 A, hfluint8 V) {
    const uint8_t cc = cond.ToInt();
    CHECK(cc == 0x00 || cc == 0x01) << cc;
    if (cc == 0x01) {
      fc->fceu->RAM[A.ToInt()] = V.ToInt();
    } else {
      // No effect
    }
  }

  // Commonly we do bitwise ops with compile-time constants,
  // which can be faster than fully dynamic fluint operations.
  static constexpr uint8_t N_FLAG8{0x80};
  static constexpr uint8_t V_FLAG8{0x40};
  static constexpr uint8_t U_FLAG8{0x20};
  static constexpr uint8_t B_FLAG8{0x10};
  static constexpr uint8_t D_FLAG8{0x08};
  static constexpr uint8_t I_FLAG8{0x04};
  static constexpr uint8_t Z_FLAG8{0x02};
  static constexpr uint8_t C_FLAG8{0x01};

  // But the constants are also available as fluints.
  static constexpr hfluint8 N_FLAG{N_FLAG8};
  static constexpr hfluint8 V_FLAG{V_FLAG8};
  static constexpr hfluint8 U_FLAG{U_FLAG8};
  static constexpr hfluint8 B_FLAG{B_FLAG8};
  static constexpr hfluint8 D_FLAG{D_FLAG8};
  static constexpr hfluint8 I_FLAG{I_FLAG8};
  static constexpr hfluint8 Z_FLAG{Z_FLAG8};
  static constexpr hfluint8 C_FLAG{C_FLAG8};

  FC *fc = nullptr;
  DISALLOW_COPY_AND_ASSIGN(X6502);
};

#define NTSC_CPU 1789772.7272727272727272
#define PAL_CPU  1662607.125

#define FCEU_IQEXT      0x001
#define FCEU_IQEXT2     0x002
/* ... */
#define FCEU_IQRESET    0x020
#define FCEU_IQNMI2     0x040  // Delayed NMI, gets converted to *_IQNMI
#define FCEU_IQNMI      0x080
#define FCEU_IQDPCM     0x100
#define FCEU_IQFCOUNT   0x200
#define FCEU_IQTEMP     0x800

#endif
