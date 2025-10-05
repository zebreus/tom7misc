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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#ifndef _FCEULIB_X6502_H
#define _FCEULIB_X6502_H

#include <cstdint>
#include <bit>
#include <utility>

#include "fceu.h"
#include "fc.h"

#ifdef AOT_INSTRUMENTATION
#include <unordered_map>
#endif

struct X6502 {
  // Initialize with fc pointer, since memory reads/writes
  // trigger callbacks.
  explicit X6502(FC *fc);

  #ifdef GET_INST_HISTO
  int64_t inst_histo[256] = {};
  void ClearInstHisto() {
    for (int i = 0; i < 256; i++) inst_histo[i] = 0;
  }
  #endif

  #ifdef AOT_INSTRUMENTATION
  // Number of times the PC had the corresponding value.
  int64 pc_histo[0x10000] = {};
  // Number of times Run was called with the given number of cycles
  // (prior to multiplication for NTSC/PAL). Last element means that
  // number or greater.
  int64 cycles_histo[1024] = {};

  // int64 entered_aot[0x10000] = {};

  // On instruction (key), how often does the stack pointer have
  // a particular value?
  std::unordered_map<uint16_t, std::unordered_map<uint8_t, int64_t>>
     stack_histo;
  void RecordStack() {
    stack_histo[reg_PC - 1][reg_S]++;
  }
  #else
  void RecordStack() {}
  #endif


  /* Temporary cycle counter */
  int32_t tcount;

  // XXX make sure if you change something you also change its
  // size in state.cc.

  // Program counter.
  uint16_t reg_PC;
  // A is accumulator, X and Y other general purpose registes.
  // S is the stack pointer. Stack grows "down" (push decrements).
  // P is processor flags (from msb to lsb: NVUBDIZC).
  // PI is another copy of the processor flags, maybe having
  // to do with interrupt handling, which I don't understand.
  uint8_t reg_A, reg_X, reg_Y, reg_S, reg_P, reg_PI;
  // I think this is set for some instructions that halt the
  // processor until an interrupt.
  uint8_t jammed;

  uint16_t GetPC() const { return reg_PC; }
  uint8_t GetA() const { return reg_A; }
  uint8_t GetX() const { return reg_X; }
  uint8_t GetY() const { return reg_Y; }
  uint8_t GetS() const { return reg_S; }
  uint8_t GetP() const { return reg_P; }
  uint8_t GetPI() const { return reg_PI; }

  int32_t count;
  /* Simulated IRQ pin held low (or is it high?).
     And other junk hooked on for speed reasons. */
  uint32_t IRQlow;
  /* Data bus "cache" for reads from certain areas */
  uint8_t DB;

  void Run(int32_t cycles);
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
  uint8_t DMR(uint32_t A);
  void DMW(uint32_t A, uint8_t V);

  uint32_t timestamp = 0;

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

 private:
  inline static constexpr uint16_t Make16(uint8_t hi, uint8_t lo) {
    uint16_t ret = hi;
    ret <<= 8;
    ret |= lo;
    return ret;
  }

  inline static constexpr uint8_t High(uint16_t vv) {
    return (vv >> 8) & 0xFF;
  }
  inline static constexpr uint8_t Low(uint16_t vv) {
    return vv & 0xFF;
  }

  inline static constexpr std::pair<uint8_t, uint8_t>
  AddWithCarry(uint8_t a, uint8_t b) {
    uint32_t aa = a, bb = b;
    uint32_t cc = aa + bb;
    return std::make_pair(uint8_t((cc & 0x100) ? 0x01 : 0x00),
                          uint8_t(cc & 0xFF));
  }

  inline static constexpr std::pair<uint8_t, uint8_t>
  SubtractWithCarry(uint8_t a, uint8_t b) {
    uint32_t aa = a, bb = b;
    uint32_t cc = aa - bb;
    return std::make_pair(uint8_t((cc & 0x100) ? 0x01 : 0x00),
                          uint8_t(cc & 0xFF));
  }

  inline void PUSH(uint8_t v) {
    WrRAM(uint16_t(0x100) + reg_S, v);
    reg_S--;
  }

  inline void PUSH16(uint16_t vv) {
    PUSH(High(vv));
    PUSH(Low(vv));
  }

