#include "fluint8.h"

#include <cstdint>

#include "half.h"

using namespace half_float;
using namespace half_float::literal;

int64_t Fluint8::num_cheats = 0;

#define ECHECK(cond) CHECK(true || (cond))
// #define ECHECK(cond) CHECK(cond)

#if !FLUINT8_WRAP

uint16_t Fluint8::Representation() const { return GetU16(h); }

uint8_t Fluint8::ToInt() const {
  return (int)h;
}

// For functions of multiple variables, we allow some linear
// functions of these. Note we should be careful when multiplying,
// since e.g. x * x is not linear!
std::pair<Fluint8, Fluint8> Fluint8::AddWithCarry(Fluint8 x, Fluint8 y) {
  // Correct value, but maybe 256 too high because of overflow.
  const half z = x.h + y.h;

  // Shift down 8 times to get the overflow bit.
  half o = RightShiftHalf8(z);

  ECHECK(o == 1.0_h || o == 0.0_h) << o;
  return make_pair(Fluint8(o), Fluint8(z - o * 256.0_h));
}

Fluint8 Fluint8::Plus(Fluint8 x, Fluint8 y) {
  // As above but don't compute carry.
  const half z = x.h + y.h;
  half o = RightShiftHalf8(z);
  return Fluint8(z - o * 256.0_h);
}

std::pair<Fluint8, Fluint8> Fluint8::SubtractWithCarry(Fluint8 x, Fluint8 y) {
  const half z = x.h - y.h + 256.0_h;

  // Shift down 8 times to get the overflow bit.
  half o = RightShiftHalf8(z);

  ECHECK(o == 1.0_h || o == 0.0_h) << o;
  return make_pair(Fluint8(1.0_h - o), Fluint8(z - o * 256.0_h));
}

