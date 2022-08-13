
#ifndef _GRAD_HASH_UTIL_H
#define _GRAD_HASH_UTIL_H

#include <array>
#include <cstdint>

#include "expression.h"
#include "choppy.h"
#include "half.h"

struct HashUtil {
  using half = half_float::half;
  using Choppy = ChoppyGrid<256>;

  // Permutations are specified by an array of 64 ints.
  // Reading bits from left (msb) to right (lsb), this gives
  // the output location for each bit. So if the first entry
  // is 49, that saysd that the 0th bit in the input is sent
  // to the 49th bit in the output.

  // A permute function computes a single byte (stored in a half)
  // by selecting bits from the entire state (eight halves). It
  // consists of eight functions, which should be applied to each
  // of the eight halves, and then their output summed. This sum
  // gives a number in [0, 255] which represents the byte (which
  // should then be normalized).
  //
  // x gives the byte index (a = 0) to compute.
  //
  // This is hard-coded to permute 64-bit words of eight 8-bit
  // bytes, but it would not be hard to parameterize.
  static std::array<const Exp *, 8> PermuteFn(
      const std::array<int, 64> &perm,
      Choppy::DB *basis, int x);

  // Permute the bits of a 64-bit word.
  // (If you need a faster version, see smallcrush-gen for an
  // unrolled one, and permbench for some failed experiments.)
  static inline uint64_t Permute64(
      const std::array<int, 64> &perm, uint64_t data) {
    uint64_t out = 0;
    for (int i = 0; i < 64; i++) {
      int in_pos = i;
      uint64_t bit = (data >> (63 - in_pos)) & 1;
      int out_pos = perm[i];
      out |= (bit << (63 - out_pos));
    }
    return out;
  }

  // TODO: Test that this works for every byte in
  // the 8-bit version!
  // h in [-1, 1). Returns the corresponding byte.
  static inline uint8_t HalfToBits(half h) {
    using namespace half_float::literal;
    // put in [0, 2)
    h += 1.0_h;
    // now in [0, 16)
    h *= (Choppy::GRID / 2.0_h);
    // and make integral
    return (uint8_t)trunc(h);
  }

  // The centered value for a given byte.
  static inline half BitsToHalf(uint8_t b) {
    using namespace half_float::literal;
    half h = (half)(int)b;
    h *= 2.0_h / Choppy::GRID;
    h -= 1.0_h;
    const half HALF_GRID = (half)(0.5 / (Choppy::GRID * 2));
    h += HALF_GRID;
    return h;
  }

};

#endif