  inline uint8_t POP() {
    reg_S++;
    return RdRAM(uint16_t(0x100) + reg_S);
  }

  inline uint16_t POP16() {
    uint8_t lo = POP();
    uint8_t hi = POP();
    return Make16(hi, lo);
  }

  /* Indexed Indirect */
  uint16_t GetIX() {
    uint8_t tmp = RdMem(reg_PC);
    reg_PC++;
    tmp += reg_X;
    uint8_t lo = RdRAM(uint16_t(tmp));
    tmp++;
    uint8_t hi = RdRAM(uint16_t(tmp));
    return Make16(hi, lo);
  }

  // Zero Page
  uint8_t GetZP() {
    uint8_t ret = RdMem(reg_PC);
    reg_PC++;
    return ret;
  }

  /* Zero Page Indexed */
  uint8_t GetZPI(uint8_t i) {
    uint8_t ret = i + RdMem(reg_PC);
    reg_PC++;
    return ret;
  }

  /* Absolute */
  uint16_t GetAB() {
    uint8_t lo = RdMem(reg_PC);
    reg_PC++;
    uint8_t hi = RdMem(reg_PC);
    reg_PC++;
    return Make16(hi, lo);
  }

  /* Absolute Indexed (for writes and rmws) */
  uint16_t GetABIWR(uint8_t i) {
    uint16_t rt = GetAB();
    uint16_t target = rt + i;
    (void)RdMem((target & uint16_t(0x00FF)) | (rt & uint16_t(0xFF00)));
    return target;
  }

  /* Absolute Indexed (for reads) */
  uint16_t GetABIRD(uint8_t i) {
    uint16_t tmp = GetAB();
    uint16_t ret = tmp + i;
    uint8_t cc = Low(((ret ^ tmp) & uint16_t(0x100)) >> 8);
    (void)RdMemIf(cc, ret ^ uint16_t(0x100));

    ADDCYC(cc);

    return ret;
  }

  /* Indirect Indexed (for reads) */
  uint16_t GetIYRD() {
    uint8_t tmp(RdMem(reg_PC));
    reg_PC++;
    uint8_t lo = RdRAM(uint16_t(tmp));
    uint8_t hi = RdRAM(uint16_t(tmp + uint8_t(1)));
    uint16_t rt = Make16(hi, lo);
    uint16_t ret = rt + reg_Y;
    uint8_t cc =
      Low(((ret ^ rt) & uint16_t(0x100)) >> 8);
    (void)RdMemIf(cc, ret ^ uint16_t(0x100));

    ADDCYC(cc);

    return ret;
  }


  /* Indirect Indexed(for writes and rmws) */
  uint16_t GetIYWR() {
    uint8_t tmp = RdMem(reg_PC);
    reg_PC++;
    uint8_t lo = RdRAM(uint16_t(tmp));
    uint8_t hi = RdRAM(uint16_t(tmp + uint8_t(0x01)));
    uint16_t rt = Make16(hi, lo);
    uint16_t ret = rt + reg_Y;
    (void)RdMem((ret & uint16_t(0x00FF)) | (rt & uint16_t(0xFF00)));
    return ret;
  }

  // Implements the ZNTable (zero flag and negative flag),
  // returning 0, Z_FLAG, or N_FLAG.
  uint8_t ZnFlags(uint8_t zort) {
    static_assert(N_FLAG == 0x80, "This requires the negative flag "
                  "to be the same as the sign bit.");
    uint8_t zf = zort ? 0 : Z_FLAG;
    uint8_t nf = N_FLAG & zort;
    // Can't overflow because these are two different bits.
    return nf | zf;
  }

  void X_ZN(uint8_t zort) {
    reg_P &= ~(Z_FLAG | N_FLAG);
    // We just masked out the bits, so this can't overflow.
    reg_P |= ZnFlags(zort);
  }

  void X_ZNT(uint8_t zort) {
    reg_P |= ZnFlags(zort);
  }

  void LDA(uint8_t x) {
    reg_A = x;
    X_ZN(reg_A);
  }
  void LDX(uint8_t x) {
    reg_X = x;
    X_ZN(reg_X);
  }
  void LDY(uint8_t x) {
    reg_Y = x;
    X_ZN(reg_Y);
  }

