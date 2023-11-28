
#include "quad.h"

#include <bit>
#include <array>
#include <string>
#include <cstdio>
#include <cstdint>

#include "base/logging.h"
#include "base/stringprintf.h"
#include "threadutil.h"
#include "bignum/big.h"
#include "bignum/big-overloads.h"
#include "timer.h"
#include "periodically.h"
#include "ansi.h"
#include "atomic-util.h"
#include "util.h"
#include "auto-histo.h"
#include "crypt/lfsr.h"
#include "arcfour.h"
#include "randutil.h"
#include "factorization.h"

#include "sos-util.h"

static constexpr int BITS = 45;
static constexpr uint64_t SEED = 0xCAFEBABE;

using namespace std;

DECLARE_COUNTERS(count_done,
                 count_interesting,
                 count_u2,
                 count_u3,
                 count_u4,
                 count_u5,
                 count_u6,
                 count_u7);

static string CounterString() {
  return StringPrintf(ABLUE("%lld") " done "
                      // AGREEN("%lld") " quad "
                      // APURPLE("%lld") " lin "
                      // ACYAN("%lld") " pt "
                      // AYELLOW("%lld") " rec "
                      // AGREY("%lld") " none "
                      ARED("%lld") " int",
                      count_done.Read(),
                      /*
                        count_quad.Read(),
                      count_linear.Read(),
                      count_point.Read(),
                      count_recursive.Read(),
                      count_none.Read(), */
                      count_interesting.Read()
                      );
}

