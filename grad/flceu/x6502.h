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

#include "fluint8.h"
#include "fluint16.h"

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
    Fluint16 reg_PC;
    // A is accumulator, X and Y other general purpose registes.
    // S is the stack pointer. Stack grows "down" (push decrements).
    // P is processor flags (from msb to lsb: NVUBDIZC).
    // PI is another copy of the processor flags, maybe having
    // to do with interrupt handling, which I don't understand.
    Fluint8 reg_A, reg_X, reg_Y, reg_S, reg_P, reg_PI;
    // Cycle count for a single instruction.
    Fluint16 cycles;

    Fluint8 jammed;
    // 1 if this instruction is active, otherwise 0.
    Fluint8 active;

    X6502 *parent;

    void AddCycle(Fluint8 x) {
      cycles += x;
    }

    // These local versions of memory I/O only perform the operation
    // if active is 0x01 (passing this to the native RdMemIf etc.).
    inline Fluint8 RdMem(Fluint16 A) {
      return parent->RdMemIf(active, A);
    }

    inline void WrMem(Fluint16 A, Fluint8 V) {
      parent->WrMemIf(active, A, V);
    }

    // We could make this non-conditional if we had a local
    // memory bus (DB), but I don't think that is more
    // authentic.
    inline Fluint8 RdRAM(Fluint16 A) {
      return parent->RdRAMIf(active, A);
    }

    inline void WrRAM(Fluint16 A, Fluint8 V) {
      parent->WrRAMIf(active, A, V);
    }

    inline void BRK() {
      reg_PC++;
      PUSH16(reg_PC);
      PUSH(Fluint8::OrWith<(uint8_t)(U_FLAG8 | B_FLAG8)>(reg_P));
      reg_P = Fluint8::OrWith<I_FLAG8>(reg_P);
      reg_PI = Fluint8::OrWith<I_FLAG8>(reg_PI);
      Fluint8 lo = RdMem(Fluint16(0xFFFE));
      Fluint8 hi = RdMem(Fluint16(0xFFFF));
      reg_PC = Fluint16(hi, lo);
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
      PUSH(Fluint8::OrWith<(uint8_t)(U_FLAG8 | B_FLAG8)>(reg_P));
    }

    inline void PLP() {
      reg_P = POP();
    }

    inline void JMPABS() {
      /* JMP ABSOLUTE */
      // XXX use pc directly
      Fluint16 ptmp(reg_PC);
      Fluint8 lo = RdMem(ptmp);
      Fluint8 hi = RdMem(ptmp + Fluint8(0x01));
      reg_PC = Fluint16(hi, lo);
    }

    inline void JMPIND() {
      /* JMP INDIRECT */
      Fluint16 tmp = GetAB();
      Fluint8 lo = RdMem(tmp);
      Fluint8 hi = RdMem(((tmp + Fluint8(0x01)) & Fluint16(0x00FF)) |
                         (tmp & Fluint16(0xFF00)));
      reg_PC = Fluint16(hi, lo);
    }

    inline void JSR() {
      /* JSR */
      Fluint16 opc(reg_PC);
      Fluint16 opc1 = opc + Fluint8(0x01);
      Fluint8 lo = RdMem(opc);
      PUSH16(opc1);
      Fluint8 hi = RdMem(opc1);
      reg_PC = Fluint16(hi, lo);
    }

    inline void ARR(Fluint8 x) {
      /* ARR - ARGH, MATEY! */
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
      reg_P = Fluint8::AndWith<(uint8_t)~C_FLAG8>(reg_P);
    }

    inline void CLD() {
      reg_P = Fluint8::AndWith<(uint8_t)~D_FLAG8>(reg_P);
    }

    inline void CLI() {
      reg_P = Fluint8::AndWith<(uint8_t)~I_FLAG8>(reg_P);
    }

    inline void CLV() {
      reg_P = Fluint8::AndWith<(uint8_t)~V_FLAG8>(reg_P);
    }

    inline void SEC() {
      reg_P = Fluint8::OrWith<C_FLAG8>(reg_P);
    }

    inline void SED() {
      reg_P = Fluint8::OrWith<D_FLAG8>(reg_P);
    }

    inline void SEI() {
      reg_P = Fluint8::OrWith<I_FLAG8>(reg_P);
    }

    // PERF For the branch instructions, since we are extracting a
    // single bit, can make something like HasBit instead of using the
    // full generality of IsZero.
    inline void BCC() {
      JR(Fluint8::IsZero(Fluint8::AndWith<C_FLAG8>(reg_P)));
    }

    inline void BCS() {
      JR(Fluint8::IsntZero(Fluint8::AndWith<C_FLAG8>(reg_P)));
    }

    inline void BEQ() {
      JR(Fluint8::IsntZero(Fluint8::AndWith<Z_FLAG8>(reg_P)));
    }

    inline void BNE() {
      JR(Fluint8::IsZero(Fluint8::AndWith<Z_FLAG8>(reg_P)));
    }

    inline void BMI() {
      JR(Fluint8::IsntZero(Fluint8::AndWith<N_FLAG8>(reg_P)));
    }

    inline void BPL() {
      JR(Fluint8::IsZero(Fluint8::AndWith<N_FLAG8>(reg_P)));
    }

    inline void BVC() {
      JR(Fluint8::IsZero(Fluint8::AndWith<V_FLAG8>(reg_P)));
    }

    inline void BVS() {
      JR(Fluint8::IsntZero(Fluint8::AndWith<V_FLAG8>(reg_P)));
    }

    inline void AAC(Fluint8 x) {
      AND(x);
      reg_P = Fluint8::AndWith<(uint8_t)~C_FLAG8>(reg_P);
      reg_P |= Fluint8::RightShift<7>(reg_A);
    }

    inline void ATX(Fluint8 x) {
      /* ATX(OAL) Is this(OR with $EE) correct? Blargg did some test
         and found the constant to be OR with is $FF for NES

         (but of course OR with FF is degenerate! -tom7) */
      reg_A = Fluint8::OrWith<0xFF>(reg_A);
      AND(x);
      reg_X = reg_A;
    }

    inline Fluint8 DCP(Fluint8 x) {
      x = DEC(x);
      CMPL(reg_A, x);
      return x;
    }

    inline Fluint8 ISB(Fluint8 x) {
      x = INC(x);
      return SBC(x);
    }

    inline void KIL() {
      AddCycle(Fluint8(0xFF));
      jammed = Fluint8(0x01);
      reg_PC--;
    }

    inline Fluint8 LAR(Fluint8 x) {
      reg_S &= x;
      reg_A = reg_X = reg_S;
      X_ZN(reg_X);
      return x;
    }

    inline void LAX(Fluint8 x) {
      LDA(x);
      LDX(x);
    }

    inline Fluint8 RLA(Fluint8 x) {
      x = ROL(x);
      AND(x);
      return x;
    }

    inline Fluint8 RRA(Fluint8 x) {
      x = ROR(x);
      return ADC(x);
    }

    inline Fluint8 SLO(Fluint8 x) {
      x = ASL(x);
      ORA(x);
      return x;
    }

    inline Fluint8 SRE(Fluint8 x) {
      x = LSR(x);
      EOR(x);
      return x;
    }

    inline Fluint8 AXA(Fluint16 AA) {
      return reg_A & reg_X & WeirdHiByte(AA, reg_Y);
    }

    inline Fluint8 SYA(Fluint16 AA) {
      return reg_Y & WeirdHiByte(AA, reg_X);
    }

    inline Fluint8 SXA(Fluint16 AA) {
      return reg_X & WeirdHiByte(AA, reg_Y);
    }

    inline void PUSH(Fluint8 v) {
      WrRAM(Fluint16(0x100) + reg_S, v);
      reg_S--;
    }

    inline void PUSH16(Fluint16 vv) {
      PUSH(vv.Hi());
      PUSH(vv.Lo());
    }

    inline Fluint8 POP() {
      reg_S++;
      return RdRAM(Fluint16(0x100) + reg_S);
    }

    inline Fluint16 POP16() {
      Fluint8 lo = POP();
      Fluint8 hi = POP();
      return Fluint16(hi, lo);
    }

    // Several undocumented instructions AND with the high byte
    // of the address, plus one. Computes that expression.
    static Fluint8 WeirdHiByte(Fluint16 aa, Fluint8 r) {
      Fluint8 hi = (aa - Fluint16(r)).Hi();
      return hi + Fluint8(0x01);
    }

    /* Indexed Indirect */
    Fluint16 GetIX() {
      Fluint8 tmp = RdMem(reg_PC);
      reg_PC++;
      tmp += reg_X;
      Fluint8 lo = RdRAM(Fluint16(tmp));
      tmp++;
      Fluint8 hi = RdRAM(Fluint16(tmp));
      return Fluint16(hi, lo);
    }

    // Zero Page
    Fluint8 GetZP() {
      Fluint8 ret = RdMem(reg_PC);
      reg_PC++;
      return ret;
    }

    /* Zero Page Indexed */
    Fluint8 GetZPI(Fluint8 i) {
      Fluint8 ret = i + RdMem(reg_PC);
      reg_PC++;
      return ret;
    }

    /* Absolute */
    Fluint16 GetAB() {
      Fluint8 lo = RdMem(reg_PC);
      reg_PC++;
      Fluint8 hi = RdMem(reg_PC);
      reg_PC++;
      return Fluint16(hi, lo);
    }

    /* Absolute Indexed (for writes and rmws) */
    Fluint16 GetABIWR(Fluint8 i) {
      Fluint16 rt = GetAB();
      Fluint16 target = rt + i;
      (void)RdMem((target & Fluint16(0x00FF)) |
                  (rt & Fluint16(0xFF00)));
      return target;
    }

    void XAS() {
      // Can this be done inside the ST_ABY, making this
      // undocumented instruction more normal-seeming?
      // Should be okay unless read/write hooks care
      // about the stack pointer?
      // (No test coverage for this...)
      reg_S = reg_A & reg_X;
      ST_ABY([](CPU *cpu, Fluint16 AA) {
          return cpu->reg_S & cpu->WeirdHiByte(AA, cpu->reg_Y);
      });
    }

    void XAA() {
      // XXX similar...
      reg_A = Fluint8::OrWith<0xEE>(reg_A);
      reg_A &= reg_X;
      LD_IM([](CPU *cpu, Fluint8 x) { cpu->AND(x); });
    }

    /* Absolute Indexed (for reads) */
    Fluint16 GetABIRD(Fluint8 i) {
      Fluint16 tmp = GetAB();
      Fluint16 ret = tmp + i;
      Fluint8 cc = Fluint16::RightShift<8>((ret ^ tmp) & Fluint16(0x100)).Lo();
      // PERF: Could have a boolean & that assumes 1 or 0.
      (void)parent->RdMemIf(cc & active, ret ^ Fluint16(0x100));

      AddCycle(cc);

      return ret;
    }

    /* Indirect Indexed (for reads) */
    Fluint16 GetIYRD() {
      Fluint8 tmp(RdMem(reg_PC));
      reg_PC++;
      Fluint8 lo = RdRAM(Fluint16(tmp));
      Fluint8 hi = RdRAM(Fluint16(tmp + Fluint8(0x01)));
      Fluint16 rt(hi, lo);
      Fluint16 ret = rt + reg_Y;
      Fluint8 cc = Fluint16::RightShift<8>((ret ^ rt) & Fluint16(0x100)).Lo();
      // PERF: Could have a boolean & that assumes 1 or 0.
      (void)parent->RdMemIf(cc & active, ret ^ Fluint16(0x100));

      AddCycle(cc);

      return ret;
    }


    /* Indirect Indexed(for writes and rmws) */
    Fluint16 GetIYWR() {
      Fluint8 tmp(RdMem(reg_PC));
      reg_PC++;
      Fluint8 lo = RdRAM(Fluint16(tmp));
      Fluint8 hi = RdRAM(Fluint16(tmp + Fluint8(0x01)));
      Fluint16 rt(hi, lo);
      Fluint16 ret = rt + reg_Y;
      (void)RdMem((ret & Fluint16(0x00FF)) |
                  (rt & Fluint16(0xFF00)));
      return ret;
    }

    // Implements the ZNTable (zero flag and negative flag),
    // returning 0, Z_FLAG, or N_FLAG.
    Fluint8 ZnFlags(Fluint8 zort) {
      static_assert(N_FLAG8 == 0x80, "This requires the negative flag "
                    "to be the same as the sign bit.");
      static_assert(Z_FLAG8 == 0x02, "This expects the zero flag to "
                    "have a specific value, although this would be "
                    "easily remedied.");
      Fluint8 zf = Fluint8::LeftShift1Under128(Fluint8::IsZero(zort));
      Fluint8 nf = Fluint8::AndWith<N_FLAG8>(zort);
      // Can't overflow because these are two different bits.
      Fluint8 res = Fluint8::PlusNoOverflow(nf, zf);

      return res;
    }

    void X_ZN(Fluint8 zort) {
      reg_P = Fluint8::AndWith<(uint8_t)~(Z_FLAG8 | N_FLAG8)>(reg_P);
      // We just masked out the bits, so this can't overflow.
      reg_P = Fluint8::PlusNoOverflow(reg_P, ZnFlags(zort));
    }

    void X_ZNT(Fluint8 zort) {
      reg_P |= ZnFlags(zort);
    }

    void LDA(Fluint8 x) {
      reg_A = x;
      X_ZN(reg_A);
    }
    void LDX(Fluint8 x) {
      reg_X = x;
      X_ZN(reg_X);
    }
    void LDY(Fluint8 x) {
      reg_Y = x;
      X_ZN(reg_Y);
    }

    void AND(Fluint8 x) {
      reg_A &= x;
      X_ZN(reg_A);
    }

    void BIT(Fluint8 x) {
      reg_P = Fluint8::AndWith<(uint8_t)~(Z_FLAG8 | V_FLAG8 | N_FLAG8)>(reg_P);
      // PERF: AddNoOverflow
      /* PERF can simplify this ... just use iszero? */
      reg_P |= Fluint8::AndWith<Z_FLAG8>(ZnFlags(x & reg_A));
      reg_P |= Fluint8::AndWith<(uint8_t)(V_FLAG8 | N_FLAG8)>(x);
    }

    void EOR(Fluint8 x) {
      reg_A ^= x;
      X_ZN(reg_A);
    }
    void ORA(Fluint8 x) {
      reg_A |= x;
      X_ZN(reg_A);
    }

    void CMPL(Fluint8 a1, Fluint8 a2) {
      auto [carry, diff] = Fluint8::SubtractWithCarry(a1, a2);
      X_ZN(diff);
      reg_P =
        Fluint8::PlusNoOverflow(
            Fluint8::AndWith<(uint8_t)~C_FLAG8>(reg_P),
            Fluint8::XorWith<C_FLAG8>(
                Fluint8::AndWith<C_FLAG8>(carry)));
    }

    // Input must be 0x01 or 0x00.
    void JR(Fluint8 cond) {
      {
        uint8_t cc = cond.ToInt();
        CHECK(cc == 0x00 || cc == 0x01) << cc;
      }

      // Signed displacement. We'll only use it if cond is true.
      Fluint8 disp = parent->RdMemIf(cond & active, reg_PC);

      // Program counter incremented whether the branch is
      // taken or not (which makes sense as we would not
      // want to execute the displacement byte!)
      reg_PC++;

      AddCycle(cond);

      Fluint16 old_pc = reg_PC;

      // Only modify the PC if condition is true. We have to
      // sign extend it to 16 bits first (but this does nothing
      // to zero).
      reg_PC += Fluint16::SignExtend(Fluint8::If(cond, disp));

      // Additional cycle is taken if this crosses a "page" boundary.
      // Note this does nothing if the cond is false, as old_pc = reg_PC
      // in that case.
      AddCycle(Fluint16::IsntZero((old_pc ^ reg_PC) & Fluint16(0x100)));
    }

    Fluint8 ASL(Fluint8 x) {
      reg_P = Fluint8::AndWith<(uint8_t)~C_FLAG8>(reg_P);
      reg_P |= Fluint8::RightShift<7>(x);
      x = Fluint8::LeftShift<1>(x);
      X_ZN(x);
      return x;
    }

    Fluint8 LSR(Fluint8 x) {
      reg_P = Fluint8::AndWith<(uint8_t)~(C_FLAG8 | N_FLAG8 | Z_FLAG8)>(reg_P);
      reg_P |= Fluint8::AndWith<1>(x);
      x = Fluint8::RightShift<1>(x);
      X_ZNT(x);
      return x;
    }

    Fluint8 DEC(Fluint8 x) {
      x--;
      X_ZN(x);
      return x;
    }

    Fluint8 INC(Fluint8 x) {
      x++;
      X_ZN(x);
      return x;
    }

    Fluint8 ROL(Fluint8 x) {
      Fluint8 l = Fluint8::RightShift<7>(x);
      x = Fluint8::LeftShift<1>(x);
      // PERF PlusNoOverflow
      x |= Fluint8::AndWith<C_FLAG8>(reg_P);
      reg_P = Fluint8::AndWith<(uint8_t)~(Z_FLAG8 | N_FLAG8 | C_FLAG8)>(reg_P);
      reg_P |= l;
      X_ZNT(x);
      return x;
    }

    Fluint8 ROR(Fluint8 x) {
      Fluint8 l = Fluint8::AndWith<1>(x);
      x = Fluint8::RightShift<1>(x);
      x |= Fluint8::LeftShift<7>(Fluint8::AndWith<C_FLAG8>(reg_P));
      reg_P = Fluint8::AndWith<(uint8_t)~(Z_FLAG8 | N_FLAG8 | C_FLAG8)>(reg_P);
      reg_P |= l;
      X_ZNT(x);
      return x;
    }

    template<class F>
    void ST_ZP(F rf) {
      Fluint16 AA(GetZP());
      WrRAM(AA, rf(this, AA));
    }

    template<class F>
    void ST_ZPX(F rf) {
      Fluint16 AA(GetZPI(reg_X));
      WrRAM(AA, rf(this, AA));
    }

    template<class F>
    void ST_ZPY(F rf) {
      Fluint16 AA(GetZPI(reg_Y));
      WrRAM(AA, rf(this, AA));
    }

    template<class F>
    void ST_AB(F rf) {
      Fluint16 AA = GetAB();
      WrMem(AA, rf(this, AA));
    }

    template<class F>
    void ST_ABI(Fluint8 reg, F rf) {
      Fluint16 AA = GetABIWR(reg);
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
      Fluint16 AA = GetIX();
      WrMem(AA, rf(this, AA));
    }

    template<class F>
    void ST_IY(F rf) {
      Fluint16 AA = GetIYWR();
      WrMem(AA, rf(this, AA));
    }

    template<class F>
    void LD_IY(F op) {
      const Fluint16 AA = GetIYRD();
      const Fluint8 x = RdMem(AA);
      op(this, x);
    }

    template<class F>
    void LD_IX(F op) {
      const Fluint16 AA = GetIX();
      const Fluint8 x = RdMem(AA);
      op(this, x);
    }

    template<class F>
    void LD_AB(F op) {
      const Fluint16 AA = GetAB();
      const Fluint8 x = RdMem(AA);
      op(this, x);
    }

    template<class F>
    void LD_ABI(Fluint8 reg, F op) {
      const Fluint16 AA = GetABIRD(reg);
      const Fluint8 x = RdMem(AA);
      op(this, x);
    }
    template<class F> void LD_ABX(F op) { LD_ABI(reg_X, op); }
    template<class F> void LD_ABY(F op) { LD_ABI(reg_Y, op); }

    template<class F>
    void LD_ZPY(F op) {
      const Fluint16 AA(GetZPI(reg_Y));
      const Fluint8 x = RdRAM(AA);
      op(this, x);
    }

    template<class F>
    void LD_ZPX(F op) {
      const Fluint16 AA(GetZPI(reg_X));
      const Fluint8 x = RdRAM(AA);
      op(this, x);
    }

    template<class F>
    void LD_ZP(F op) {
      const Fluint16 AA(GetZP());
      const Fluint8 x = RdRAM(AA);
      op(this, x);
    }

    template<class F>
    void LD_IM(F op) {
      const Fluint8 x = RdMem(reg_PC);
      reg_PC++;
      op(this, x);
    }

    template<class F>
    void RMW_ZPX(F op) {
      const Fluint16 AA(GetZPI(reg_X));
      Fluint8 x = RdRAM(AA);
      x = op(this, x);
      WrRAM(AA, x);
    }

    template<class F>
    void RMW_ZP(F op) {
      const Fluint16 AA(GetZP());
      Fluint8 x = RdRAM(AA);
      x = op(this, x);
      WrRAM(AA, x);
    }

    template<class F>
    void RMW_IY(F op) {
      (void)GetIX();
      const Fluint16 AA = GetIYWR();
      Fluint8 x = RdMem(AA);
      WrMem(AA, x);
      x = op(this, x);
      WrMem(AA, x);
    }

    template<class F>
    void RMW_IX(F op) {
      const Fluint16 AA = GetIX();
      Fluint8 x = RdMem(AA);
      WrMem(AA, x);
      x = op(this, x);
      WrMem(AA, x);
    }

    template<class F>
    void RMW_ABI(Fluint8 reg, F op) {
      const Fluint16 AA = GetABIWR(reg);
      Fluint8 x = RdMem(AA);
      WrMem(AA, x);
      x = op(this, x);
      WrMem(AA, x);
    }

    template<class F> void RMW_ABX(F op) { RMW_ABI(reg_X, op); }
    template<class F> void RMW_ABY(F op) { RMW_ABI(reg_Y, op); }

    template<class F>
    void RMW_AB(F op) {
      const Fluint16 AA = GetAB();
      Fluint8 x = RdMem(AA);
      WrMem(AA, x);
      x = op(this, x);
      WrMem(AA, x);
    }

    template<class F>
    void RMW_A(F op) {
      Fluint8 x = reg_A;
      x = op(this, x);
      reg_A = x;
    }

    Fluint8 ADC(Fluint8 x) {
      static_assert(C_FLAG8 == 0x01, "we assume this is the one's place");
      const Fluint8 p_carry_bit = Fluint8::AndWith<C_FLAG8>(reg_P);
      auto [carry1, sum1] = Fluint8::AddWithCarry(reg_A, x);
      auto [carry2, sum] = Fluint8::AddWithCarry(sum1, p_carry_bit);

      // Since p_carry_bit is at most 1, these can't both overflow.
      Fluint8 carry = Fluint8::PlusNoOverflow(carry1, carry2);

      // uint32 l = reg_A.ToInt() + (x).ToInt() + p_carry_bit.ToInt();
      reg_P = Fluint8::AndWith<
        (uint8_t)~(Z_FLAG8 | C_FLAG8 | N_FLAG8 | V_FLAG8)>(reg_P);
      // The overflow is for signed arithmetic. It tells us if we've
      // added two positive numbers but got a negative one, or added two
      // negative numbers but got a positive one. (If the signs are
      // different, overflow is not possible.) This is computed from the
      // sign bits.
      Fluint8 aaa = Fluint8::XorWith<0x80>(
          Fluint8::AndWith<0x80>(reg_A ^ x));
      Fluint8 bbb = Fluint8::AndWith<0x80>(reg_A ^ sum);
      static_assert(V_FLAG8 == 0x40);

      CHECK((reg_P.ToInt() & (V_FLAG8 | C_FLAG8)) == 0);
      // Sets overflow bit, which was cleared above.
      reg_P = Fluint8::PlusNoOverflow(reg_P, Fluint8::RightShift<1>(aaa & bbb));
      // Sets carry bit, which was cleared above.
      reg_P = Fluint8::PlusNoOverflow(reg_P, carry);
      reg_A = sum;
      // PERF since we already cleared Z and N flags, can use
      // PlusNoOverflow
      X_ZNT(reg_A);
      return x;
    }

    Fluint8 SBC(Fluint8 x) {
      static_assert(C_FLAG8 == 0x01, "we assume this is the one's place");
      // On 6502, the borrow flag is !Carry.
      Fluint8 p_ncarry_bit = Fluint8::XorWith<C_FLAG8>(
          Fluint8::AndWith<C_FLAG8>(reg_P));

      auto [carry1, diff1] = Fluint8::SubtractWithCarry(reg_A, x);
      auto [carry2, diff] = Fluint8::SubtractWithCarry(diff1, p_ncarry_bit);

      // As in ADC.
      Fluint8 carry = Fluint8::PlusNoOverflow(carry1, carry2);

      // uint32 l = reg_A.ToInt() - x.ToInt() - p_ncarry_bit.ToInt();
      reg_P = Fluint8::AndWith<
        (uint8_t)~(Z_FLAG8 | C_FLAG8 | N_FLAG8 | V_FLAG8)>(reg_P);
      // As above, detect overflow by looking at sign bits.
      Fluint8 aaa = reg_A ^ diff;
      Fluint8 bbb = reg_A ^ x;
      Fluint8 overflow = Fluint8::AndWith<0x80>(aaa & bbb);
      static_assert(V_FLAG8 == 0x40);

      CHECK((reg_P.ToInt() & (V_FLAG8 | C_FLAG8)) == 0);
      // V_FLAG8 bit is cleared above.
      reg_P = Fluint8::PlusNoOverflow(reg_P, Fluint8::RightShift<1>(overflow));
      // C_FLAG8 bit is cleared above.
      reg_P = Fluint8::PlusNoOverflow(
          reg_P,
          Fluint8::XorWith<C_FLAG8>(Fluint8::AndWith<C_FLAG8>(carry)));
      reg_A = diff;
      // PERF since we already cleared Z and N flags, can use
      // PlusNoOverflow here too
      X_ZNT(reg_A);
      return x;
    }

    void LSRA() {
      /* For undocumented instructions, maybe for other things later... */
      reg_P = Fluint8::AndWith<(uint8_t)~(C_FLAG8 | N_FLAG8 | Z_FLAG8)>(reg_P);
      reg_P |= Fluint8::AndWith<1>(reg_A);
      reg_A = Fluint8::RightShift<1>(reg_A);
      X_ZNT(reg_A);
    }

    /* Special undocumented operation.  Very similar to CMP. */
    void AXS(Fluint8 x) {
      auto [carry, diff] =
        Fluint8::SubtractWithCarry(reg_A & reg_X, x);
      // uint32 t = (reg_A & reg_X).ToInt() - x.ToInt();
      X_ZN(diff);
      reg_P =
        Fluint8::PlusNoOverflow(
            Fluint8::AndWith<(uint8_t)~C_FLAG8>(reg_P),
            Fluint8::XorWith<C_FLAG8>(
                Fluint8::AndWith<C_FLAG8>(carry)));
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
  inline Fluint8 RdMem(Fluint16 A) {
    return RdMemIf(Fluint8(0x01), A);
  }

  inline Fluint8 RdMemIf(Fluint8 cond, Fluint16 A) {
    const uint8_t cc = cond.ToInt();
    CHECK(cc == 0x00 || cc == 0x01) << cc;
    if (cc == 0x01) {
      uint16_t AA = A.ToInt();
      TraceRead(AA);
      DB = fc->fceu->ARead[AA](fc, AA);
      return Fluint8(DB);
    } else {
      // Arbitrary
      return Fluint8(0x00);
    }
  }

  // normal memory write
  inline void WrMem(Fluint16 A, Fluint8 V) {
    WrMemIf(Fluint8(0x01), A, V);
  }

  inline void WrMemIf(Fluint8 cond, Fluint16 A, Fluint8 V) {
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

  inline Fluint8 RdRAM(Fluint16 A) {
    return RdRAMIf(Fluint8(0x01), A);
  }

  inline Fluint8 RdRAMIf(Fluint8 cond, Fluint16 A) {
    const uint8_t cc = cond.ToInt();
    CHECK(cc == 0x00 || cc == 0x01) << cc;
    if (cc == 0x01) {
      DB = fc->fceu->RAM[A.ToInt()];
      return Fluint8(DB);
    } else {
      // Arbitrary
      return Fluint8(0x00);
    }
  }

  inline void WrRAM(Fluint16 A, Fluint8 V) {
    return WrRAMIf(Fluint8(0x01), A, V);
  }

  inline void WrRAMIf(Fluint8 cond, Fluint16 A, Fluint8 V) {
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
  static constexpr Fluint8 N_FLAG{N_FLAG8};
  static constexpr Fluint8 V_FLAG{V_FLAG8};
  static constexpr Fluint8 U_FLAG{U_FLAG8};
  static constexpr Fluint8 B_FLAG{B_FLAG8};
  static constexpr Fluint8 D_FLAG{D_FLAG8};
  static constexpr Fluint8 I_FLAG{I_FLAG8};
  static constexpr Fluint8 Z_FLAG{Z_FLAG8};
  static constexpr Fluint8 C_FLAG{C_FLAG8};

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
