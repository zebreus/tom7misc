#ifndef _QUAD_H
#define _QUAD_H

#include <string>
#include <vector>
#include <utility>
#include <cstdint>

#include "bignum/big.h"

struct PointSolution {
  uint64_t X, Y;
};

struct Solutions {
  bool interesting_coverage = false;
  std::vector<PointSolution> points;
};

// Solve x^2 + y^2 = f.
// Needs the prime factorization (e.g. from Factorization::Factorize).
Solutions SolveQuad(
    uint64_t f,
    const std::vector<std::pair<uint64_t, int>> &factors);

#endif
