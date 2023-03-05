#include "fluint8.h"

#include <cstdint>

#include "half.h"

using namespace half_float;
using namespace half_float::literal;

int64_t Fluint8::num_cheats = 0;

#define ECHECK(cond) CHECK(true || (cond))
// #define ECHECK(cond) CHECK(cond)

#if !FLUINT8_WRAP

template<std::size_t N, class F, std::size_t START = 0>
inline void Repeat(const F &f) {
  if constexpr (N == 0) {
    return;
  } else {
    f(START);
    Repeat<N - 1, F, START + 1>(f);
  }
}

uint16_t Fluint8::Representation() const { return GetU16(h); }

uint8_t Fluint8::ToInt() const {
  return (int)h;
}

// For functions of multiple variables, we allow some linear
// functions of these. Note we should be careful when multiplying,
// since e.g. x * x is not linear!
std::pair<Fluint8, Fluint8> Fluint8::AddWithCarry(Fluint8 x, Fluint8 y) {
  static constexpr half HALF256 = GetHalf(0x5c00);

  // Correct value, but maybe 256 too high because of overflow.
  const half z = x.h + y.h;

  // Shift down 8 times to get the overflow bit.
  half o = RightShiftHalf8(z);

  ECHECK(o == 1.0_h || o == 0.0_h) << o;
  return make_pair(Fluint8(o), Fluint8(z - o * HALF256));
}

Fluint8 Fluint8::Plus(Fluint8 x, Fluint8 y) {
  static constexpr half HALF256 = GetHalf(0x5c00);

  // As above but don't compute carry.
  const half z = x.h + y.h;
  half o = RightShiftHalf8(z);
  return Fluint8(z - o * HALF256);
}

std::pair<Fluint8, Fluint8> Fluint8::SubtractWithCarry(Fluint8 x, Fluint8 y) {
  static constexpr half HALF256 = GetHalf(0x5c00);
  static constexpr half HALF1 = GetHalf(0x3c00);

  const half z = x.h - y.h + HALF256;

  // Shift down 8 times to get the overflow bit.
  half o = RightShiftHalf8(z);

  ECHECK(o == 1.0_h || o == 0.0_h) << o;
  return make_pair(Fluint8(HALF1 - o), Fluint8(z - o * HALF256));
}

Fluint8 Fluint8::Minus(Fluint8 x, Fluint8 y) {
  static constexpr half HALF256 = GetHalf(0x5c00);
  // Same but don't compute carry.
  const half z = x.h - y.h + HALF256;
  half o = RightShiftHalf8(z);
  return Fluint8(z - o * HALF256);
}

// This only works when in canonical form, but it's much
// faster than the old approach of using Canonicalize!
Fluint8 Fluint8::RightShift1(Fluint8 x) {
  return Fluint8(RightShiftHalf1(x.h));
}

/* This comment is a memorial to a great joke which is no
   longer necessary:

  // Get to the chopa
  const half chopa = a.ToChoppy();
*/

// Returns the bits (as an integral half in [0, 255]) that are in
// common between the args, i.e. a & b.
half Fluint8::GetCommonBits(Fluint8 a, Fluint8 b) {
  // Note: This can be computed as
  // const half scale = GetHalf(0x3c00 + 0x400 * bit_idx);
  static constexpr std::array<half, 8> HALF_POW2 = {
    GetHalf(0x3c00),  // 1.0
    GetHalf(0x4000),  // 2.0
    GetHalf(0x4400),  // 4.0
    GetHalf(0x4800),  // 8.0
    GetHalf(0x4c00),  // 16.0
    GetHalf(0x5000),  // 32.0
    GetHalf(0x5400),  // 64.0
    GetHalf(0x5800),  // 128.0
  };
  half common = GetHalf(0x0000);
  Fluint8 aa = a, bb = b;
  for (int bit_idx = 0; bit_idx < 8; bit_idx++) {
    // Low order bit as a - ((a >> 1) << 1)
    Fluint8 aashift = RightShift1(aa);
    Fluint8 bbshift = RightShift1(bb);
    half a_bit = aa.h - LeftShift1Under128(aashift).h;
    half b_bit = bb.h - LeftShift1Under128(bbshift).h;
    const half scale = HALF_POW2[bit_idx];
    // const half scale = GetHalf(0x3c00 + 0x400 * bit_idx);

    // Multiplication is like AND.
    // But note that if we tried to do GetCommonBits(z, z) here,
    // we would end up multiplying some function of z by some
    // function of z, which is not linear. So instead of
    // multiplying, we compute "a & b" as "(a + b) >> 1".
    // const half and_bits = (a_bit * b_bit);
    const half and_bits = RightShiftHalf1(a_bit + b_bit);
    // (Whereas scale is just a constant...)
    common += scale * and_bits;

    // and keep shifting down
    aa = aashift;
    bb = bbshift;
  }

  return common;
}

