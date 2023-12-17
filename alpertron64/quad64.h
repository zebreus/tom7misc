#ifndef _QUAD64_H
#define _QUAD64_H

#include <string>
#include <vector>
#include <utility>
#include <cstdint>

#include "bignum/big.h"

struct PointSolution {
  uint64_t X, Y;
};

// XXX temporary
struct MaxValue {
  BigInt max;
  void Observe(const BigInt &x) {
    BigInt xa = BigInt::Abs(x);
    if (BigInt::Greater(xa, max)) max = std::move(xa);
  }
};

struct Solutions {
  std::vector<PointSolution> points;
  bool interesting_coverage = false;

  // From GetNextConvergent
  // MaxValue o1, o2;
  // MaxValue vsquared;
#if 0
  MaxValue u, u1, u2;
  MaxValue v, v1, v2;

  MaxValue tmp, tmp2, tmp3;
#endif
};

// Solve x^2 + y^2 = f.
// This is probably not correct for numbers larger than 2^60 or so,
// since we sometimes do stuff like (value << 1).
// Needs the prime factorization (e.g. from Factorization::Factorize).
Solutions SolveQuad64(
    uint64_t f,
    const std::vector<std::pair<uint64_t, int>> &factors);

#endif
