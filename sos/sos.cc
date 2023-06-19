#include <cmath>
#include <memory>
#include <vector>
#include <functional>
#include <string>
#include <bit>
#include <tuple>

// #include "clutil.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "arcfour.h"
#include "randutil.h"
#include "threadutil.h"
#include "util.h"
#include "periodically.h"
#include "timer.h"
#include "ansi.h"
#include "autoparallel.h"
#include "factorize.h"

using namespace std;

// static CL *cl = nullptr;

using int64 = int64_t;

// https://www.nuprl.org/MathLibrary/integer_sqrt/
#if 0
static uint64_t Sqrt64(uint64_t xx) {
  if (xx <= 1) return xx;
  // z = xx / 4
  uint64_t z = xx >> 2;
  uint64_t r2 = 2 * Sqrt64(z);
  uint64_t r3 = r2 + 1;
  return (xx < r3 * r3) ? r2 : r3;
}
#else

static uint64_t Sqrt64(uint64_t n) {
  if (n == 0) return 0;
  uint64_t r = std::sqrt((double)n);
  return r - (r * r - 1 >= n);
}

#endif

// Slow decomposition into sums of squares two ways, for reference.
static optional<tuple<uint64_t,uint64_t,uint64_t,uint64_t>>
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

// Same for three ways.
static optional<tuple<uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t>>
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

static string WaysString(const std::vector<std::pair<uint64_t, uint64_t>> &v) {
  string out;
  for (const auto &[a, b] : v) {
    StringAppendF(&out, "%llu^2 + %llu^2, ", a, b);
  }
  return out;
}

// n ways.
static std::vector<std::pair<uint64_t, uint64_t>>
BruteGetNWays(uint64_t sum) {
  // Neither factor can be larger than the square root.
  // uint64_t limit_b = Sqrt64(sum);
  // while (limit_b * limit_b < sum) limit_b++;
  // Also, since a^2 + b^2 = sum, but a < b, a^2 can actually
  // be at most half the square root.
  uint64_t limit_a = Sqrt64(sum / 2);
  while (limit_a * limit_a < (sum / 2)) limit_a++;

  // XXX also since a < b, only need to go to half

  // with x^2 + y^2 == sum and x <= y, get y.
  // (Or return zero)
  auto GetOther = [sum](uint64_t x) -> uint64_t {
      uint64_t xx = x * x;
      uint64_t target = sum - xx;
      uint64_t y = Sqrt64(target);
      // Insist that the result is smaller than the
      // input, even if it would work. We find it the
      // other way. Try x = 7072 for sum = 100012225.
      if (y < x) return 0;
      if (y * y == target) return y;
      else return 0;
      #if 0
      for (uint64_t y = x; /* in loop */; y++) {
        const uint64_t yy = y * y;
        if (yy == target) {
          return y;
        } else if (yy > target) {
          return 0;
        }
      }
      return 0;
      #endif
    };

  // The way we ensure distinctness is that the pairs are ordered
  // a < b, and the search (and vector) is ordered by the first
  // element.
  std::vector<std::pair<uint64_t, uint64_t>> ret;
  for (uint64_t a = 0; a <= limit_a; a++) {
    if (uint64_t b = GetOther(a)) {
      ret.emplace_back(a, b);
    }
  }

  return ret;
}


// Some useful facts:
//  Must be in OEIS https://oeis.org/A004431, sum of two distinct squares.
//  OEIS https://oeis.org/A000161 gives the count of ways, so if
//  A000161[n] >= 2, it is in this set.
//
//  https://proofwiki.org/wiki/Sum_of_2_Squares_in_2_Distinct_Ways
//    if m and n are in the set, then m*n is in the set (except
//    possibly for some equivalences like a = b).
// https://users.rowan.edu/~hassen/Papers/SUM%20OF%20TWO%20SQUARES%20IN%20MORE%20THAN%20ONE%20WAY.pdf
//    If it's in the set, then it is the product of sums of squares.

