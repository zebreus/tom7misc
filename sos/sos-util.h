
#ifndef _SOS_SOS_UTIL_H
#define _SOS_SOS_UTIL_H

#include <string>
#include <cstdint>
#include <optional>
#include <tuple>
#include <vector>
#include <utility>
#include <cmath>

inline uint64_t Sqrt64(uint64_t n) {
  if (n == 0) return 0;
  uint64_t r = std::sqrt((double)n);
  return r - (r * r - 1 >= n);
}

// Squares mod 8 have certain residues, so their sum
// can't take on the values 3, 6, or 7. See filter.z3.
inline bool MaybeSumOfSquares(uint64_t n) {
  switch (n & 7) {
  case 3:
  case 6:
  case 7:
    return false;
  default:
    return true;
  }
}

// Returns the number of ways that 'sum' can be written as
// a^2 + b^2. Much faster than enumerating them.
// From note on OEIS by Chai Wah Wu, Sep 08 2022.
int ChaiWahWu(uint64_t sum);

// Slow decomposition into sums of squares two ways, for reference.
std::optional<std::tuple<uint64_t, uint64_t, uint64_t, uint64_t>>
ReferenceValidate2(uint64_t sum);

// Same for three ways.
std::optional<std::tuple<uint64_t, uint64_t,
                         uint64_t, uint64_t,
                         uint64_t, uint64_t>>
ReferenceValidate3(uint64_t sum);

// Generate all of the ways that the sum can be written as
// a^2 + b^2. If num_expected is nonnegative, stop once we
// get that many.
// Brute force, but with smarter limits on search.
std::vector<std::pair<uint64_t, uint64_t>>
BruteGetNWays(uint64_t sum, int num_expected = -1);

// Another algorithm for the above (based on some Maple code), which
// still does an O(sqrt(n)) search, but with even smarter limits.
// This is the fastest CPU method so far.
std::vector<std::pair<uint64_t, uint64_t>>
NSoks2(uint64_t sum, int num_expected = -1);

std::string WaysString(
    const std::vector<std::pair<uint64_t, uint64_t>> &v);

void NormalizeWays(std::vector<std::pair<uint64_t, uint64_t>> *v);

#endif
