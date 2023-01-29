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

#ifndef __X6502_H
#define __X6502_H

#include "tracing.h"
#include "fceu.h"
#include "fc.h"

#include "fluint8.h"

// using fluint8 = uint8;


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

  /* I'll change this to uint32 later...
     I'll need to AND PC after increments to 0xFFFF
     when I do, though.  Perhaps an IPC() macro? */

  // XXX make sure if you change something you also change its
  // size in state.cc.

  // Program counter.
  // XXX this is part of processor state so it should be
  // fluint16?
  uint16 reg_PC;
  // A is accumulator, X and Y other general purpose registes.
  // S is the stack pointer. Stack grows "down" (push decrements).
  // P is processor flags (from msb to lsb: NVUBDIZC).
  // PI is another copy of the processor flags, maybe having
  // to do with interrupt handling, which I don't understand.
  Fluint8 reg_A, reg_X, reg_Y, reg_S, reg_P, reg_PI;
  // I think this is set for some instructions that halt the
  // processor until an interrupt.
  uint8 jammed;

  uint8 GetA() const { return reg_A.ToInt(); }
  uint8 GetX() const { return reg_X.ToInt(); }
  uint8 GetY() const { return reg_Y.ToInt(); }
  uint8 GetS() const { return reg_S.ToInt(); }
  uint8 GetP() const { return reg_P.ToInt(); }
  uint8 GetPI() const { return reg_PI.ToInt(); }

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
  // but consumes a CPU cycle.
  uint8 DMR(uint32 A);
  void DMW(uint32 A, uint8 V);

  uint32 timestamp = 0;

  void (*MapIRQHook)(FC *, int) = nullptr;

