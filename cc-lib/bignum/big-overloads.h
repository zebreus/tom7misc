// Operator overloads for big.h. These can be very nice if doing
// a lot of math, but they also can obscure what's really going
// on (or pollute the namespace), so they are kept separate.

#ifndef _CCLIB_BIGNUM_BIG_OVERLOADS_H
#define _CCLIB_BIGNUM_BIG_OVERLOADS_H

#include "bignum/big.h"

// TODO modifying operators
// TODO spaceship
// TODO << >> | & ^
// TODO more operators that take int64 on one side
// TODO bigq operators, maybe in a separate file?

inline BigInt operator ""_b(const char *s) {
  return BigInt(s);
}

inline BigInt operator *(const BigInt &a, const BigInt &b) {
  return BigInt::Times(a, b);
}

inline BigInt operator *(const BigInt &a, int64_t b) {
  return BigInt::Times(a, b);
}

inline BigInt operator *(int64_t a, const BigInt &b) {
  return BigInt::Times(b, a);
}

inline BigInt operator +(const BigInt &a, const BigInt &b) {
  return BigInt::Plus(a, b);
}

inline BigInt operator /(const BigInt &a, const BigInt &b) {
  return BigInt::Div(a, b);
}

inline BigInt operator /(const BigInt &a, int64_t b) {
  return BigInt::Div(a, b);
}

inline BigInt operator %(const BigInt &a, const BigInt &b) {
  return BigInt::Mod(a, b);
}

inline BigInt operator -(const BigInt &a, const BigInt &b) {
  return BigInt::Minus(a, b);
}

inline BigInt operator -(const BigInt &a) {
  return BigInt::Negate(a);
}

inline BigInt operator -(BigInt &&a) {
  return BigInt::Negate(std::move(a));
}

inline BigInt operator +(const BigInt &a) {
  return a;
}

inline BigInt operator <<(const BigInt &a, uint64_t bits) {
  return BigInt::LeftShift(a, bits);
}

inline bool operator <(const BigInt &a, const BigInt &b) {
  return BigInt::Less(a, b);
}

inline bool operator <=(const BigInt &a, const BigInt &b) {
  return BigInt::LessEq(a, b);
}

inline bool operator >(const BigInt &a, const BigInt &b) {
  return BigInt::Greater(a, b);
}

inline bool operator >=(const BigInt &a, const BigInt &b) {
  return BigInt::GreaterEq(a, b);
}

inline bool operator ==(const BigInt &a, const BigInt &b) {
  return BigInt::Eq(a, b);
}

inline bool operator ==(const BigInt &a, int64_t b) {
  return BigInt::Eq(a, b);
}

inline bool operator ==(int64_t a, const BigInt &b) {
  return BigInt::Eq(b, a);
}

#endif
