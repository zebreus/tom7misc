
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
// MaybeSumOfSquares below should be better in most cases
// (fewer false positives).
inline bool MaybeSumOfSquares8(uint64_t n) {
  switch (n & 7) {
  case 3:
  case 6:
  case 7:
    return false;
  default:
    return true;
  }
}

// See residue.cc.
inline constexpr bool MaybeSumOfSquares(uint64_t sum) {
  return !(((0x0000000001209048LLU >> (sum % 27)) & 1) |
           ((0x0000040810204080LLU >> (sum % 49)) & 1) |
           ((0xd9c9d8c8d9c8d8c8LLU >> (sum % 64)) & 1));
}

inline constexpr bool MaybeSumOfSquaresFancy2(uint64_t sum) {
  if (!MaybeSumOfSquares(sum)) return false;
  if ((41 * (sum % 121)) % 113 > 102) return false;
  if ((73 * (sum % 361)) % 347 > 328) return false;
  if ((177 * (sum % 529)) % 509 > 486) return false;
  if ((272 * (sum % 961)) % 937 > 906) return false;
  if ((1095 * (sum % 1849)) % 1811 > 1768) return false;
  if ((1669 * (sum % 2209)) % 2179 > 2132) return false;
  if ((2502 * (sum % 3481)) % 3433 > 3374) return false;
  if ((66 * (sum % 4489)) % 4423 > 4356) return false;
  if ((1681 * (sum % 5041)) % 4973 > 4902) return false;
  if ((78 * (sum % 6241)) % 6163 > 6084) return false;
  if ((3617 * (sum % 6889)) % 6823 > 6740) return false;
  if ((6022 * (sum % 10609)) % 10513 > 10410) return false;
  if ((1273 * (sum % 11449)) % 11351 > 11244) return false;
  if ((6428 * (sum % 16129)) % 16007 > 15880) return false;
  return true;
}

inline constexpr bool MaybeSumOfSquaresFancy3(uint64_t sum) {
  if (!MaybeSumOfSquaresFancy2(sum)) return false;

  // 2/182 bad :(
  // if ((2 * (sum % 729)) % 487 > 484) return false;

  // 130/130
  if ((1561 * (sum % 17161)) % 17041 > 16910) return false;
  // 262/262
  if ((6289 * (sum % 69169)) % 68917 > 68654) return false;
  // 210/210
  if ((1892 * (sum % 44521)) % 44357 > 44146) return false;
  // 198/198
  if ((11288 * (sum % 39601)) % 39409 > 39210) return false;
  // 310/310
  if ((20467 * (sum % 96721)) % 96443 > 96132) return false;
  // 222/222
  if ((7997 * (sum % 49729)) % 49537 > 49314) return false;
  // 226/226
  if ((20568 * (sum % 51529)) % 51307 > 51080) return false;
  // 190/190
  if ((12161 * (sum % 36481)) % 36293 > 36102) return false;
  // 162/162
  if ((162 * (sum % 26569)) % 26407 > 26244) return false;
  // 178/178
  if ((713 * (sum % 32041)) % 31907 > 31728) return false;
  // 306/306
  if ((13465 * (sum % 94249)) % 93949 > 93642) return false;

  return true;
}

// Returns the number of ways that 'sum' can be written as
// a^2 + b^2. Much faster than enumerating them.
// From note on OEIS by Chai Wah Wu, Sep 08 2022.
int ChaiWahWu(uint64_t sum);

// For reference/testing. Same as above but skipping the sum of
// squares filters.
int ChaiWahWuNoFilter(uint64_t sum);

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
