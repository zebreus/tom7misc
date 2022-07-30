
#ifndef _GRAD_HCOMPLEX_H
#define _GRAD_HCOMPLEX_H

#include <cstdint>

#include "half.h"

struct hcomplex {
  using half = half_float::half;

  half rpart = GetHalf(0), ipart = GetHalf(0);

  hcomplex() {}
  hcomplex(half r, half i) : rpart(r), ipart(i) {}

  hcomplex Conj() const { return hcomplex(rpart, -ipart); }
  half Abs() const { return sqrt(rpart * rpart + ipart * ipart); }

  half Re() const { return rpart; }
  half Im() const { return ipart; }

 private:
  static inline constexpr uint16_t GetU16(half h) {
    uint16_t u;
    static_assert(sizeof (h) == sizeof (u));
    memcpy((void*)&u, (void*)&h, sizeof (u));
    return u;
  }

  static inline constexpr half GetHalf(uint16_t u) {
    half h;
    static_assert(sizeof (h) == sizeof (u));
    memcpy((void*)&h, (void*)&u, sizeof (u));
    return h;
  }
};

inline hcomplex operator +(const hcomplex &a, const hcomplex &b) {
  return hcomplex(a.rpart + b.rpart, a.ipart + b.ipart);
}

inline hcomplex operator -(const hcomplex &a, const hcomplex &b) {
  return hcomplex(a.rpart - b.rpart, a.ipart - b.ipart);
}

inline hcomplex operator *(const hcomplex &a, const hcomplex &b) {
  // (a.rpart + a.ipart * i)(b.rpart + b.ipart * i) =
  // a.rpart * b.rpart +
  //   a.rpart * b.ipart * i + a.ipart * b.rpart * i +
  // a.ipart * b.ipart * i^2  =
  //   (because i^2 = -1)
  // a.rpart * b.rpart - a.ipart * b.ipart +
  //    (a.rpart * b.ipart + a.ipart * b.rpart) * i
  return hcomplex(a.rpart * b.rpart - a.ipart * b.ipart,
                  a.rpart * b.ipart + a.ipart * b.rpart);
}

// TODO: Division, etc.

// Operations with scalars.

inline hcomplex operator +(const hcomplex &a, const hcomplex::half &s) {
  return hcomplex(a.rpart + s, a.ipart);
}

inline hcomplex operator -(const hcomplex &a, const hcomplex::half &s) {
  return hcomplex(a.rpart - s, a.ipart);
}

inline hcomplex operator *(const hcomplex &a, const hcomplex::half &s) {
  // This is the same thing we get with s + 0i, but saving several
  // terms that don't do anything.
  return hcomplex(a.rpart * s, a.ipart * s);
}

// unary negation; same as multiplication by scalar -1.
inline hcomplex operator -(const hcomplex &a) {
  return hcomplex(-a.rpart, -a.ipart);
}

#endif
