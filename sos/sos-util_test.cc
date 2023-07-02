
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
         ANSI::Time(sec).c_str(), ANSI::Time(sec / NUM).c_str());
  if (wrong == 0) {
    printf(AGREEN("OK") "\n");
  } else {
    printf(ARED("%d") " wrong\n", wrong);
    CHECK(false) << "Failed";
  }
}

// 264.3/sec -> 244401.7/sec -> 1040228/sec :)
template<class F>
static void TestGetWays(const char *name, F f) {
  std::mutex m;
  int triples = 0;
  Timer timer;
  static constexpr uint64_t START = 100'000'000'000;
  static constexpr uint64_t NUM   =  10'000'000;
  Periodically status_per(5.0);
  ParallelComp(
    NUM,
    [&f, &triples, &status_per, &timer, &m](uint64_t idx) {
      uint64_t i = START + idx;
      // This one doesn't work, and we don't care.
      if (i == 0) return;

      int num = ChaiWahWu(i);
      if (num > 3) {
        // Note: We can pass num to make this faster, but
        // in a test it makes sense to check that we don't
        // get *too many*.
        std::vector<std::pair<uint64_t, uint64_t>> nways =
          f(i, -1);
#if 0
        for (const auto &[a, b] : nways) {
          CHECK(a * a + b * b == i) << a << " " << b << " " << i;
        }
        CHECK((int)nways.size() == num)
          << "For sum " << i << ", CWW says "
          << num
          << " but got " << nways.size() << ":\n"
          << WaysString(nways);
#endif

        {
          MutexLock ml(&m);
          triples++;
          if (status_per.ShouldRun()) {
            double pct = (triples * 100.0)/(double)idx;
            double sec = timer.Seconds();
            double nps = idx / sec;
            printf("%d/%llu (%.5f%%) are triples (%s) %.1f/sec\n",
                   triples, idx, pct, ANSI::Time(sec).c_str(), nps);
          }
        }
      }
    }, 6);

  double sec = timer.Seconds();
  printf("[%s] Total triples: %d/%llu\n", name, triples, NUM);
  printf("Done in %s. (%s/ea.)\n",
         ANSI::Time(sec).c_str(), ANSI::Time(sec / NUM).c_str());
  printf(" = %.1f/sec\n", NUM / sec);
}

static void TestSimple() {
  for (int i = 2; i < 60; i++) {
    int num = ChaiWahWu(i);
    std::vector<std::pair<uint64_t, uint64_t>> nways_fast =
      BruteGetNWays(i, num);
    std::vector<std::pair<uint64_t, uint64_t>> nways =
      BruteGetNWays(i);
    CHECK(nways_fast.size() == nways.size());
    CHECK(num == (int)nways.size());
    printf("%d: %d ways: %s\n", i, num, WaysString(nways).c_str());
  }
}

int main(int argc, char **argv) {
  ANSI::Init();

  #if 0
  for (int i = 50; i < 60; i++) {
    printf("%d: ", i);
    std::vector<std::pair<uint64_t, uint64_t>> nways_brute =
      BruteGetNWays(i);
    std::vector<std::pair<uint64_t, uint64_t>> nways =
      NSoks2(i);
    int num = ChaiWahWu(i);
    CHECK(num == (int)nways.size()) << num << " vs " << nways.size() << "\n"
                                    << WaysString(nways) << "\nBrute: "
                                    << WaysString(nways_brute);
    printf("%d ways: %s\n", num, WaysString(nways).c_str());
  }

  return 0;
  #endif

  // TestSimple();
  // TestCWW();
  TestGetWays("brute", BruteGetNWays);
  TestGetWays("nsoks2", NSoks2);

  printf("OK\n");
  return 0;
}
