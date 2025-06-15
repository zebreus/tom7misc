
#include "quad64.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <format>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "ansi.h"
#include "arcfour.h"
#include "atomic-util.h"
#include "auto-histo.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "factorization.h"
#include "periodically.h"
#include "threadutil.h"
#include "timer.h"

#include "sos-util.h"

// When computing F, number of bits we allow X and Y to be.
static constexpr int XY_BITS = 30;
static_assert(XY_BITS < 32);

using namespace std;

std::mutex file_mutex;

DECLARE_COUNTERS(count_any,
                 count_quad,
                 count_linear,
                 count_point,
                 count_recursive,
                 count_interesting,
                 count_none,
                 count_done);

static string CounterString() {
  return std::format(ABLUE("{}") " any "
                     AGREEN("{}") " quad "
                     APURPLE("{}") " lin "
                     ACYAN("{}") " pt "
                     AYELLOW("{}") " rec "
                     AGREY("{}") " none "
                     ARED("{}") " int",
                     count_any.Read(),
                     count_quad.Read(),
                     count_linear.Read(),
                     count_point.Read(),
                     count_recursive.Read(),
                     count_none.Read(),
                     count_interesting.Read());
}

static void RunGrid() {
  std::string seed = std::format("grid.{}", time(nullptr));
  ArcFour rc(seed);

  for (int i = 0; i < 80; i++)
    printf("\n");

  Periodically stats_per(5.0);
  Timer start_time;

  // Microseconds.
  std::mutex histo_mutex;
  AutoHisto auto_histo(100000);
  int64_t batches_done = 0;

  // old frontier, but it's probably better to be looking for
  // problems with much larger numbers.
  // static constexpr int64_t START_NUM = 39867870912;
  static constexpr int64_t START_NUM = 23432902782229;
  static constexpr int64_t MAX_NUM   = 64'000'000'000'000;
  static constexpr int64_t BATCH_SIZE = 32;

  static constexpr int64_t RANGE = MAX_NUM - START_NUM;

  ParallelComp(
      RANGE / BATCH_SIZE,
      [&](int64_t batch_idx) {

        std::vector<double> local_timing;
        for (int off = 0; off < BATCH_SIZE; off++) {
          const uint64_t f =
            START_NUM +
            (uint64_t)batch_idx * BATCH_SIZE +
            off;

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
                std::abort();
              }
            };

          Timer sol_timer;

          std::vector<std::pair<uint64_t, int>> factors =
            Factorization::Factorize(f);

          Solutions64 sols = SolveQuad64(f, factors);
          const double sol_ms = sol_timer.Seconds() * 1000.0;
          local_timing.push_back(sol_ms);

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

          for (const PointSolution64 &point : sols.points) {
            count_point++;
            Assert("point", point.X, point.Y);
          }

          if (sols.points.empty()) {
            count_none++;
          }

          const int found_sols = sols.points.size();

          // XXX pass factors!
          const int expected_sols = ChaiWahWu(f);

          CHECK(found_sols == expected_sols) << f;

          // Full batches
          count_done++;
        }

        {
          MutexLock ml(&histo_mutex);
          for (double d : local_timing) {
            auto_histo.Observe(d);
          }
          batches_done++;
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
              auto_histo.PrintSimpleANSI(HISTO_LINES);
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

  printf("Done in %s\n",
         ANSI::Time(start_time.Seconds()).c_str());
}



int main(int argc, char **argv) {
  ANSI::Init();

  RunGrid();

  return 0;
}
