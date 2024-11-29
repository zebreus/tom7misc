#include "sos-quad.h"

#include "quad64.h"
#include "base/logging.h"

std::vector<std::pair<uint64_t, uint64_t>>
GetWaysQuad(uint64_t sum, int num_expected_ignored,
            int num_factors,
            const uint64_t *bases,
            const uint8_t *exponents) {
  // We want x^2 + y^2 = sum.
  // This is 1 x^2 + 0 xy + 1 y^2 + 0x + 0y + -sum = 0

  std::vector<std::pair<uint64_t, int>> factors(num_factors);
  for (int i = 0; i < num_factors; i++) {
    factors[i].first = bases[i];
    factors[i].second = exponents[i];
  }


  Solutions64 sols = SolveQuad64(sum, factors);

  // Not a bug, but I want to know!
  CHECK(!sols.interesting_coverage) << "New coverage! " << sum;

  std::vector<std::pair<uint64_t, uint64_t>> ret;
  for (const PointSolution64 &pt : sols.points) {
    uint64_t x = pt.X;
    uint64_t y = pt.Y;
    ret.emplace_back(x, y);
  }

  return ret;
}