  void AND(uint8_t x) {
    reg_A &= x;
    X_ZN(reg_A);
  }

  void BIT(uint8_t x) {
    reg_P &= ~(Z_FLAG | V_FLAG | N_FLAG);
    /* PERF can simplify this ... just use iszero? */
    reg_P |= Z_FLAG & ZnFlags(x & reg_A);
    reg_P |= (V_FLAG | N_FLAG) & x;
  }

  void EOR(uint8_t x) {
    reg_A ^= x;
    X_ZN(reg_A);
  }
  void ORA(uint8_t x) {
    reg_A |= x;
    X_ZN(reg_A);
  }



  void CMPL(uint8_t a1, uint8_t a2) {
    uint32_t t = a1 - a2;
    X_ZN(t & 0xFF);
    reg_P &= ~C_FLAG;
    reg_P |= ((t >> 8) & C_FLAG) ^ C_FLAG;
  }

  // Input must be 0x01 or 0x00.
  void JR(uint8_t cond) {
    if (cond) {
      // Signed displacement. We'll only use it if cond is true.
      int32_t disp = (int8_t)RdMem(reg_PC);
      reg_PC++;
      ADDCYC(1);
      uint32_t tmp = reg_PC;
      reg_PC += disp;
      // Additional cycle is taken if this crosses a "page" boundary.
      if ((tmp ^ reg_PC) & 0x100) {
        ADDCYC(1);
      }
    } else {
      // Program counter incremented whether the branch is
      // taken or not (which makes sense as we would not
      // want to execute the displacement byte!)
      reg_PC++;
    }
  }

  uint8_t ASL(uint8_t x) {
    reg_P &= ~C_FLAG;
    reg_P |= (x >> 7);
    x <<= 1;
    X_ZN(x);
    return x;
  }

  uint8_t LSR(uint8_t x) {
    reg_P &= ~(C_FLAG | N_FLAG | Z_FLAG);
    reg_P |= (x & 1);
    x >>= 1;
    X_ZNT(x);
    return x;
  }

  uint8_t DEC(uint8_t x) {
    x--;
    X_ZN(x);
    return x;
  }

  uint8_t INC(uint8_t x) {
    x++;
    X_ZN(x);
    return x;
  }

  uint8_t ROL(uint8_t x) {
    uint8_t l = x >> 7;
    x <<= 1;
    x |= reg_P & C_FLAG;
    reg_P &= ~(Z_FLAG | N_FLAG | C_FLAG);
    reg_P |= l;
    X_ZNT(x);
    return x;
  }

  uint8_t ROR(uint8_t x) {
    uint8_t l = x & 1;
    x >>= 1;
    x |= (reg_P & C_FLAG) << 7;
    reg_P &= ~(Z_FLAG | N_FLAG | C_FLAG);
    reg_P |= l;
    X_ZNT(x);
    return x;
  }

  void ADDCYC(int x) {
    this->tcount += x;
    this->count -= x * 48;
    timestamp += x;
  }

  template<class F>
  void ST_ZP(F rf) {
    uint16_t AA(GetZP());
    WrRAM(AA, rf(AA));
  }

  template<class F>
  void ST_ZPX(F rf) {
    uint16_t AA(GetZPI(reg_X));
    WrRAM(AA, rf(AA));
  }

  template<class F>
  void ST_ZPY(F rf) {
    uint16_t AA(GetZPI(reg_Y));
    WrRAM(AA, rf(AA));
  }

  template<class F>
  void ST_AB(F rf) {
    uint16_t AA = GetAB();
    WrMem(AA, rf(AA));
  }

  template<class F>
  void ST_ABI(uint8_t reg, F rf) {
    uint16_t AA = GetABIWR(reg);
    WrMem(AA, rf(AA));
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
    uint16_t AA = GetIX();
    WrMem(AA, rf(AA));
  }

  template<class F>
  void ST_IY(F rf) {
    uint16_t AA = GetIYWR();
    WrMem(AA, rf(AA));
  }

  template<class F>
  void LD_IY(F op) {
    const uint16_t AA = GetIYRD();
    const uint8_t x = RdMem(AA);
    op(x);
  }

  template<class F>
  void LD_IX(F op) {
    const uint16_t AA = GetIX();
    const uint8_t x = RdMem(AA);
    op(x);
  }

