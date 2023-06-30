
#ifndef _GRAD_HFLUINT16_H
#define _GRAD_HFLUINT16_H

#include <cstdint>
#include <bit>

#include "half.h"
#include "hfluint8.h"

// 16-bit numbers implemented as a pair of hfluint8s.
struct hfluint16 {

  // Converting in/out uses non-linear operations, of course.
  explicit constexpr hfluint16(uint16_t u) :
    hi(0xFF & (u >> 8)),
    lo(0xFF & u) {
  }

  explicit constexpr hfluint16(hfluint8 lo) : hi(0), lo(lo) {}
  hfluint16(hfluint8 hi, hfluint8 lo) : hi(hi), lo(lo) {}

  uint16_t ToInt() const;

  constexpr hfluint16() : hi(0), lo(0) {}
  hfluint16(hfluint16 &&other) = default;
  hfluint16(const hfluint16 &other) = default;
  constexpr hfluint16 &operator =(const hfluint16 &) = default;

  static hfluint16 SignExtend(hfluint8 a);

  hfluint8 Hi() const { return hi; }
  hfluint8 Lo() const { return lo; }

  static hfluint16 Plus(hfluint16 a, hfluint16 b);
  static hfluint16 Plus(hfluint16 a, hfluint8 b);
  static hfluint16 Minus(hfluint16 a, hfluint16 b);

  static hfluint16 BitwiseXor(hfluint16 a, hfluint16 b);
  static hfluint16 BitwiseAnd(hfluint16 a, hfluint16 b);
  static hfluint16 BitwiseOr(hfluint16 a, hfluint16 b);

  // a.k.a. !, this returns (a == 0) ? 1 : 0.
  static hfluint8 IsZero(hfluint16 a);
  // Same as !!
  static hfluint8 IsntZero(hfluint16 a);

  // For cc = 0x01 or 0x00 (only), returns c ? t : 0.
  static hfluint16 If(hfluint8 cc, hfluint16 t);

  // Left shift by a compile-time constant.
  template<size_t n>
  static hfluint16 LeftShift(hfluint16 x);

  template<size_t n>
  static hfluint16 RightShift(hfluint16 x);

  // One bit; no sign extension.
  static hfluint16 RightShift1(hfluint16 x);
  static hfluint16 LeftShift1(hfluint16 x);

  // Computes a + b, if neither (ah + bh) nor (al + bl) overflows.
  // (h is the high byte, l is the low).
  static hfluint16 PlusNoByteOverflow(hfluint16 a, hfluint16 b);

  // For testing.
  uint16_t Representation() const;

 private:
  hfluint8 hi, lo;
};

// Overloaded operators.

inline hfluint16 operator +(const hfluint16 &a, const hfluint16 &b) {
  return hfluint16::Plus(a, b);
}

inline hfluint16 operator +(const hfluint16 &a, const hfluint8 &b) {
  return hfluint16::Plus(a, b);
}

inline hfluint16 operator -(const hfluint16 &a, const hfluint16 &b) {
  return hfluint16::Minus(a, b);
}

inline hfluint16 operator ^(const hfluint16 &a, const hfluint16 &b) {
  return hfluint16::BitwiseXor(a, b);
}

inline hfluint16 operator |(const hfluint16 &a, const hfluint16 &b) {
  return hfluint16::BitwiseOr(a, b);
}

inline hfluint16 operator &(const hfluint16 &a, const hfluint16 &b) {
  return hfluint16::BitwiseAnd(a, b);
}

inline hfluint16 operator +(const hfluint16 &a) {
  return a;
}

inline hfluint16 operator -(const hfluint16 &a) {
  return hfluint16::Minus(hfluint16(0), a);
}

inline hfluint16 operator ~(const hfluint16 &a) {
  return hfluint16::BitwiseXor(hfluint16(255), a);
}

inline hfluint16& operator++(hfluint16 &a) {
  a = hfluint16::Plus(a, hfluint8(1));
  return a;
}

inline hfluint16 operator++(hfluint16 &a, int) {
  hfluint16 ret = a;
  a = hfluint16::Plus(a, hfluint8(1));
  return ret;
}

// PERF can implement 8-bit version as with plus
inline hfluint16& operator--(hfluint16 &a) {
  a = hfluint16::Minus(a, hfluint16(1));
  return a;
}

inline hfluint16 operator--(hfluint16 &a, int) {
  hfluint16 ret = a;
  a = hfluint16::Minus(a, hfluint16(1));
  return ret;
}

inline hfluint16& operator+=(hfluint16 &a, const hfluint16 &b) {
  a = a + b;
  return a;
}

inline hfluint16& operator+=(hfluint16 &a, const hfluint8 &b) {
  a = a + b;
  return a;
}

inline hfluint16& operator-=(hfluint16 &a, const hfluint16 &b) {
  a = a - b;
  return a;
}

inline hfluint16& operator^=(hfluint16 &a, const hfluint16 &b) {
  a = a ^ b;
  return a;
}

inline hfluint16& operator|=(hfluint16 &a, const hfluint16 &b) {
  a = a | b;
  return a;
}

inline hfluint16& operator&=(hfluint16 &a, const hfluint16 &b) {
  a = a & b;
  return a;
}


template<size_t N>
hfluint16 hfluint16::LeftShift(hfluint16 x) {
  if constexpr (N == 0) {
    return x;
  } else {
    hfluint16 y = LeftShift<N - 1>(x);
    return LeftShift1(y);
  }
}

// PERF partial shifts!
template<size_t N>
hfluint16 hfluint16::RightShift(hfluint16 x) {
  if constexpr (N == 0) {
    return x;
  } else if constexpr (N >= 8) {
    hfluint16 y = RightShift<N - 8>(x);
    return hfluint16(hfluint8(0), y.hi);
  } else {
    hfluint16 y = RightShift<N - 1>(x);
    return RightShift1(y);
  }
}

#endif



