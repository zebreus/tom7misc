
#include "sos-util.h"

#include <vector>
#include <utility>
#include <cstdint>
#include <bit>
#include <tuple>
#include <optional>

#include "factorize.h"
#include "base/stringprintf.h"
#include "base/logging.h"

using namespace std;

int ChaiWahWu(uint64_t sum) {
  if (sum == 0) return 1;
  std::vector<std::pair<uint64_t, int>> collated =
    Factorize::PrimeFactorization(sum);

  auto HasAnyOddPowers = [&collated]() {
      for (const auto &[p, e] : collated) {
        if (e & 1) return true;
      }
      return false;
    };

  // int(not any(e&1 for e in f.values()))
  int first = HasAnyOddPowers() ? 0 : 1;

  // (((m:=prod(1 if p==2 else (e+1 if p&3==1 else (e+1)&1)
  //   for p, e in f.items()))
  int m = 1;
  for (const auto &[p, e] : collated) {
    if (p != 2) {
      m *= (p % 4 == 1) ? e + 1 : ((e + 1) & 1);
    }
  }

  // ((((~n & n-1).bit_length()&1)<<1)-1 if m&1 else 0)
  int b = 0;
  if (m & 1) {
    int bits = std::bit_width<uint64_t>(~sum & (sum - 1));
    b = ((bits & 1) << 1) - 1;
  }

  return first + ((m + b) >> 1);
}

string WaysString(const std::vector<std::pair<uint64_t, uint64_t>> &v) {
  string out;
  for (const auto &[a, b] : v) {
    StringAppendF(&out, "%llu^2 + %llu^2, ", a, b);
  }
  return out;
}

// Slow decomposition into sums of squares two ways, for reference.
optional<tuple<uint64_t,uint64_t,uint64_t,uint64_t>>
ReferenceValidate2(uint64_t sum) {
  // Easy to see that there's no need to search beyond this.
  uint64_t limit = Sqrt64(sum);
  while (limit * limit < sum) limit++;

  // with x^2 + y^2 == sum and x <= y, get y.
  // (Or return zero)
  auto GetOther = [limit, sum](uint64_t x) -> uint64_t {
      uint64_t xx = x * x;
      for (uint64_t y = x; y <= limit; y++) {
        const uint64_t yy = y * y;
        if (xx + yy == sum) {
          return y;
        } else if (xx + yy > sum) {
          return 0;
        }
      }
      return 0;
    };

  for (uint64_t a = 0; a <= limit; a++) {
    if (uint64_t b = GetOther(a)) {
      for (uint64_t c = a + 1; c <= limit; c++) {
        if (uint64_t d = GetOther(c)) {
          return make_tuple(a, b, c, d);
        }
      }
    }
  }
  return nullopt;
}

optional<tuple<uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t>>
ReferenceValidate3(uint64_t sum) {
  // Easy to see that there's no need to search beyond this.
  uint64_t limit = Sqrt64(sum);
  while (limit * limit < sum) limit++;

  // with x^2 + y^2 == sum and x <= y, get y.
  // (Or return zero)
  auto GetOther = [limit, sum](uint64_t x) -> uint64_t {
      uint64_t xx = x * x;
      for (uint64_t y = x; y <= limit; y++) {
        const uint64_t yy = y * y;
        if (xx + yy == sum) {
          return y;
        } else if (xx + yy > sum) {
          return 0;
        }
      }
      return 0;
    };

  for (uint64_t a = 0; a <= limit; a++) {
    if (uint64_t b = GetOther(a)) {
      for (uint64_t c = a + 1; c <= limit; c++) {
        if (uint64_t d = GetOther(c)) {
          for (uint64_t e = c + 1; e <= limit; e++) {
            if (uint64_t f = GetOther(e)) {
              return make_tuple(a, b, c, d, e, f);
            }
          }
        }
      }
    }
  }
  return nullopt;
}

// from factor.c; GPL