  template<class F>
  void LD_AB(F op) {
    const uint16_t AA = GetAB();
    [[maybe_unused]] const uint8_t x = RdMem(AA);
    op(x);
  }

  template<class F>
  void LD_ABI(uint8_t reg, F op) {
    const uint16_t AA = GetABIRD(reg);
    [[maybe_unused]] const uint8_t x = RdMem(AA);
    op(x);
  }
  template<class F> void LD_ABX(F op) { LD_ABI(reg_X, op); }
  template<class F> void LD_ABY(F op) { LD_ABI(reg_Y, op); }

  template<class F>
  void LD_ZPY(F op) {
    const uint16_t AA(GetZPI(reg_Y));
    const uint8_t x = RdRAM(AA);
    op(x);
  }

  template<class F>
  void LD_ZPX(F op) {
    const uint16_t AA(GetZPI(reg_X));
    const uint8_t x = RdRAM(AA);
    op(x);
  }

  template<class F>
  void LD_ZP(F op) {
    const uint16_t AA(GetZP());
    const uint8_t x = RdRAM(AA);
    op(x);
  }

  template<class F>
  void LD_IM(F op) {
    const uint8_t x = RdMem(reg_PC);
    reg_PC++;
    op(x);
  }

  template<class F>
  void RMW_ZPX(F op) {
    const uint16_t AA(GetZPI(reg_X));
    uint8_t x = RdRAM(AA);
    x = op(x);
    WrRAM(AA, x);
  }

  template<class F>
  void RMW_ZP(F op) {
    const uint16_t AA(GetZP());
    uint8_t x = RdRAM(AA);
    x = op(x);
    WrRAM(AA, x);
  }

  template<class F>
  void RMW_IY(F op) {
    const uint16_t AA = GetIYWR();
    uint8_t x = RdMem(AA);
    // Emulate the RMW dummy write cycle. Writing the original value
    // back to the address can trigger side effects on memory-mapped
    // hardware registers.
    WrMem(AA, x);
    x = op(x);
    WrMem(AA, x);
  }

  template<class F>
  void RMW_IX(F op) {
    const uint16_t AA = GetIX();
    uint8_t x = RdMem(AA);
    // Dummy write, as above.
    WrMem(AA, x);
    x = op(x);
    WrMem(AA, x);
  }

  template<class F>
  void RMW_ABI(uint8_t reg, F op) {
    const uint16_t AA = GetABIWR(reg);
    uint8_t x = RdMem(AA);
    // Dummy write, as above.
    WrMem(AA, x);
    x = op(x);
    WrMem(AA, x);
  }

  template<class F> void RMW_ABX(F op) { RMW_ABI(reg_X, op); }
  template<class F> void RMW_ABY(F op) { RMW_ABI(reg_Y, op); }

  template<class F>
  void RMW_AB(F op) {
    const uint16_t AA = GetAB();
    uint8_t x = RdMem(AA);
    // Dummy write, as above.
    WrMem(AA, x);
    x = op(x);
    WrMem(AA, x);
  }

  template<class F>
  void RMW_A(F op) {
    uint8_t x = reg_A;
    x = op(x);
    reg_A = x;
  }

  uint8_t ADC(uint8_t x) {
    static_assert(C_FLAG == 0x01, "we assume this is the one's place");
    const uint8_t p_carry_bit = C_FLAG & reg_P;
    auto [carry1, sum1] = AddWithCarry(reg_A, x);
    auto [carry2, sum] = AddWithCarry(sum1, p_carry_bit);

    // Since p_carry_bit is at most 1, these can't both overflow.
    uint8_t carry = carry1 + carry2;

    // uint32_t l = reg_A.ToInt() + (x).ToInt() + p_carry_bit.ToInt();
    reg_P &= ~(Z_FLAG | C_FLAG | N_FLAG | V_FLAG);
    // The overflow is for signed arithmetic. It tells us if we've
    // added two positive numbers but got a negative one, or added two
    // negative numbers but got a positive one. (If the signs are
    // different, overflow is not possible.) This is computed from the
    // sign bits.
    uint8_t aaa = 0x80 ^ (0x80 & (reg_A ^ x));
    uint8_t bbb = 0x80 & (reg_A ^ sum);
    static_assert(V_FLAG == 0x40);

    // CHECK((reg_P & (V_FLAG | C_FLAG)) == 0);
    // Sets overflow bit, which was cleared above.
    reg_P = reg_P + ((aaa & bbb) >> 1);
    // Sets carry bit, which was cleared above.
    reg_P = reg_P + carry;
    reg_A = sum;
    // PERF since we already cleared Z and N flags, can use
    // PlusNoOverflow
    X_ZNT(reg_A);
    return x;
  }

