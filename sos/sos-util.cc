
#include "sos-util.h"

#include <vector>
#include <utility>
#include <cstdint>
#include <bit>
#include <tuple>
#include <optional>
#include <algorithm>

#include "factorize.h"
#include "base/stringprintf.h"
#include "base/logging.h"
#include "ansi.h"

using namespace std;

int ChaiWahWuNoFilter(uint64_t sum) {
  if (sum == 0) return 1;

  uint64_t bases[20];
  uint8_t exponents[20];
  int num_factors =
    Factorize::PrimeFactorizationPreallocated(sum, bases, exponents);

  auto AllEvenPowers = [&exponents, num_factors]() {
      for (int i = 0; i < num_factors; i++) {
        if (exponents[i] & 1) return false;
      }
      return true;
    };

  // If all of the powers are even, then it is itself a square.
  // So we have e.g. x^2 * y^2 * z^4 = (xyz^2)^2 + 0^2
  //
  // int(not any(e&1 for e in f.values()))
  int first = AllEvenPowers() ? 1 : 0;
  // PERF: For this form, we also know what the sum of squares is,
  // so we could do this and skip kernel1. But that phase is only
  // 0.38% of the time spent in the GPU.

  // (((m:=prod(1 if p==2 else (e+1 if p&3==1 else (e+1)&1)
  //   for p, e in f.items()))
  int m = 1;
  // for (const auto &[p, e] : collated) {
  for (int i = 0; i < num_factors; i++) {
    const uint64_t p = bases[i];
    const int e = exponents[i];
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

int ChaiWahWu(uint64_t sum) {
  if (sum == 0) return 1;

  // Don't factor if it's impossible.
  if (!MaybeSumOfSquaresFancy4(sum)) return 0;
  return ChaiWahWuNoFilter(sum);
}


string WaysString(const std::vector<std::pair<uint64_t, uint64_t>> &v) {
  string out;
  for (const auto &[a, b] : v) {
    StringAppendF(&out, "%llu^2 + %llu^2, ", a, b);
  }
  return out;
}

void NormalizeWays(std::vector<std::pair<uint64_t, uint64_t>> *v) {
  for (auto &p : *v)
    if (p.first > p.second)
      std::swap(p.first, p.second);

  std::sort(v->begin(), v->end(),
            [](const std::pair<uint64_t, uint64_t> &x,
               const std::pair<uint64_t, uint64_t> &y) {
              return x.first < y.first;
            });
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

static inline std::optional<uint64_t> NSoks1(uint64_t n) {
  if (!MaybeSquare(n))
    return nullopt;

  uint64_t r = Sqrt64(n);
  uint64_t rr = r * r;
  if (rr == n) return {r};
  else return nullopt;
}

// This is based on some Maple code by Joe Riel, 2006
//
// This runs trials over a smaller range; I don't quite understand
// why the min side works (I guess it's like: The factors can't both
// be tiny or both be large), but it passes the tests. About 45% faster
// than BruteNWays above.
std::vector<std::pair<uint64_t, uint64_t>>
NSoks2(uint64_t n, int num_expected) {
  uint64_t maxts = Sqrt64(n - 2 + 1);
  uint64_t maxtsmaxts = maxts * maxts;
  // Note: In maple, isqrt can be up to 1 off in either direction
  // when the input is not a perfect square. Might be able to do
  // better here since Sqrt64 never overestimates.
  if (maxtsmaxts > n - 2 + 1)
    maxts--;

  uint64_t q = n / 2;
  uint64_t r = n % 2;
  uint64_t mints = Sqrt64(q + (r ? 1 : 0));
  if (2 * mints * mints < n) {
    mints++;
  }

  // printf("nsoks2: from %llu to %llu\n", mints, maxts);

  std::vector<std::pair<uint64_t, uint64_t>> res;
  if (num_expected >= 0) res.reserve(num_expected);

  // The original nsoks tries all K and then fills with zero. For
  // K=2, this is just the case that the input is a perfect square.
  auto sq = NSoks1(n);
  if (sq.has_value()) {
    res.emplace_back(sq.value(), 0);
    if (num_expected == 1)
      return res;
  }

  for (uint64_t trialsquare = mints; trialsquare <= maxts; trialsquare++) {
    auto vo = NSoks1(n - trialsquare * trialsquare);
    if (vo.has_value()) {
      res.emplace_back(vo.value(), trialsquare);
      if (num_expected >= 0 && res.size() == num_expected)
        break;
    }
  }
  return res;
}

// This is a version of the above for arbitrary K; untested.
// n: target number to represent
// K is the number of squares in the sum
template<size_t K>
[[maybe_unused]]
static std::vector<std::vector<uint64_t>>
NSoksK(uint64_t n, uint64_t maxsq,
       int num_expected = -1) {
  uint64_t maxts = Sqrt64(n - K + 1);
  uint64_t maxtsmaxts = maxts * maxts;
  if constexpr (K == 1) {
    if (maxtsmaxts == n) return {{maxts}};
    else return {};
  } else {
    if (maxtsmaxts > n - K + 1)
      maxts--;

    maxts = std::min(maxts, maxsq);
    uint64_t q = n / K;
    uint64_t r = n % K;
    uint64_t mints = Sqrt64(q + r ? 1 : 0);
    // minTs := isqrt(iquo(n,k,'r') + `if`(r<>0,1,0));
    if (K * mints * mints < n) {
      mints++;
    }

    std::vector<std::vector<uint64_t>> res;
    if (num_expected >= 0) res.reserve(num_expected);
    for (uint64_t trialsquare = mints; trialsquare <= maxts; trialsquare++) {
      auto val = NSoksK<K - 1>(n - trialsquare * trialsquare, trialsquare);
      for (auto &v : val) {
        v.push_back(trialsquare);
        res.push_back(std::move(v));
        if (num_expected >= 0 && res.size() == num_expected)
          break;
      }
    }
    return res;
  }
}

// Another attempt at this, which is O(sqrt(n)), but avoids square
// roots in the inner loop.

#if 0
std::vector<std::pair<uint64_t, uint64_t>>
GetWaysMerge(uint64_t sum, int num_expected) {
  static constexpr bool VERBOSE = false;
  uint64_t root = Sqrt64(sum);
  std::vector<std::pair<uint64_t, uint64_t>> ways;

  uint64_t a = 0;
  uint64_t aa = 0;
  uint64_t b = root + 1;
  uint64_t bb = b * b;

  while (b >= a) {
    if (VERBOSE) {
      printf("%llu^2 + %llu^2 == %llu?\n", a, b, sum);
    }
    // PERF: We compute this below.
    if (aa + bb == sum) {
      if (VERBOSE) {
        printf("got " AGREEN("%llu^2 + %llu^2 == %llu") "\n",
               a, b, sum);
      }
      ways.emplace_back(a, b);
      if (num_expected >= 0 && ways.size() == num_expected)
        break;
    }

    // PERF can compute these without squaring
    uint64_t ap = a + 1;
    uint64_t apap = ap * ap;

    uint64_t bm = b - 1;
    uint64_t bmbm = bm * bm;

    // Either increase a or decrease b. Which one gets
    // us closer to sum?
    int64_t da = llabs((int64_t)(apap + bb) - (int64_t)sum);
    int64_t db = llabs((int64_t)(aa + bmbm) - (int64_t)sum);

    if (VERBOSE) {
    printf(AGREY("da: %llu^2 (%llu) + %llu^2 (%llu) = %llu   (err %lld)") "\n"
           AGREY("db: %llu^2 (%llu) + %llu^2 (%llu) = %llu   (err %lld)") "\n",
           ap, apap, b, bb, (apap + bb), da,
           a, aa, bm, bmbm, (aa + bmbm), db);
    }

    if (da < db) {
      a = ap;
      aa = apap;
    } else {
      b = bm;
      bb = bmbm;
    }
  }
  return ways;
}
#endif

std::vector<std::pair<uint64_t, uint64_t>>
GetWaysMerge(uint64_t sum, int num_expected) {
  uint64_t root = Sqrt64(sum);
  std::vector<std::pair<uint64_t, uint64_t>> ways;

  uint64_t a = 0;
  uint64_t aa = 0;
  uint64_t b = root + 1;
  uint64_t bb = b * b;

  uint64_t aaplusbb = aa + bb;

  while (b >= a) {
    // PERF: We compute this below.
    if (aaplusbb == sum) {
      ways.emplace_back(a, b);
      if (num_expected >= 0 && ways.size() == num_expected)
        break;
    }

    // uint64_t ap = a + 1;
    // (a + 1) * (a + 1) == a^2 + 2a + 1
    // uint64_t apap = aa + a + ap;
    // uint64_t apap = aa + (a << 1) + 1;
    // this is the term that when added to a^2, gives us (a+1)^2
    uint64_t ainc = (a << 1) + 1;

    // uint64_t bm = b - 1;
    // (b - 1) * (b - 1) == b^2 - 2b + 1
    // uint64_t bmbm = bb - (b << 1) + 1;
    // this is the term that when subtracted from b^2, gives (b-1)^2
    uint64_t bdec = (b << 1) - 1;

    // Either increase a or decrease b. Which one gets
    // us closer to sum?
    // uint64_t asum = apap + bb;
    uint64_t asum = aaplusbb + ainc;
    // uint64_t bsum = aa + bmbm;
    uint64_t bsum = aaplusbb - bdec;

    int64_t da = llabs((int64_t)asum - (int64_t)sum);
    int64_t db = llabs((int64_t)bsum - (int64_t)sum);

    if (da < db) {
      a++;
      aa += ainc;
      aaplusbb = asum;
    } else {
      b--;
      bb -= bdec;
      aaplusbb = bsum;
    }
  }
  return ways;
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
#endif