/* MAGIC[N] has a bit i set iff i is a quadratic residue mod N.  */
# define MAGIC64 0x0202021202030213ULL
# define MAGIC63 0x0402483012450293ULL
# define MAGIC65 0x218a019866014613ULL
# define MAGIC11 0x23b

/* Return the square root if the input is a square, otherwise 0.  */
inline static bool
MaybeSquare(uint64_t x) {
  /* Uses the tests suggested by Cohen.  Excludes 99% of the non-squares before
     computing the square root.  */
  return (((MAGIC64 >> (x & 63)) & 1)
          && ((MAGIC63 >> (x % 63)) & 1)
          /* Both 0 and 64 are squares mod (65).  */
          && ((MAGIC65 >> ((x % 65) & 63)) & 1)
          && ((MAGIC11 >> (x % 11) & 1)));
}


std::vector<std::pair<uint64_t, uint64_t>>
BruteGetNWays(uint64_t sum, int num_expected) {
  if (num_expected == 0) return {};
  // We use 0 as a sentinel value below, so get that out of the way.
  // PERF: We could request this as a precondition.
  if (sum == 0) return {{0, 0}};

  // Neither factor can be larger than the square root.
  // uint64_t limit_b = Sqrt64(sum);
  // while (limit_b * limit_b < sum) limit_b++;
  // Also, since a^2 + b^2 = sum, but a < b, a^2 can actually
  // be at most half the square root.
  uint64_t limit_a = Sqrt64(sum / 2);
  while (limit_a * limit_a < (sum / 2)) limit_a++;

  // with x^2 + y^2 == sum and x <= y, get y.
  // (Or return zero)
  auto GetOther = [sum](uint64_t x) -> uint64_t {
      uint64_t xx = x * x;
      uint64_t target = sum - xx;
      if (!MaybeSquare(target))
        return 0;

      uint64_t y = Sqrt64(target);
      if (y * y != target)
        return 0;

      // Insist that the result is smaller than the
      // input, even if it would work. We find it the
      // other way. Try x = 7072 for sum = 100012225.
      if (y < x)
        return 0;

      return y;
    };

  // The way we ensure distinctness is that the pairs are ordered
  // a < b, and the search (and vector) is ordered by the first
  // element.
  std::vector<std::pair<uint64_t, uint64_t>> ret;
  for (uint64_t a = 0; a <= limit_a; a++) {
    if (uint64_t b = GetOther(a)) {
      ret.emplace_back(a, b);
      if (num_expected >= 0 && (int)ret.size() == num_expected)
        return ret;
    }
  }

  return ret;
}

#if 0
// from factor.c, gpl

// uintmax_t should be uint64
static uintmax_t
isqrt (uintmax_t n)
{
  uintmax_t x;
  unsigned c;
  if (n == 0)
    return 0;

  count_leading_zeros (c, n);

  /* Make x > sqrt(n).  This will be invariant through the loop.  */
  x = (uintmax_t) 1 << ((W_TYPE_SIZE + 1 - c) / 2);

  for (;;)
    {
      uintmax_t y = (x + n / x) / 2;
      if (y >= x)
        return x;

      x = y;
    }
}


/* MAGIC[N] has a bit i set iff i is a quadratic residue mod N.  */
# define MAGIC64 0x0202021202030213ULL
# define MAGIC63 0x0402483012450293ULL
# define MAGIC65 0x218a019866014613ULL
# define MAGIC11 0x23b

/* Return the square root if the input is a square, otherwise 0.  */
ATTRIBUTE_CONST
static uintmax_t
is_square (uintmax_t x)
{
  /* Uses the tests suggested by Cohen.  Excludes 99% of the non-squares before
     computing the square root.  */
  if (((MAGIC64 >> (x & 63)) & 1)
      && ((MAGIC63 >> (x % 63)) & 1)
      /* Both 0 and 64 are squares mod (65).  */
      && ((MAGIC65 >> ((x % 65) & 63)) & 1)
      && ((MAGIC11 >> (x % 11) & 1)))
    {
      uintmax_t r = isqrt (x);
      if (r * r == x)
        return r;
    }
  return 0;
}
#endif