Fluint8 Fluint8::Minus(Fluint8 x, Fluint8 y) {
  // Same but don't compute carry.
  const half z = x.h - y.h + 256.0_h;
  half o = RightShiftHalf8(z);
  return Fluint8(z - o * 256.0_h);
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
  half common = (half)0.0f;
  Fluint8 aa = a, bb = b;
  for (int bit_idx = 0; bit_idx < 8; bit_idx++) {
    // Low order bit as a - ((a >> 1) << 1)
    Fluint8 aashift = RightShift1(aa);
    Fluint8 bbshift = RightShift1(bb);
    half a_bit = aa.h - LeftShift1Under128(aashift).h;
    half b_bit = bb.h - LeftShift1Under128(bbshift).h;
    const half scale = (half)(1 << bit_idx);


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

#if 0
Fluint8 Fluint8::IsntZero(Fluint8 a) {
  // A simple way to do this is to extract all the (negated) bits
  // and multiply them together. But this would mean multiplying
  // a by itself, and so is not linear.
  //
  // Instead, do the same but ADD the bits. Now we have a
  // number in [0, 8]. So now we can do the whole thing again
  // and get a number in [0, 4], etc.

  half num_ones = (half)0.0f;
  Fluint8 aa = a;
  for (int bit_idx = 0; bit_idx < 8; bit_idx++) {
    Fluint8 aashift = RightShift1(aa);
    half a_bit = aa.h - LeftShift1Under128(aashift).h;
    num_ones += a_bit;
    aa = aashift;
  }

  ECHECK(num_ones >= (half)0.0f && num_ones <= (half)8.0f);

  // now count the ones in num_ones.
  aa = Fluint8(num_ones);
  num_ones = (half)0.0f;
  for (int bit_idx = 0; bit_idx < 4; bit_idx++) {
    Fluint8 aashift = RightShift1(aa);
    half a_bit = aa.h - LeftShift1Under128(aashift).h;
    num_ones += a_bit;
    aa = aashift;
  }

  ECHECK(num_ones >= (half)0.0f && num_ones <= (half)4.0f);

  // and again ...
  aa = Fluint8(num_ones);
  num_ones = (half)0.0f;
  for (int bit_idx = 0; bit_idx < 3; bit_idx++) {
    Fluint8 aashift = RightShift1(aa);
    half a_bit = aa.h - LeftShift1Under128(aashift).h;
    num_ones += a_bit;
    aa = aashift;
  }

  ECHECK(num_ones >= (half)0.0f && num_ones <= (half)3.0f);

  // and again ...
  aa = Fluint8(num_ones);
  num_ones = (half)0.0f;
  for (int bit_idx = 0; bit_idx < 2; bit_idx++) {
    Fluint8 aashift = RightShift1(aa);
    half a_bit = aa.h - LeftShift1Under128(aashift).h;
    num_ones += a_bit;
    aa = aashift;
  }

  ECHECK(num_ones >= (half)0.0f && num_ones <= (half)2.0f);

  // Now num_ones is either 0, 1, or 2. Since 1 and 2 is each
  // represented with a single bit, we can collapse them with
  // a shift and add:
  //   num_ones    output
  //         00         0
  //         01         1
  //         10         1
  Fluint8 nn(num_ones);
  return Fluint8(nn.h + RightShift1(nn).h);
}

Fluint8 Fluint8::IsZero(Fluint8 a) {
  return Fluint8(1.0_h - IsntZero(a).h);
}

#endif

#if 0
// This is worse!

Fluint8 Fluint8::IsntZero(Fluint8 a) {
  return Fluint8(1.0_h - IsZero(a).h);
}

Fluint8 Fluint8::IsZero(Fluint8 a) {
  // We know IsZero returns 1 or 0.
  // return Fluint8(1.0_h - IsntZero(a).h);

  Fluint8 aa = a;
  // true if everything is 1.0 so far
  half res = 1.0_h;
  for (int bit_idx = 0; bit_idx < 8; bit_idx++) {
    // leftmost bit
    half bit = RightShift<7>(aa).h;
    half nbit = (1.0_h - bit);
    // res = res & ~bit
    res = RightShiftHalf1(nbit + res);
    // aa = LeftShift<1>(aa);
    // We already have the high bit, so mask it off if necessary
    aa = Fluint8(aa.h - bit * 128.0_h);
    aa = LeftShift1Under128(aa);
  }

  return Fluint8(res);
}

#endif

Fluint8 Fluint8::IsntZero(Fluint8 a) {
  // PERF: Might be shorter version of 0F step.
  // PERF: Instead of multiplying in FF step, maybe could
  // just be subtraction fused with first addition of 0F step.
  return Fluint8(CompressHalf0F(CompressHalfFF(a.h)));
}

Fluint8 Fluint8::IsZero(Fluint8 a) {
  return Fluint8(1.0_h - IsntZero(a).h);
}


Fluint8 Fluint8::Eq(Fluint8 a, Fluint8 b) {
  return IsZero(a - b);
}

// For cc = 0x01 or 0x00 (only), returns c ? t : 0.
Fluint8 Fluint8::If(Fluint8 cc, Fluint8 t) {
  // Could do this by spreading the cc to 0xFF or 0x00 and
  // using AND, but it is faster to just keep consulting the
  // ones place of cc.

  half kept = (half)0.0f;
  Fluint8 tt = t;
  for (int bit_idx = 0; bit_idx < 8; bit_idx++) {
    // Low order bit as a - ((a >> 1) << 1)
    Fluint8 ttshift = RightShift1(tt);
    half t_bit = tt.h - LeftShift1Under128(ttshift).h;
    const half scale = (half)(1 << bit_idx);

    const half and_bits = RightShiftHalf1(t_bit + cc.h);
    kept += scale * and_bits;

    // and keep shifting down
    tt = ttshift;
  }

  return Fluint8(kept);
}

// For cc = 0x01 or 0x00 (only), returns c ? t : f.
Fluint8 Fluint8::IfElse(Fluint8 cc, Fluint8 t, Fluint8 f) {
  // PERF probably can be faster to do this directly?
  return PlusNoOverflow(If(cc, t), If(XorWith<0x01>(cc), f));
}


#endif