Fluint8 Fluint8::BitwiseAnd(Fluint8 a, Fluint8 b) {
  return Fluint8(GetCommonBits(a, b));
}

Fluint8 Fluint8::BitwiseOr(Fluint8 a, Fluint8 b) {
  half common_bits = GetCommonBits(a, b);
  // Neither the subtraction nor addition can overflow here, so
  // we can just do this directly without corrections.
  half result = (a.h - common_bits) + b.h;

  return Fluint8(result);
}

Fluint8 Fluint8::BitwiseXor(Fluint8 a, Fluint8 b) {
  half common_bits = GetCommonBits(a, b);
  // XOR is just the bits that the two do NOT have in common.
  half result = (a.h - common_bits) + (b.h - common_bits);
  return Fluint8(result);
}


Fluint8 Fluint8::IsntZero(Fluint8 a) {
  // PERF: Might be shorter version of 0F step.
  // PERF: Instead of multiplying in FF step, maybe could
  // just be subtraction fused with first addition of 0F step.
  return Fluint8(CompressHalf0F(CompressHalfFF(a.h)));
}

Fluint8 Fluint8::IsZero(Fluint8 a) {
  static constexpr half HALF1 = GetHalf(0x3c00);
  return Fluint8(HALF1 - IsntZero(a).h);
}


Fluint8 Fluint8::Eq(Fluint8 a, Fluint8 b) {
  return IsZero(a - b);
}

// New approach is much more mysterious, but twice as fast as
// the previous. The trick centers around some constants,
// that when added and subtracted, reduce the number of distinct
// values until everything becomes zero. But we premultiply
// those constants by (1 - cc), so (and this requires cc to be
// exactly 0x00 or 0x01) the values added are actually zero
// in the case that the condition is true. So this either adds
// a bunch of zeroes (and leaves the value as-is) or adds magic
// constants that causes the result to be zero.
//
// I was only able to solve this for values <= 0x80, so we
// actually do that, then flip the values around with (128 - x),
// then do it again.
Fluint8 Fluint8::If(Fluint8 cc, Fluint8 t) {
  // PERF: We can probably do this in fewer than 8 steps?
  static constexpr std::array<half, 8> OFF = {
    GetHalf(0x77f9),
    GetHalf(0x7829),
    GetHalf(0x77fb),
    GetHalf(0x78e2),
    GetHalf(0x77fd),
    GetHalf(0x780b),
    GetHalf(0x77ff),
    GetHalf(0x7864),

    // old way, 9 steps
    /*
    GetHalf(0x780e), GetHalf(0x77fd), GetHalf(0x79f9),
    GetHalf(0x77fb), GetHalf(0x795c), GetHalf(0x77fd),
    GetHalf(0x7a33), GetHalf(0x77ff), GetHalf(0x7800),
    */
  };

  static constexpr half HALF1 = GetHalf(0x3c00);
  static constexpr half HALF128 = GetHalf(0x5800);
  static constexpr half HALFNEG1 = GetHalf(0xbc00);
  static constexpr half HALF0 = GetHalf(0x0000);

  half xh = t.h;
  const half nch = HALF1 - cc.h;
  const half c128 = HALF128 * nch;

  std::array<half, OFF.size()> COFF;
  for (int i = 0; i < (int)OFF.size(); i++) {
    // This results in 0 or the constant.
    COFF[i] = OFF[i] * nch;
  }

  // Now do the dance
  for (const half &h : COFF) {
    xh += h;
    xh -= h;
  }

  xh = (c128 - xh);

  for (const half &h : COFF) {
    xh += h;
    xh -= h;
  }

  // 128 - x above was 0 - x in the true case, which
  // negates the value; so flip the sign here. We need
  // to add zero to avoid outputing -0.
  xh = xh * HALFNEG1 + HALF0;

  return Fluint8(xh);
}

// For cc = 0x01 or 0x00 (only), returns c ? t : f.
Fluint8 Fluint8::IfElse(Fluint8 cc, Fluint8 t, Fluint8 f) {
  // PERF probably can be faster to do this directly?
  // PERF Could have BooleanNot
  return PlusNoOverflow(If(cc, t), If(XorWith<0x01>(cc), f));
}

Fluint8 Fluint8::BooleanAnd(Fluint8 a, Fluint8 b) {
  return RightShift1(PlusNoOverflow(a, b));
}

#endif