  uint8_t SBC(uint8_t x) {
    static_assert(C_FLAG == 0x01, "we assume this is the one's place");
    // On 6502, the borrow flag is !Carry.
    uint8_t p_ncarry_bit = C_FLAG ^ (reg_P & C_FLAG);

    auto [carry1, diff1] = SubtractWithCarry(reg_A, x);
    auto [carry2, diff] = SubtractWithCarry(diff1, p_ncarry_bit);

    // As in ADC.
    uint8_t carry = carry1 + carry2;

    // uint32_t l = reg_A.ToInt() - x.ToInt() - p_ncarry_bit.ToInt();
    reg_P &= ~(Z_FLAG | C_FLAG | N_FLAG | V_FLAG);
    // As above, detect overflow by looking at sign bits.
    uint8_t aaa = reg_A ^ diff;
    uint8_t bbb = reg_A ^ x;
    uint8_t overflow = 0x80 & (aaa & bbb);
    static_assert(V_FLAG == 0x40);

    // CHECK((reg_P & (V_FLAG | C_FLAG)) == 0);
    // V_FLAG bit is cleared above.
    reg_P += overflow >> 1;
    // C_FLAG bit is cleared above.
    reg_P += C_FLAG ^ (C_FLAG & carry);
    reg_A = diff;
    // PERF since we already cleared Z and N flags, can use
    // PlusNoOverflow here too
    X_ZNT(reg_A);
    return x;
  }

  void LSRA() {
    /* For undocumented instructions, maybe for other things later... */
    reg_P &= ~(C_FLAG | N_FLAG | Z_FLAG);
    reg_P |= (reg_A & 1);
    reg_A = (reg_A >> 1);
    X_ZNT(reg_A);
  }

  /* Special undocumented operation.  Very similar to CMP. */
  void AXS(uint8_t x) {
    uint32_t t = (reg_A & reg_X) - x;
    X_ZN(t & 0xFF);
    reg_P &= ~C_FLAG;
    reg_P |= ((t >> 8) & C_FLAG) ^ C_FLAG;
    reg_X = t;
  }

  // normal memory read, which calls hooks. We trace the sequence
  // of these to make sure we're not dropping or reordering them
  // (which often does not matter to games, but could).
  inline uint8_t RdMem(uint16_t A) {
    return RdMemIf(uint8_t(0x01), A);
  }

  inline uint8_t RdMemIf(uint8_t cc, uint16_t AA) {
    // CHECK(cc == 0x00 || cc == 0x01) << cc;
    if (cc) {
      TraceRead(AA);
      return uint8_t(DB = fc->fceu->ARead[AA](fc, AA));
    } else {
      // Arbitrary
      return uint8_t(0x00);
    }
  }

  // normal memory write
  inline void WrMem(uint16_t AA, uint8_t VV) {
    TraceWrite(AA, VV);
    fc->fceu->BWrite[AA](fc, AA, VV);
  }

  inline uint8_t RdRAM(uint16_t AA) {
    return uint8_t(DB = fc->fceu->RAM[AA]);
  }

  inline void WrRAM(uint16_t AA, uint8_t V) {
    fc->fceu->RAM[AA] = V;
  }

  // Processor flags.
  static constexpr uint8_t N_FLAG{0x80};
  static constexpr uint8_t V_FLAG{0x40};
  static constexpr uint8_t U_FLAG{0x20};
  static constexpr uint8_t B_FLAG{0x10};
  static constexpr uint8_t D_FLAG{0x08};
  static constexpr uint8_t I_FLAG{0x04};
  static constexpr uint8_t Z_FLAG{0x02};
  static constexpr uint8_t C_FLAG{0x01};

  FC *fc = nullptr;
 private:
  X6502() = delete;
  X6502(const X6502 &other) = delete;
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
