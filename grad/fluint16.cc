
#include "fluint16.h"

#include <cstdint>

#include "fluint8.h"

using namespace std;

Fluint16 Fluint16::BitwiseXor(Fluint16 a, Fluint16 b) {
  return Fluint16(a.hi ^ b.hi, a.lo ^ b.lo);
}

Fluint16 Fluint16::BitwiseAnd(Fluint16 a, Fluint16 b) {
  return Fluint16(a.hi & b.hi, a.lo & b.lo);
}

Fluint16 Fluint16::BitwiseOr(Fluint16 a, Fluint16 b) {
  return Fluint16(a.hi | b.hi, a.lo | b.lo);
}

Fluint16 Fluint16::RightShift1(Fluint16 x) {
  // per-component, but put the low bit from hi into the high bit
  // of lo.
  Fluint8 carry = Fluint8::AndWith<0x01>(x.hi);
  return Fluint16(Fluint8::RightShift1(x.hi),
                  Fluint8::LeftShift<7>(carry) |
                  Fluint8::RightShift1(x.lo));
}

Fluint16 Fluint16::LeftShift1(Fluint16 x) {
  Fluint8 carry = Fluint8::AndWith<0x80>(x.lo);
  return Fluint16(
      Fluint8::RightShift<7>(carry) |
      Fluint8::LeftShift<1>(x.hi),
      Fluint8::LeftShift<1>(x.lo));
}

uint16_t Fluint16::ToInt() const {
  uint16_t h = hi.ToInt();
  uint16_t l = lo.ToInt();
  return (h << 8) | l;
}

Fluint16 Fluint16::Plus(Fluint16 a, Fluint16 b) {
  auto [lcarry, lsum] = Fluint8::AddWithCarry(a.lo, b.lo);
  // carry here is ignored
  Fluint8 hsum = a.hi + b.hi + lcarry;
  return Fluint16(hsum, lsum);
}

// Common case that RHS is 8-bit.
Fluint16 Fluint16::Plus(Fluint16 a, Fluint8 b) {
  auto [lcarry, lsum] = Fluint8::AddWithCarry(a.lo, b);
  // carry here is ignored
  Fluint8 hsum = a.hi + lcarry;
  return Fluint16(hsum, lsum);
}

Fluint16 Fluint16::PlusNoByteOverflow(Fluint16 a, Fluint16 b) {
  return Fluint16(Fluint8::PlusNoOverflow(a.hi, b.hi),
                  Fluint8::PlusNoOverflow(a.lo, b.lo));
}

Fluint16 Fluint16::Minus(Fluint16 a, Fluint16 b) {
  auto [lcarry, ldiff] = Fluint8::SubtractWithCarry(a.lo, b.lo);
  // carry here is ignored
  Fluint8 hdiff = a.hi - b.hi - lcarry;
  return Fluint16(hdiff, ldiff);
}

Fluint8 Fluint16::IsZero(Fluint16 a) {
  Fluint8 hz = Fluint8::IsZero(a.hi);
  Fluint8 lz = Fluint8::IsZero(a.lo);
  // Just one bit, so we can use the faster boolean operator.
  return Fluint8::BooleanAnd(hz, lz);
}

Fluint8 Fluint16::IsntZero(Fluint16 a) {
  Fluint8 hz = Fluint8::IsntZero(a.hi);
  Fluint8 lz = Fluint8::IsntZero(a.lo);
  // Just one bit, so we can use the faster boolean operator.
  return Fluint8::BooleanOr(hz, lz);
}

Fluint16 Fluint16::SignExtend(Fluint8 a) {
  // Either 0x80 or 0.
  Fluint8 sign = Fluint8::AndWith<0x80>(a);
  // Spread the bit to all bits.
  // 1100000
  sign |= Fluint8::RightShift<1>(sign);
  // 1111000
  sign = Fluint8::PlusNoOverflow(sign, Fluint8::RightShift<2>(sign));
  // 1111111
  sign = Fluint8::PlusNoOverflow(sign, Fluint8::RightShift<4>(sign));
  CHECK(sign.ToInt() == 0xFF || sign.ToInt() == 0x00);

  return Fluint16(sign, a);
}

Fluint16 Fluint16::If(Fluint8 cc, Fluint16 t) {
  return Fluint16(Fluint8::If(cc, t.Hi()), Fluint8::If(cc, t.Lo()));
}
