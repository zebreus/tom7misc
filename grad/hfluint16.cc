
#include "hfluint16.h"

#include <cstdint>

#include "hfluint8.h"

using namespace std;

hfluint16 hfluint16::BitwiseXor(hfluint16 a, hfluint16 b) {
  return hfluint16(a.hi ^ b.hi, a.lo ^ b.lo);
}

hfluint16 hfluint16::BitwiseAnd(hfluint16 a, hfluint16 b) {
  return hfluint16(a.hi & b.hi, a.lo & b.lo);
}

hfluint16 hfluint16::BitwiseOr(hfluint16 a, hfluint16 b) {
  return hfluint16(a.hi | b.hi, a.lo | b.lo);
}

hfluint16 hfluint16::RightShift1(hfluint16 x) {
  // per-component, but put the low bit from hi into the high bit
  // of lo.
  hfluint8 carry = hfluint8::AndWith<0x01>(x.hi);
  return hfluint16(hfluint8::RightShift1(x.hi),
                  hfluint8::LeftShift<7>(carry) |
                  hfluint8::RightShift1(x.lo));
}

hfluint16 hfluint16::LeftShift1(hfluint16 x) {
  hfluint8 carry = hfluint8::AndWith<0x80>(x.lo);
  return hfluint16(
      hfluint8::RightShift<7>(carry) |
      hfluint8::LeftShift<1>(x.hi),
      hfluint8::LeftShift<1>(x.lo));
}

uint16_t hfluint16::ToInt() const {
  uint16_t h = hi.ToInt();
  uint16_t l = lo.ToInt();
  return (h << 8) | l;
}

hfluint16 hfluint16::Plus(hfluint16 a, hfluint16 b) {
  auto [lcarry, lsum] = hfluint8::AddWithCarry(a.lo, b.lo);
  // carry here is ignored
  hfluint8 hsum = a.hi + b.hi + lcarry;
  return hfluint16(hsum, lsum);
}

// Common case that RHS is 8-bit.
hfluint16 hfluint16::Plus(hfluint16 a, hfluint8 b) {
  auto [lcarry, lsum] = hfluint8::AddWithCarry(a.lo, b);
  // carry here is ignored
  hfluint8 hsum = a.hi + lcarry;
  return hfluint16(hsum, lsum);
}

hfluint16 hfluint16::PlusNoByteOverflow(hfluint16 a, hfluint16 b) {
  return hfluint16(hfluint8::PlusNoOverflow(a.hi, b.hi),
                  hfluint8::PlusNoOverflow(a.lo, b.lo));
}

hfluint16 hfluint16::Minus(hfluint16 a, hfluint16 b) {
  auto [lcarry, ldiff] = hfluint8::SubtractWithCarry(a.lo, b.lo);
  // carry here is ignored
  hfluint8 hdiff = a.hi - b.hi - lcarry;
  return hfluint16(hdiff, ldiff);
}

hfluint8 hfluint16::IsZero(hfluint16 a) {
  hfluint8 hz = hfluint8::IsZero(a.hi);
  hfluint8 lz = hfluint8::IsZero(a.lo);
  // Just one bit, so we can use the faster boolean operator.
  return hfluint8::BooleanAnd(hz, lz);
}

hfluint8 hfluint16::IsntZero(hfluint16 a) {
  hfluint8 hz = hfluint8::IsntZero(a.hi);
  hfluint8 lz = hfluint8::IsntZero(a.lo);
  // Just one bit, so we can use the faster boolean operator.
  return hfluint8::BooleanOr(hz, lz);
}

hfluint16 hfluint16::SignExtend(hfluint8 a) {
  // Either 0x80 or 0.
  hfluint8 sign = hfluint8::AndWith<0x80>(a);
  // Spread the bit to all bits.
  // 1100000
  sign |= hfluint8::RightShift<1>(sign);
  // 1111000
  sign = hfluint8::PlusNoOverflow(sign, hfluint8::RightShift<2>(sign));
  // 1111111
  sign = hfluint8::PlusNoOverflow(sign, hfluint8::RightShift<4>(sign));
  CHECK(sign.ToInt() == 0xFF || sign.ToInt() == 0x00);

  return hfluint16(sign, a);
}

hfluint16 hfluint16::If(hfluint8 cc, hfluint16 t) {
  return hfluint16(hfluint8::If(cc, t.Hi()), hfluint8::If(cc, t.Lo()));
}
