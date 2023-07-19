
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
#include "arcfour.h"
#include "randutil.h"

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

#define CHECK_SQUARED(r) do {                                       \
    uint64_t rr = r * r;                                            \
    auto ro = Sqrt64Opt(rr);                                        \
    CHECK(ro.has_value()) << r << " " << rr;                        \
    CHECK(ro.value() == r) << r << " " << rr << " " << ro.value();  \
  } while (0)

static void TestSqrtOpt() {
  CHECK_SQUARED(0);
  CHECK_SQUARED(1);
  CHECK_SQUARED(2);
  CHECK_SQUARED(3);

  CHECK_SQUARED(0xFFFFFFFEULL);
  CHECK_SQUARED(0xFFFFFFFDULL);
  CHECK_SQUARED(0xFFFFFFFCULL);
  CHECK_SQUARED(0xF0000000ULL);
  CHECK_SQUARED(0xF0000001ULL);
  CHECK_SQUARED(0xEFFFFFFFULL);

  ArcFour rc("sqrtopt");
  for (int i = 0; i < 1000000; i++) {
    // numbers in the low trillions, at most
    const uint64_t num = Rand64(&rc) & 0xFFFFFFFFFF;
    auto no = Sqrt64Opt(num);
    if (no.has_value()) {
      CHECK(no.value() * no.value() == num) << num << " " << no.value();
    }
  }

  printf("TestSqrtOpt " AGREEN("OK") "\n");
}

static void BenchMSOSFancy() {
  printf("Benchmark MaybeSumOfSquaresFancy3:\n");
  ArcFour rc("bench");
  int64_t res = 0;
  static constexpr int NUM = 400000000;
  Timer timer;
  for (int i = 0; i < NUM; i++) {
    // numbers in the low trillions, at most
    const uint64_t num = Rand64(&rc) & 0xFFFFFFFFFF;
    if (num == 0) continue;
    res += MaybeSumOfSquaresFancy3(num);
  }
  printf("Res: %llx\n", res);
  double sec = timer.Seconds();
  printf("Took %s (%s/ea)\n",
         ANSI::Time(sec).c_str(),
         ANSI::Time(sec / NUM).c_str());
}

static void BenchCWW() {
  printf("Benchmark CWW:\n");
  ArcFour rc("bench");
  int64_t res = 0;
  static constexpr int NUM = 20000000;
  Timer timer;
  for (int i = 0; i < NUM; i++) {
    // numbers in the low trillions, at most
    const uint64_t num = Rand64(&rc) & 0xFFFFFFFFFF;
    if (num == 0) continue;

    const int ways = ChaiWahWu(num);
    res += ways;
  }
  printf("Res: %llx\n", res);
  double sec = timer.Seconds();
  printf("Took %s (%s/ea)\n",
         ANSI::Time(sec).c_str(),
         ANSI::Time(sec / NUM).c_str());
}

