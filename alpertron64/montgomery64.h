#ifndef _MONTGOMERY64_H
#define _MONTGOMERY64_H

#include "base/int128.h"

// A 64-bit number in montgomery form.
struct Montgomery64 {
  // As a representation invariant, this will be in [0, modulus).
  uint64_t x;
};

// Defines a montgomery form.
struct MontgomeryRep64 {
  // The modulus we're working in.
  uint64_t modulus;
  uint64_t inv;
  // (2^64)^2 mod modulus.
  uint64_t r_squared;

  MontgomeryRep64(uint64_t modulus) : modulus(modulus) {
    // PERF can be done in fewer steps with tricks
    inv = 1;
    // PERF don't need this many steps!
    for (int i = 0; i < 7; i++)
      inv *= 2 - modulus * inv;

    // Initialize r_squared = 2^128 mod modulus.
    uint64_t r2 = -modulus % modulus;
    for (int i = 0; i < 2; i++) {
      r2 <<= 1;
      if (r2 >= modulus)
        r2 -= modulus;
    }
    // r2 = r * 2^2 mod m

    for (int i = 0; i < 5; i++)
      r2 = Mult(Montgomery64{.x = r2}, Montgomery64{.x = r2}).x;

    // r2 = r * (2^2)^(2^5) = 2^64
    r_squared = r2;
  }

  constexpr Montgomery64 One() const { return Montgomery64{.x = r_squared}; }
  inline constexpr Montgomery64 ToMontgomery(uint64_t x) const {
    // PERF necessary?
    x %= modulus;
    return Mult(Montgomery64{.x = x}, One());
    // x * r mod modulus
    // return BasicModMultU64(x, r, modulus);
  }

  // TODO: Everything

  inline Montgomery64 Add(Montgomery64 a, Montgomery64 b) const {
    // PERF: Should be able to do this without int128
    uint128_t aa(a.x);
    uint128_t bb(b.x);
    uint128_t ss = aa + bb;
    if (ss > (uint128_t)modulus) {
      ss -= (uint128_t)modulus;
    }
    return Montgomery64{.x = (uint64_t)ss};
  }

  inline Montgomery64 Sub(Montgomery64 a, Montgomery64 b) const {
    // PERF: Should be able to do this without int128
    int128_t aa(a.x);
    int128_t bb(b.x);
    int128_t ss = aa - bb;
    if (ss < 0) {
      ss += (int128_t)modulus;
    }
    return Montgomery64{.x = (uint64_t)ss};
  }

  inline Montgomery64 Mult(Montgomery64 a, Montgomery64 b) const {
    uint128_t aa(a.x);
    uint128_t bb(b.x);
    uint64_t r = Reduce(aa * bb);
    return Montgomery64{.x = r};
  }

  // ?
  inline uint64_t ToInt(Montgomery64 a) const {
    return Reduce((uint128_t)a.x);
  }

 private:

  inline uint64_t Reduce(uint128_t x) const {
    // ((x % 2^64) * inv) % 2^64
    uint64_t q = Uint128Low64(x) * inv;
    // (x / 2^64) - (q * modulus) / 2^64
    int64_t a =
      (int64_t)(
          (int128_t)Uint128High64(x) -
          Uint128High64((int128_t)q * (int128_t)modulus));
    if (a < 0)
      a += modulus;
    return (uint64_t)a;
  }

  // Normal multiplication, not montgomery.
  static inline uint64_t BasicModMultU64(uint64_t a, uint64_t b,
                                         uint64_t m) {
    // PERF: Check that this is making appropriate simplifications
    // knowing that each high word is zero.
    uint128 aa(a);
    uint128 bb(b);
    uint128 mm(m);

    uint128 rr = (aa * bb) % mm;
    return (uint64_t)rr;
  }
};

#endif
