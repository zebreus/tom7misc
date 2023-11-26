#ifndef _MONTGOMERY64_H
#define _MONTGOMERY64_H

#include "base/int128.h"

// A 64-bit number in montgomery form.
struct Montgomery64 {
  uint64_t x;
};

// Defines a montgomery form.
struct MontgomeryRep64 {
  // The modulus we're working in.
  uint64_t modulus;
  // This is the representation of 1 in montgomery form.
  uint64_t r;
  constexpr Montgomery64 One() const { return Montgomery64{.x = r}; }
  constexpr Montgomery64 ToMontgomery(uint64_t x) const {
    // x * r mod modulus
    return BasicModMultU64(x, r, modulus);
  }

  // TODO: Everything

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