private:
  inline void PUSH(Fluint8 v) {
    WrRAM(0x100 + reg_S.ToInt(), v.ToInt());
    reg_S--;
  }

  inline void PUSH16(uint16_t vv) {
    WrRAM(0x100 + reg_S.ToInt(), (vv >> 8) & 0xFF);
    reg_S--;
    WrRAM(0x100 + reg_S.ToInt(), vv & 0xFF);
    reg_S--;
  }

  inline uint8 POP() {
    return RdRAM(0x100 + (++reg_S).ToInt());
  }

  inline uint16_t POP16() {
    uint16 ret = POP();
    ret |= ((uint16)POP()) << 8;
    return ret;
  }

  /* Indexed Indirect */
  uint16_t GetIX() {
    Fluint8 tmp(RdMem(reg_PC));
    reg_PC++;
    tmp += reg_X;
    uint16 ret = RdRAM(tmp.ToInt());
    tmp++;
    ret |= ((uint16)RdRAM(tmp.ToInt())) << 8;
    return ret;
  }

  // Zero Page
  Fluint8 GetZP() {
    Fluint8 ret(RdMem(reg_PC));
    reg_PC++;
    return ret;
  }

  /* Zero Page Indexed */
  Fluint8 GetZPI(Fluint8 i) {
    Fluint8 ret = i + Fluint8(RdMem(reg_PC));
    reg_PC++;
    return ret;
  }

  /* Absolute */
  uint16_t GetAB() {
    uint16_t ret = RdMem(reg_PC);
    reg_PC++;
    ret |= RdMem(reg_PC) << 8;
    reg_PC++;
    return ret;
  }

  /* Absolute Indexed(for writes and rmws) */
  uint16_t GetABIWR(Fluint8 i) {
    uint16_t rt = GetAB();
    uint16_t target = rt;
    target += i.ToInt();
    target &= 0xFFFF;
    (void)RdMem((target & 0x00FF) | (rt & 0xFF00));
    return target;
  }

  /* Absolute Indexed(for reads) */
  uint16_t GetABIRD(Fluint8 i) {
    uint16 tmp = GetAB();
    uint16 ret = tmp;
    ret += i.ToInt();
    if ((ret ^ tmp) & 0x100) {
      ret &= 0xFFFF;
      RdMem(ret ^ 0x100);
      ADDCYC(1);
    }
    return ret;
  }

  /* Indirect Indexed(for reads) */
  uint16_t GetIYRD() {
    uint8 tmp = RdMem(reg_PC);
    reg_PC++;
    uint16_t rt = RdRAM(tmp);
    tmp++;
    rt |= ((uint16_t)RdRAM(tmp)) << 8;
    uint16 ret = rt;
    ret += reg_Y.ToInt();
    if ((ret ^ rt) & 0x100) {
      ret &= 0xFFFF;
      (void)RdMem(ret ^ 0x100);
      ADDCYC(1);
    }
    return ret;
  }


  /* Indirect Indexed(for writes and rmws) */
  uint16_t GetIYWR() {
    uint8 tmp = RdMem(reg_PC);
    reg_PC++;
    uint16 rt = RdRAM(tmp);
    tmp++;
    rt |= ((uint16)RdRAM(tmp)) << 8;
    uint16 ret = rt;
    ret += reg_Y.ToInt();
    ret &= 0xFFFF;
    (void)RdMem((ret & 0x00FF) | (rt & 0xFF00));
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

  void CMPL(Fluint8 a1, Fluint8 a2) {
    // TODO: Can use subtract with carry?
    Fluint8::Cheat();
    uint32 t = a1.ToInt() - a2.ToInt();
    X_ZN(Fluint8(t));
    reg_P =
      Fluint8::PlusNoOverflow(
          Fluint8::AndWith<(uint8_t)~C_FLAG8>(reg_P),
          Fluint8::XorWith<C_FLAG8>(
              Fluint8::AndWith<C_FLAG8>(Fluint8((uint8)(t >> 8)))));
  }

  // Input should be 1 or 0.
  void JR(Fluint8 cond) {
    {
      uint8_t cc = cond.ToInt();
      CHECK(cc == 0 || cc == 1) << cc;
    }
    Fluint8::Cheat();
    if (cond.ToInt()) {
      int32 disp = (int8)RdMem(reg_PC);
      reg_PC++;
      ADDCYC(1);
      uint32 tmp = reg_PC;
      reg_PC += disp;
      if ((tmp ^ reg_PC) & 0x100) {
        ADDCYC(1);
      }
    } else {
      reg_PC++;
    }
  }

  void ADDCYC(int x) {
    this->tcount += x;
    this->count -= x * 48;
    timestamp += x;
  }

  template<class F>
  void ST_ZP(F rf) {
    uint16_t AA = GetZP().ToInt();
    WrRAM(AA, rf(AA).ToInt());
  }

  template<class F>
  void ST_ZPX(F rf) {
    uint16_t AA = GetZPI(reg_X).ToInt();
    WrRAM(AA, rf(AA).ToInt());
  }

  template<class F>
  void ST_ZPY(F rf) {
    uint16_t AA = GetZPI(reg_Y).ToInt();
    WrRAM(AA, rf(AA).ToInt());
  }

  template<class F>
  void ST_AB(F rf) {
    uint16 AA = GetAB();
    WrMem(AA, rf(AA).ToInt());
  }

  template<class F>
  void ST_ABI(Fluint8 reg, F rf) {
    uint16 AA = GetABIWR(reg);
    WrMem(AA, rf(AA).ToInt());
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
    uint16 AA = GetIX();
    WrMem(AA, rf(AA).ToInt());
  }

  template<class F>
  void ST_IY(F rf) {
    uint16 AA = GetIYWR();
    WrMem(AA, rf(AA).ToInt());
  }

  void ADC(Fluint8 x) {
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
    reg_P |= Fluint8::RightShift<1>(aaa & bbb);

    // PERF: Cleared above, so we can use PlusNoOverflow?
    reg_P |= carry;
    reg_A = sum;
    // PERF since we already cleared Z and N flags, can use
    // PlusNoOverflow
    X_ZNT(reg_A);
  }

  void SBC(Fluint8 x) {
    // TODO: Implement subtract with carry natively
    // On 6502, the borrow flag is !Carry.
    Fluint8 p_ncarry_bit = Fluint8::XorWith<C_FLAG8>(
        Fluint8::AndWith<C_FLAG8>(reg_P));
    Fluint8::Cheat();
    uint32 l = reg_A.ToInt() - x.ToInt() - p_ncarry_bit.ToInt();
    reg_P = Fluint8::AndWith<(uint8_t)~(Z_FLAG8 | C_FLAG8 | N_FLAG8 | V_FLAG8)>(reg_P);
    // As above, detect overflow by looking at sign bits.
    Fluint8 aaa = Fluint8::AndWith<0x80>(
        (reg_A ^ Fluint8((uint8)l)) & (reg_A ^ x));
    static_assert(V_FLAG8 == 0x40);
    // PERF Cleared above; can PlusNoOverflow
    reg_P |= Fluint8::RightShift<1>(aaa);
    // PERF here too
    reg_P |= Fluint8::XorWith<C_FLAG8>(Fluint8::AndWith<C_FLAG8>(Fluint8((uint8)(l >> 8))));
    reg_A = Fluint8((uint8)l);
    X_ZNT(reg_A);
  }


  // normal memory read
  inline uint8 RdMem(unsigned int A) {
    return DB = fc->fceu->ARead[A](fc, A);
  }

  // normal memory write
  inline void WrMem(unsigned int A, uint8 V) {
    fc->fceu->BWrite[A](fc, A, V);
  }

  inline uint8 RdRAM(unsigned int A) {
    return (DB = fc->fceu->RAM[A]);
  }

  inline void WrRAM(unsigned int A, uint8 V) {
    fc->fceu->RAM[A] = V;
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
