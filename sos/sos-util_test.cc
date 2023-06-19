
#include "sos-util.h"

#include <optional>
#include <tuple>
#include <cstdio>
#include <cstdint>
#include <mutex>

#include "ansi.h"
#include "threadutil.h"
#include "periodically.h"
#include "timer.h"
#include "base/logging.h"
#include "base/stringprintf.h"

using namespace std;

// TODO: Test sqrt against this
// https://www.nuprl.org/MathLibrary/integer_sqrt/
static uint64_t Sqrt64Nuprl(uint64_t xx) {
  if (xx <= 1) return xx;
  // z = xx / 4
  uint64_t z = xx >> 2;
  uint64_t r2 = 2 * Sqrt64Nuprl(z);
  uint64_t r3 = r2 + 1;
  return (xx < r3 * r3) ? r2 : r3;
}


static void TestCWW() {
  std::mutex m;
  int wrong = 0;
  Timer timer;
  static constexpr int START = 1000000;
  static constexpr int NUM   =  100000;
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
        bool print = !correct || num >= 16;
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
    CHECK(false) << "Failed";
  }
}

// 264.3/sec -> 244401.7/sec :)
static void TestBruteN() {
  std::mutex m;
  int triples = 0;
  Timer timer;
  static constexpr uint64_t START = 100'000'000;
  static constexpr uint64_t NUM   = 100'000'00;
  Periodically status_per(10.0);
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


int main(int argc, char **argv) {
  AnsiInit();
  TestCWW();
  TestBruteN();

  printf("OK\n");
  return 0;
}