// From note on OEIS by Chai Wah Wu, Sep 08 2022.
static int ChaiWahWu(uint64_t sum) {
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

static void TestCWW() {
  std::mutex m;
  int wrong = 0;
  Timer timer;
  static constexpr int START = 1000000;
  static constexpr int NUM = 1000000;
  ParallelComp(
      NUM,
      [&wrong, &m](int idx) {
        int i = START + idx;
        int num = ChaiWahWu(i);
        auto fo = ReferenceValidate2(i);
        auto ffo = ReferenceValidate3(i);

        if (ffo.has_value()) { CHECK(fo.has_value()); }

        bool correct = num >= 3 ? ffo.has_value() :
          num == 2 ? fo.has_value() && !ffo.has_value() :
          (!fo.has_value() && !ffo.has_value());
        if (!correct) {
          MutexLock ml(&m);
          wrong++;
        }
        bool print = !correct || num >= 8;
        if (print) {
          MutexLock ml(&m);
          printf("%s%d: %d%s", correct ? "" : ANSI_RED, i, num,
                 correct ? "" : ANSI_RESET);
          if (fo.has_value()) {
            if (ffo.has_value()) {
              auto [a, b, c, d, e, f] = ffo.value();
              printf(" = %llu^2 + %llu^2 = %llu^2 + %llu^2 = %llu^2 + %llu^2",
                     a, b, c, d, e, f);
            } else {
              auto [a, b, c, d] = fo.value();
              printf(" = %llu^2 + %llu^2 = %llu^2 + %llu^2", a, b, c, d);
            }
          }

          printf("\n");
        }
      }, 8);

  double sec = timer.Seconds();
  printf("Done in %s. (%s/ea.)\n",
         AnsiTime(sec).c_str(), AnsiTime(sec / NUM).c_str());
  if (wrong == 0) {
    printf(AGREEN("OK") "\n");
  } else {
    printf(ARED("%d") " wrong\n", wrong);
  }
}

// 264.3/sec -> 24401.7/sec :)
static void GenCWW() {
  // AutoParallelComp comp(16, 1000, true, "cww.autoparallel");

  std::mutex m;
  int triples = 0;
  Timer timer;
  static constexpr uint64_t START = 100'000'000;
  static constexpr uint64_t NUM   = 100'000'000;
  Periodically status_per(10.0);
  // comp.
    ParallelComp(
      NUM,
      [&triples, &status_per, &timer, &m](uint64_t idx) {
        uint64_t i = START + idx;
        // This one doesn't work, and we don't care.
        if (i == 0) return;

        int num = ChaiWahWu(i);
        std::vector<std::pair<uint64_t, uint64_t>> nways =
          BruteGetNWays(i);
        for (const auto &[a, b] : nways) {
          CHECK(a * a + b * b == i) << a << " " << b << " " << i;
        }
        CHECK((int)nways.size() == num)
          << "For sum " << i << ", CWW says "
          << num
          << " but got " << nways.size() << ":\n"
          << WaysString(nways);

        if (num > 3) {
          MutexLock ml(&m);
          triples++;
          if (status_per.ShouldRun()) {
            double pct = (triples * 100.0)/(double)idx;
            double sec = timer.Seconds();
            double nps = idx / sec;
            printf("%d/%llu (%.5f%%) are triples (%s) %.1f/sec\n",
                   triples, idx, pct, AnsiTime(sec).c_str(), nps);
          }
        }
      }, 6);

  double sec = timer.Seconds();
  printf("Total triples: %d/%llu\n", triples, NUM);
  printf("Done in %s. (%s/ea.)\n",
         AnsiTime(sec).c_str(), AnsiTime(sec / NUM).c_str());
}

// So now take numbers that can be written as sums of squares
// three ways: Z = B^2 + C^2 = D^2 + G^2 = E^2 + I^2
//
//  [a]  B   C
//
//   D   E  [f]
//
//   G  [h]  I
//
// This gives us the SUM = G + E + C, which then uniquely
// determines a, f, h (if they exist). Since the starting
// values were distinct, these residues are also distinct.
//
// The order of (B, C), (D, G), (E, I) matters, although there
// are some symmetries. We can req

// old, worse version
//
// So we have
//  A B C
//  D E F
//  G H I
//
// For some choice of A,
// we pick Z that can be written as the sum of squares three ways,
//  so Z = B^2 + C^2 = D^2 + G^2 = E^2 + I^2
// and then SUM = A^2 + Z.
//
//
// B/C, D/G, E/I can be swapped. (There are many symmetries, so we
// should perhaps say something like B<D? What would be the best
// constraint?) Given those, F and H are uniquely determined, if
// they exist.
//
// So basically if we have the list of Zs (and the ways of writing
// them as sums of squares), then we loop over all A, and then
// check the remainder of the conditions.

int main(int argc, char **argv) {
  AnsiInit();
  // cl = new CL;
  // DoSearch(0x0BADBEEF, 0x0123456, 0x0777777);
  // DoSearch(29, 47, 1);
  // TestCWW();
  GenCWW();

  return 0;
}

