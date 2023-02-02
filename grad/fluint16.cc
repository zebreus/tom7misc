
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

Fluint16 Fluint16::Minus(Fluint16 a, Fluint16 b) {
  auto [lcarry, ldiff] = Fluint8::SubtractWithCarry(a.lo, b.lo);
  // carry here is ignored
  Fluint8 hdiff = a.hi - b.hi - lcarry;
  return Fluint16(hdiff, ldiff);
}