// XXX this test is pretty weak, since we check against only
// the 2-way and 3-way reference code. TestCWW2 is just superior?
static void TestCWW() {
  std::mutex m;
  int wrong = 0;
  Timer timer;
  static constexpr int START = 1000000;
  static constexpr int NUM =    100000;
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

static void TestCWWBrute() {
  std::mutex m;
  int wrong = 0;
  Timer timer;
  Periodically bar(1.0);
  static constexpr int START = 10000000;
  static constexpr int NUM =   10000000;
  ParallelComp(
      NUM,
      [&timer, &bar, &wrong, &m](int idx) {
        int sum = START + idx;
        int num = ChaiWahWu(sum);
        auto ways = BruteGetNWays(sum);

        CHECK(ways.size() == num) <<
          sum << " " << ways.size() << " " << num;

        if (bar.ShouldRun()) {
          printf(ANSI_PREVLINE ANSI_BEGINNING_OF_LINE ANSI_CLEARLINE
                 ANSI_BEGINNING_OF_LINE "%s\n",
                 ANSI::ProgressBar(idx,
                                   NUM,
                                   "test cww vs brute",
                                   timer.Seconds()).c_str());
        }
      }, 8);

  double sec = timer.Seconds();
  printf("Done in %s. (%s/ea.)\n",
         ANSI::Time(sec).c_str(), ANSI::Time(sec / NUM).c_str());
  printf("CWW Brute " AGREEN("OK") "\n");
}


static constexpr bool CHECK_RESULT = false;
// 264.3/sec -> 244401.7/sec -> 1040228/sec :)
template<class F>
static void TestGetWays(const char *name, F f) {
  printf(AWHITE(" == ") APURPLE("%s") AWHITE(" == ") "\n",
         name);
  std::mutex m;
  int triples = 0;
  Timer timer;
  static constexpr uint64_t START = 100'000'000'000; /* ' */
  static constexpr uint64_t NUM   =   1'000;
  static constexpr uint64_t ROLL  =  10'000;
  Periodically status_per(5.0);
  ParallelComp(
    NUM,
    [&f, &triples, &status_per, &timer, &m](uint64_t major_idx) {
      uint64_t local_triples = 0;
      for (int minor_idx = 0; minor_idx < ROLL; minor_idx++) {
        uint64_t i = START + major_idx * ROLL + minor_idx;

        // This one doesn't work, and we don't care.
        if (i == 0) continue;

        int num = ChaiWahWu(i);
        if (num > 3) {
          local_triples++;
          // Note: We can pass num to make this faster, but
          // in a test it makes sense to check that we don't
          // get *too many*.
          std::vector<std::pair<uint64_t, uint64_t>> nways =
            f(i, -1);

          if (CHECK_RESULT) {
            for (const auto &[a, b] : nways) {
              CHECK(a * a + b * b == i) << a << " " << b << " " << i;
            }
            CHECK((int)nways.size() == num)
              << "For sum " << i << ", CWW says "
              << num
              << " but got " << nways.size() << ":\n"
              << WaysString(nways);
            std::sort(nways.begin(), nways.end(),
                      [](const std::pair<uint64_t, uint64_t> &x,
                         const std::pair<uint64_t, uint64_t> &y) {
                        return x.first < y.first;
                      });
            // Check uniqueness.
            for (int x = 1; x < nways.size(); x++) {
              CHECK(nways[x] != nways[x - 1]) << "Duplicates: " <<
                nways[x].first << " " << nways[x].second;
            }
          }
        }
      }

      {
        MutexLock ml(&m);
        triples += local_triples;
      }

      if (status_per.ShouldRun()) {
        int64_t done = major_idx * ROLL;
        double pct = (triples * 100.0)/(double)done;
        double sec = timer.Seconds();
        double nps = done / sec;
        printf("%d/%llu (%.5f%%) are triples (%s) %.1f/sec\n",
               triples, done, pct, ANSI::Time(sec).c_str(), nps);
      }
    }, 6);

  double sec = timer.Seconds();
  constexpr int64_t TOTAL = NUM * ROLL;
  printf("[%s] Total triples: %d/%llu\n", name, triples, TOTAL);
  printf("Done in %s. (%s/ea.)\n",
         ANSI::Time(sec).c_str(), ANSI::Time(sec / TOTAL).c_str());
  printf(" = %.1f/sec\n", TOTAL / sec);
}

template<class F>
static void TestSimple(const char * name, F f) {
  for (int i = 2; i < 120; i++) {
    int num = ChaiWahWu(i);
    std::vector<std::pair<uint64_t, uint64_t>> nways_fast =
      f(i, num);
    std::vector<std::pair<uint64_t, uint64_t>> nways =
      f(i, -1);
    CHECK(nways_fast.size() == nways.size());
    CHECK(num == (int)nways.size());
    printf("[%s] %d: %d ways: %s\n",
           name, i, num, WaysString(nways).c_str());
    for (const auto &[a, b] : nways_fast) {
      CHECK(a * a + b * b == i);
    }
  }
}

static void TestMaybeSumOfSquares() {
  Timer timer;
  Periodically bar(1.0);
  std::mutex m;
  static constexpr int MAX_X = 100000;
  printf("MaybeSumOfSquares...\n\n");
  ParallelComp(
      MAX_X,
      [&m, &bar, &timer](uint64_t x) {
        uint64_t xx = x * x;
        for (uint64_t y = 0; y < 100000; y++) {
          uint64_t sum = xx + (y * y);
          CHECK(MaybeSumOfSquaresFancy4(sum));
        }

        if (bar.ShouldRun()) {
          printf(ANSI_PREVLINE ANSI_BEGINNING_OF_LINE ANSI_CLEARLINE
                 ANSI_BEGINNING_OF_LINE "%s\n",
                 ANSI::ProgressBar(x,
                                   MAX_X,
                                   "test",
                                   timer.Seconds()).c_str());
        }
      },
      6);
  printf("MaybeSumOfSquares " AGREEN("OK") "\n");
}

static void MaybeSumOfSquaresRecall() {
  Timer timer;
  Periodically bar(1.0);
  std::mutex m;
  static constexpr int BATCHES = 50000;
  int64_t no_ways = 0;
  int64_t detected = 0;
  int64_t detected2 = 0;
  int64_t total = 0;
  ParallelComp(
      BATCHES,
      [&m, &bar, &timer, &no_ways, &detected, &detected2,
       &total](uint64_t batch) {
        int64_t local_no_ways = 0, local_detected = 0,
          local_detected2 = 0, local_total = 0;
        ArcFour rc(StringPrintf("mss.%llu", batch));
        for (int i = 0; i < 10000; i++) {
          // numbers in the low trillions, at most
          const uint64_t num = Rand64(&rc) & 0xFFFFFFFFFF;
          if (num == 0) continue;

          const bool maybe = MaybeSumOfSquaresFancy3(num);
          const bool maybe2 = MaybeSumOfSquaresFancy4(num);
          const int ways = ChaiWahWu(num);
          local_total++;

          if (ways > 0) {
            // This is supposed to be guaranteed.
            CHECK(maybe) << num;
            CHECK(maybe2) << num;
          } else {
            // Did we detect this case?
            local_no_ways++;
            if (!maybe) local_detected++;
            if (!maybe2) local_detected2++;
          }
        }

        {
          MutexLock ml(&m);
          no_ways += local_no_ways;
          detected += local_detected;
          detected2 += local_detected2;
          total += local_total;

          if (bar.ShouldRun()) {
            printf(ANSI_PREVLINE ANSI_BEGINNING_OF_LINE ANSI_CLEARLINE
                   ANSI_BEGINNING_OF_LINE "%s\n",
                   ANSI::ProgressBar(batch,
                                     BATCHES,
                                     "computing recall",
                                     timer.Seconds()).c_str());
          }
        }
      },
      6);

  printf("\n\n"
         "Took: %s\n"
         "Total:  %lld\n"
         "0 ways: %lld (%.2f%%)\n"
         "correctly detected by sieve 1: %lld/%lld (%.2f%%)\n"
         "correctly detected by sieve 2: %lld/%lld (%.2f%%)\n",
         ANSI::Time(timer.Seconds()).c_str(),
         total,
         no_ways, (no_ways * 100.0) / total,
         detected, no_ways, (detected * 100.0) / no_ways,
         detected2, no_ways, (detected2 * 100.0) / no_ways);

  printf("So full runs go from %lld to %lld (%.2f%%)\n",
         (total - detected),
         (total - detected2),
         ((total - detected2) * 100.0) / (total - detected));
}

static void TestMaybe() {
  Timer timer;
  Periodically bar(1.0);
  std::mutex m;
  static constexpr int BATCHES = 50000;
  ParallelComp(
      BATCHES,
      [&m, &bar, &timer](uint64_t batch) {
        ArcFour rc(StringPrintf("testmaybe.%llu", batch));
        for (int i = 0; i < 10000; i++) {
          // numbers in the low trillions, at most
          const uint64_t num = Rand64(&rc) & 0xFFFFFFFFFF;
          if (num == 0) continue;

          const bool maybe = MaybeSumOfSquaresFancy4(num);
          if (!maybe) {
            const int ways = ChaiWahWuNoFilter(num);
            CHECK(ways == 0) << num << ": " << ways;
          }
        }

        if (bar.ShouldRun()) {
          printf(ANSI_PREVLINE ANSI_BEGINNING_OF_LINE ANSI_CLEARLINE
                 ANSI_BEGINNING_OF_LINE "%s\n",
                 ANSI::ProgressBar(batch,
                                   BATCHES,
                                   "computing recall",
                                   timer.Seconds()).c_str());
        }
      },
      6);
  printf("TestMaybe " AGREEN("OK") "\n");
}

int main(int argc, char **argv) {
  ANSI::Init();

  TestCWWBrute();
  TestCWW();
  BenchCWW();

  // TestMaybeSumOfSquares();
  // TestMaybe();

  // TestSqrtOpt();


  // BenchMSOSFancy();
  // MaybeSumOfSquaresRecall();

  // TestCWW();

  /*
  TestSimple("brute", BruteGetNWays);
  TestSimple("nsoks2", NSoks2);
  TestSimple("merge", GetWaysMerge);

  TestGetWays("brute", BruteGetNWays);
  TestGetWays("nsoks2", NSoks2);
  TestGetWays("merge", GetWaysMerge);
  */

#if 0
#define TRY(x) if ((x & 3) != 3) printf("TRY(%d);\n", x);
  TRY(3);
  TRY(5);
  TRY(7);
  TRY(11);
  TRY(13);
  TRY(17);
  TRY(19);
  TRY(23);
  TRY(29);
  TRY(31);
  TRY(37);
  TRY(41);
  TRY(43);
  TRY(47);
  TRY(53);
  TRY(59);
  TRY(61);
  TRY(67);
  TRY(71);
  TRY(73);
  TRY(79);
  TRY(83);
  TRY(89);
  TRY(97);
  TRY(101);
  TRY(103);
  TRY(107);
  TRY(109);
  TRY(113);
  TRY(127);
  TRY(131);
#endif


  printf("OK\n");
  return 0;
}
