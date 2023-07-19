
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

// From GNU coreutils's factor.c.
// Return false if the input cannot be a square.
inline static bool
MaybeSquare(uint64_t x) {
  // MAGIC[N] has a bit i set iff i is a quadratic residue mod N.
  static constexpr uint64_t MAGIC64 = 0x0202021202030213ULL;
  static constexpr uint64_t MAGIC63 = 0x0402483012450293ULL;
  static constexpr uint64_t MAGIC65 = 0x218a019866014613ULL;
  static constexpr uint32_t MAGIC11 = 0x23b;
  // Uses the tests suggested by Cohen.  Excludes 99% of the non-squares
  // before computing the square root.
  return (((MAGIC64 >> (x & 63)) & 1)
          && ((MAGIC63 >> (x % 63)) & 1)
          /* Both 0 and 64 are squares mod (65).  */
          && ((MAGIC65 >> ((x % 65) & 63)) & 1)
          && ((MAGIC11 >> (x % 11) & 1)));
}

inline std::optional<uint64_t> Sqrt64Opt(uint64_t n) {
  if (n == 0) return {0};
  if (!MaybeSquare(n)) return std::nullopt;
  uint64_t r = std::sqrt((double)n);
  // now either r or r-1 is the square root.
  const uint64_t rr = r * r;
  if (rr == n) return {r};
  // now test (r-1)^2 (without multiplying) as r^2 - 2r + 1.
  if (rr - (r << 1) + 1 == n) {
    return {r - 1};
  }
  return std::nullopt;
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
  // 10/10
  if ((23 * (sum % 121)) % 127 > 115) return false;
  // 18/18
  if ((170 * (sum % 361)) % 359 > 340) return false;
  // 22/22
  if ((47 * (sum % 529)) % 541 > 517) return false;
  // 30/30
  if ((63 * (sum % 961)) % 977 > 945) return false;
  // 42/42
  if ((87 * (sum % 1849)) % 1871 > 1827) return false;
  // 46/46
  if ((189 * (sum % 2209)) % 2221 > 2173) return false;
  // 58/58
  if ((119 * (sum % 3481)) % 3511 > 3451) return false;
  // 66/66
  if ((135 * (sum % 4489)) % 4523 > 4455) return false;
  // 70/70
  if ((143 * (sum % 5041)) % 5077 > 5005) return false;
  // 78/78
  if ((396 * (sum % 6241)) % 6257 > 6177) return false;
  // 82/82
  if ((250 * (sum % 6889)) % 6917 > 6833) return false;
  // 102/102
  if ((2679 * (sum % 10609)) % 10613 > 10509) return false;
  // 106/106
  if ((643 * (sum % 11449)) % 11467 > 11359) return false;
  // 126/126
  if ((380 * (sum % 16129)) % 16087 > 15960) return false;
  return true;
}

inline constexpr bool MaybeSumOfSquaresFancy3(uint64_t sum) {
  if (!MaybeSumOfSquaresFancy2(sum)) return false;

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

inline constexpr bool MaybeSumOfSquaresFancy4(uint64_t sum) {
  if (!MaybeSumOfSquaresFancy3(sum)) return false;
  // 282/282
  if ((13876 * (sum % 80089)) % 80141 > 79857) return false;
  // 250/250
  if ((2260 * (sum % 63001)) % 63029 > 62777) return false;
  // 166/166
  if ((1003 * (sum % 27889)) % 27917 > 27749) return false;
  // 330/330
  if ((15231 * (sum % 109561)) % 109597 > 109265) return false;
  // 138/138
  if ((1673 * (sum % 19321)) % 19379 > 19239) return false;
  // 150/150
  if ((452 * (sum % 22801)) % 22751 > 22600) return false;
  // 238/238
  if ((4062 * (sum % 57121)) % 57107 > 56868) return false;
  // 270/270
  if ((1354 * (sum % 73441)) % 73387 > 73116) return false;

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

// Another attempt at this, which is O(sqrt(n)), but avoids square
// roots in the inner loop. It works out nicely but nsoks2 is still
// twice as fast.
std::vector<std::pair<uint64_t, uint64_t>>
GetWaysMerge(uint64_t sum, int num_expected = -1);

std::string WaysString(
    const std::vector<std::pair<uint64_t, uint64_t>> &v);

void NormalizeWays(std::vector<std::pair<uint64_t, uint64_t>> *v);

#endif
