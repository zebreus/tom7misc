
#ifndef _GRAD_FLUINT8_H
#define _GRAD_FLUINT8_H

#include <cstdint>

#include "half.h"

struct Exp;

struct Fluint8 {
  // Converting in/out uses non-linear operations, of course.
  explicit Fluint8(uint8_t byte);
  uint8_t ToInt() const;

  Fluint8(Fluint8 &&other) = default;
  Fluint8(const Fluint8 &other) = default;
  constexpr Fluint8 &operator =(const Fluint8 &) = default;

  static Fluint8 Plus(Fluint8 a, Fluint8 b);
  static Fluint8 Minus(Fluint8 a, Fluint8 b);

  // Left shift by a compile-time constant.
  template<size_t n>
  static Fluint8 LeftShift(Fluint8 x);

  template<size_t n>
  static Fluint8 RightShift(Fluint8 x);

  // One bit; no sign extension.
  static Fluint8 RightShift1(Fluint8 x);

  // For testing.
  uint16_t Representation() const;

 private:
  // Evaluate the expression with the given value for the variable.
  static half_float::half Eval(const Exp *, half_float::half h);
  // static Fluint8 Eval(const Exp *, Fluint8 x);

  explicit Fluint8(half_float::half h) : h(h) {}
  half_float::half h;
};

// Overloaded operators.

inline Fluint8 operator +(const Fluint8 &a, const Fluint8 &b) {
  return Fluint8::Plus(a, b);
}

inline Fluint8 operator -(const Fluint8 &a, const Fluint8 &b) {
  return Fluint8::Minus(a, b);
}

inline Fluint8 operator +(const Fluint8 &a) {
  return a;
}

inline Fluint8 operator -(const Fluint8 &a) {
  return Fluint8::Minus(Fluint8(0), a);
}

inline Fluint8& operator++(Fluint8 &a) {
  a = Fluint8::Plus(a, Fluint8(1));
  return a;
}

inline Fluint8 operator++(Fluint8 &a, int) {
  Fluint8 ret = a;
  a = Fluint8::Plus(a, Fluint8(1));
  return ret;
}

inline Fluint8& operator--(Fluint8 &a) {
  a = Fluint8::Minus(a, Fluint8(1));
  return a;
}

inline Fluint8 operator--(Fluint8 &a, int) {
  Fluint8 ret = a;
  a = Fluint8::Minus(a, Fluint8(1));
  return ret;
}

inline Fluint8& operator+=(Fluint8 &a, const Fluint8 &b) {
  a = a + b;
  return a;
}

inline Fluint8& operator-=(Fluint8 &a, const Fluint8 &b) {
  a = a - b;
  return a;
}


// Template implementations.

template<size_t N>
Fluint8 Fluint8::LeftShift(Fluint8 x) {
  if constexpr (N == 0) {
    return x;
  } else {
    Fluint8 y = LeftShift<N - 1>(x);
    return y + y;
  }
}

template<size_t N>
Fluint8 Fluint8::RightShift(Fluint8 x) {
  if constexpr (N == 0) {
    return x;
  } else {
    Fluint8 y = RightShift<N - 1>(x);
    return RightShift1(y);
  }
}


#endif