static void RunGrid() {
  std::string seed = StringPrintf("grid.%lld", time(nullptr));
  ArcFour rc(seed);

  for (int i = 0; i < 80; i++)
    printf("\n");

  Periodically stats_per(5.0);
  Timer start_time;

  std::mutex file_mutex;
  std::mutex histo_mutex;

  AutoHisto histo_o1, histo_o2;
  AutoHisto histo_all;

  static constexpr int64_t START_NUM = int64_t(1) << BITS;
  static constexpr int64_t MAX_NUM   = START_NUM + 10'000'000;
  static constexpr int64_t BATCH_SIZE = 32;

  static constexpr int64_t RANGE = MAX_NUM - START_NUM;

  ParallelComp(
      RANGE / BATCH_SIZE,
      [&](int64_t batch_idx) {

        uint32_t s1 = 0xCAFEBABE;
        {
          for (uint64_t x = batch_idx; x > 0; x >>= 1) {
            if (x & 1) s1 = LFSRNext32(s1);
            s1 = std::rotl(s1, 31);
          }
        }
        uint32_t s2 = batch_idx;
        if (s2 == 0) s2++;

        std::vector<std::tuple<double, double>> local_max;
        local_max.reserve(BATCH_SIZE);

        for (int off = 0; off < BATCH_SIZE; off++) {

          s1 = LFSRNext32(s1);
          s2 = LFSRNext32(s2);
          const uint64_t r = (((uint64_t)s1) << 32) | s2;

          uint64_t f = r & ((int64_t(1) << BITS) - 1);
          if (f == 0) f++;

          auto Assert = [&](const char *type,
                            uint64_t x, uint64_t y) {
              uint64_t r = x * x + y * y;
              if (r != f) {
                std::string problem = StringPrintf("%lld", f);

                printf("\n\n\n\n\n"
                       "Invalid solution on problem: %s\n\n\n",
                       problem.c_str());
                fflush(stdout);

                printf("Solution was (%lld, %lld)\n"
                       "Of type %s.\n",
                       x, y,
                       type);
                printf("Want 0, but result was: %lld\n", r);
                fflush(stdout);

                printf("\n\n" ARED("Problem") ": %s\n\n\n",
                       problem.c_str());
                abort();
              }
            };

          std::vector<std::pair<uint64_t, int>> factors =
            Factorization::Factorize(f);

          Solutions sols = SolveQuad(f, factors);
          /*
          local_max.emplace_back(
              BigInt::LogBase2(sols.vsquared.max),
              BigInt::LogBase2(sols.o2.max));
          */

          if (sols.interesting_coverage) {
            count_interesting++;
            std::string problem = StringPrintf("%lld", f);
            printf("\n\n" APURPLE("Coverage!") " %s\n\n",
                   problem.c_str());
            MutexLock ml(&file_mutex);
            FILE *file = fopen("interesting-coverage.txt", "ab");
            CHECK(file != nullptr);
            fprintf(file, "%s\n", problem.c_str());
            fclose(file);
          }

          for (const PointSolution &point : sols.points) {
            Assert("point", point.X, point.Y);
          }

          const int found_sols = sols.points.size();

          const int expected_sols = ChaiWahWu(f);

          CHECK(found_sols == expected_sols) << f;

          count_done++;
        }

        {
          MutexLock ml(&histo_mutex);
          for (const auto &[o1, o2] : local_max) {
            histo_o1.Observe(o1);
            histo_o2.Observe(o2);

            for (double d : {o1, o2}) {
              histo_all.Observe(d);
            }
          }
        }

        stats_per.RunIf([&]() {
            int64_t done = count_done.Read();
            double sec = start_time.Seconds();
            double qps = done / sec;
            double spq = sec / done;
            std::string timing = StringPrintf(AWHITE("%.3f")
                                              " solved/sec (%s ea.) "
                                              AGREY(" Seed: [%s]"),
                                              qps,
                                              ANSI::Time(spq).c_str(),
                                              seed.c_str());
            std::string counters = CounterString();
            std::string bar =
              ANSI::ProgressBar(
                  done, RANGE,
                  StringPrintf("%lld ", START_NUM + batch_idx * BATCH_SIZE),
                  sec);

            static constexpr int STATUS_LINES = 3;
            static constexpr int HISTO_LINES = 20;

            for (int i = 0; i < (HISTO_LINES + STATUS_LINES); i++) {
              printf(ANSI_PREVLINE ANSI_BEGINNING_OF_LINE ANSI_CLEARLINE);
            }

            // Histo
            {
              MutexLock ml(&histo_mutex);
              histo_all.PrintSimpleANSI(HISTO_LINES);
            }

            printf("%s\n%s\n%s\n",
                   timing.c_str(),
                   counters.c_str(),
                   bar.c_str());
          });
        // printf("...\n");
      },
      12);

  printf("\n\n\n");

  std::string counters = CounterString();

  auto OutputHisto = [&](const string &file, const AutoHisto &ah) {
      string contents = ah.SimpleAsciiString(20);
      Util::WriteFile(file, contents);
      printf("Wrote " ABLUE("%s") "\n", file.c_str());
    };

  OutputHisto("histo-vsquared.txt", histo_o1);
  OutputHisto("histo-o2-bogus.txt", histo_o2);

  printf("Done in %s\n",
         ANSI::Time(start_time.Seconds()).c_str());
}

static void SimpleMaxValues() {

  for (int64_t base = 2; base < 31; base++) {

    uint64_t pow = 1;
    for (;;) {
      uint64_t next_pow = pow * base;
      if (next_pow <= pow) break;
      if (next_pow & (1ULL << 63)) break;
      pow = next_pow;

      std::vector<std::pair<uint64_t, int>> factors =
        Factorization::Factorize(pow);

      Solutions sol = SolveQuad(pow, factors);
      (void)sol;
    }

  }
  printf("OK\n");
}


static void ProductOfTwo() {
#if 0
  for (int64_t base = 2; base < 31; base++) {

    uint64_t pow = 1;
    for (;;) {
      uint64_t next_pow = pow * base;
      if (next_pow <= pow) break;
      if (next_pow & (1ULL << 63)) break;
      pow = next_pow;

      std::vector<std::pair<uint64_t, int>> factors =
        Factorization::Factorize(pow);

      Solutions sol = SolveQuad(pow, factors);
      (void)sol;
    }

  }
#endif
  printf("OK\n");
}

int main(int argc, char **argv) {
  ANSI::Init();

  SimpleMaxValues();

  ProductOfTwo();

  // RunGrid();

  return 0;
}
