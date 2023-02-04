
#ifndef _GRAD_FLUINT16_H
#define _GRAD_FLUINT16_H

#include <cstdint>
#include <bit>

#include "half.h"
#include "fluint8.h"

// 16-bit numbers implemented as a pair of Fluint8s.
struct Fluint16 {

  // Converting in/out uses non-linear operations, of course.
  explicit constexpr Fluint16(uint16_t u) :
    hi(0xFF & (u >> 8)),
    lo(0xFF & u) {
  }

  explicit constexpr Fluint16(Fluint8 lo) : hi(0), lo(lo) {}
  Fluint16(Fluint8 hi, Fluint8 lo) : hi(hi), lo(lo) {}

  uint16_t ToInt() const;

  constexpr Fluint16() : hi(0), lo(0) {}
  Fluint16(Fluint16 &&other) = default;
  Fluint16(const Fluint16 &other) = default;
  constexpr Fluint16 &operator =(const Fluint16 &) = default;

  Fluint8 Hi() const { return hi; }
  Fluint8 Lo() const { return lo; }

  static Fluint16 Plus(Fluint16 a, Fluint16 b);
  static Fluint16 Minus(Fluint16 a, Fluint16 b);

  static Fluint16 BitwiseXor(Fluint16 a, Fluint16 b);
  static Fluint16 BitwiseAnd(Fluint16 a, Fluint16 b);
  static Fluint16 BitwiseOr(Fluint16 a, Fluint16 b);

  // a.k.a. !, this returns (a == 0) ? 1 : 0.
  static Fluint8 IsZero(Fluint16 a);
  // Same as !!
  static Fluint8 IsntZero(Fluint16 a);

  // Left shift by a compile-time constant.
  template<size_t n>
  static Fluint16 LeftShift(Fluint16 x);

  template<size_t n>
  static Fluint16 RightShift(Fluint16 x);

  // One bit; no sign extension.
  static Fluint16 RightShift1(Fluint16 x);
  static Fluint16 LeftShift1(Fluint16 x);

  // For testing.
  uint16_t Representation() const;

 private:
  Fluint8 hi, lo;
};

// Overloaded operators.

inline Fluint16 operator +(const Fluint16 &a, const Fluint16 &b) {
  return Fluint16::Plus(a, b);
}

inline Fluint16 operator -(const Fluint16 &a, const Fluint16 &b) {
  return Fluint16::Minus(a, b);
}

inline Fluint16 operator ^(const Fluint16 &a, const Fluint16 &b) {
  return Fluint16::BitwiseXor(a, b);
}

inline Fluint16 operator |(const Fluint16 &a, const Fluint16 &b) {
  return Fluint16::BitwiseOr(a, b);
}

inline Fluint16 operator &(const Fluint16 &a, const Fluint16 &b) {
  return Fluint16::BitwiseAnd(a, b);
}

inline Fluint16 operator +(const Fluint16 &a) {
  return a;
}

inline Fluint16 operator -(const Fluint16 &a) {
  return Fluint16::Minus(Fluint16(0), a);
}

inline Fluint16 operator ~(const Fluint16 &a) {
  return Fluint16::BitwiseXor(Fluint16(255), a);
}

inline Fluint16& operator++(Fluint16 &a) {
  a = Fluint16::Plus(a, Fluint16(1));
  return a;
}

inline Fluint16 operator++(Fluint16 &a, int) {
  Fluint16 ret = a;
  a = Fluint16::Plus(a, Fluint16(1));
  return ret;
}

inline Fluint16& operator--(Fluint16 &a) {
  a = Fluint16::Minus(a, Fluint16(1));
  return a;
}

inline Fluint16 operator--(Fluint16 &a, int) {
  Fluint16 ret = a;
  a = Fluint16::Minus(a, Fluint16(1));
  return ret;
}

inline Fluint16& operator+=(Fluint16 &a, const Fluint16 &b) {
  a = a + b;
  return a;
}

inline Fluint16& operator-=(Fluint16 &a, const Fluint16 &b) {
  a = a - b;
  return a;
}

inline Fluint16& operator^=(Fluint16 &a, const Fluint16 &b) {
  a = a ^ b;
  return a;
}

inline Fluint16& operator|=(Fluint16 &a, const Fluint16 &b) {
  a = a | b;
  return a;
}

inline Fluint16& operator&=(Fluint16 &a, const Fluint16 &b) {
  a = a & b;
  return a;
}


template<size_t N>
Fluint16 Fluint16::LeftShift(Fluint16 x) {
  if constexpr (N == 0) {
    return x;
  } else {
    Fluint16 y = LeftShift<N - 1>(x);
    return LeftShift1(y);
  }
}

// PERF partial shifts!
template<size_t N>
Fluint16 Fluint16::RightShift(Fluint16 x) {
  if constexpr (N == 0) {
    return x;
  } else if constexpr (N >= 8) {
    Fluint16 y = RightShift<N - 8>(x);
    return Fluint16(Fluint8(0), y.hi);
  } else {
    Fluint16 y = RightShift<N - 1>(x);
    return RightShift1(y);
  }
}

#endif



