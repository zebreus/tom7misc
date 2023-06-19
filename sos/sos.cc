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

#include "sos-util.h"

using namespace std;

// static CL *cl = nullptr;

using int64 = int64_t;


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

// 264.3/sec -> 244401.7/sec :)
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
        if (num > 3) {
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

          {
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

int main(int argc, char **argv) {
  AnsiInit();
  // cl = new CL;

  // TestCWW();
  GenCWW();

  return 0;
}

