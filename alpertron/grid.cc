
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

static constexpr int MAX_COEFF = 12;
// Positive and negative, zero
static constexpr int RADIX = MAX_COEFF * 2 + 1;

static constexpr bool COMPUTE_F = true;

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
  return StringPrintf(ABLUE("%lld") " any "
                      AGREEN("%lld") " quad "
                      APURPLE("%lld") " lin "
                      ACYAN("%lld") " pt "
                      AYELLOW("%lld") " rec "
                      AGREY("%lld") " none "
                      ARED("%lld") " int",
                      count_any.Read(),
                      count_quad.Read(),
                      count_linear.Read(),
                      count_point.Read(),
                      count_recursive.Read(),
                      count_none.Read(),
                      count_interesting.Read());
}

static void RunGrid() {
  for (int i = 0; i < 80; i++)
    printf("\n");

  Periodically stats_per(5.0);
  Timer start_time;

  std::array<int64_t, 5> dims = {
    RADIX, RADIX, RADIX,
    RADIX, RADIX, /* f in loop */
  };

  // Microseconds.
  std::mutex histo_mutex;
  AutoHisto auto_histo(100000);
  int64_t batches_done = 0;

  ParallelCompND(
      dims,
      [&](const std::array<int64_t, 5> &arg,
          int64_t idx, int64_t batches_total) {
        // Center on 0.
        // ... except a, since we already ran -10 to 0
        // int64_t a = arg[0] + 29; // - MAX_COEFF;
        // int64_t a = arg[0] - MAX_COEFF;
        // int64_t a = 24 + arg[0];
        // int64_t b = arg[1] - MAX_COEFF;
        // int64_t c = arg[2] - MAX_COEFF;
        // int64_t d = arg[3] - MAX_COEFF;
        // int64_t e = arg[4] - MAX_COEFF;

        int64_t a = 32 + arg[0];
        int64_t b = arg[1] - MAX_COEFF;
        int64_t c = arg[2] - MAX_COEFF;
        int64_t d = arg[3] - MAX_COEFF;
        int64_t e = arg[4] - MAX_COEFF;

        // Known bad problems.
        if (a == 24 && b == -6 && c == -12 && d == -4 && e == 5)
          return;

        BigInt A(a);
        BigInt B(b);
        BigInt C(c);
        BigInt D(d);
        BigInt E(e);

        const uint64_t hash64 = (uint64_t)a ^
          std::rotl((uint64_t)b, 51) ^
          std::rotl((uint64_t)c, 17) ^
          (std::rotl((uint64_t)d, 33) +
           std::rotl((uint64_t)e, 23));

        uint32_t hash_hi = (uint32_t)(hash64 >> 32);
        if (!hash_hi) hash_hi++;
        uint32_t hash_lo = (uint32_t)hash64;
        if (!hash_lo) hash_lo--;

        std::vector<double> local_timing;

        for (int64 f = -MAX_COEFF; f <= MAX_COEFF; f++) {
          int sol_x = -1, sol_y = -1;

          BigInt F;

          if (COMPUTE_F) {
            // Pick x and y "randomly"
            hash_hi = LFSRNext32((uint32_t)(hash_hi));
            hash_lo = LFSRNext32((uint32_t)(hash_lo));

            sol_x = (int)(hash_hi & 65535) - 32768;
            sol_y = (int)(hash_lo & 65535) - 32768;

            // TODO: Insist that we find *this* solution below.
            BigInt X(sol_x), Y(sol_y);

            BigInt NegF =
              A * (X * X) +
              B * (X * Y) +
              C * (Y * Y) +
              D * X +
              E * Y;

            F = -NegF;

          } else {
            F = BigInt(f);
          }

          auto Assert = [&](const char *type,
                            const BigInt &x, const BigInt &y) {
              BigInt r =
                A * (x * x) +
                B * (x * y) +
                C * (y * y) +
                D * x +
                E * y +
                F;
              if (r != 0) {
                std::string problem =
                  StringPrintf(" %s %s %s %s %s %s\n\n",
                               A.ToString().c_str(),
                               B.ToString().c_str(),
                               C.ToString().c_str(),
                               D.ToString().c_str(),
                               E.ToString().c_str(),
                               F.ToString().c_str());

                printf("\n\n\n\n\n"
                       "Invalid solution on problem: %s\n\n\n",
                       problem.c_str());
                fflush(stdout);

                printf("Solution was (%s, %s)\n"
                       "Of type %s.\n",
                       x.ToString().c_str(),
                       y.ToString().c_str(),
                       type);
                printf("Want 0, but result was: %s\n",
                       r.ToString().c_str());
                fflush(stdout);

                printf("\n\n" ARED("Problem") ": %s\n\n\n",
                       problem.c_str());
                abort();
              }
            };

          Timer sol_timer;
          Solutions sols =
            QuadBigInt(A, B, C, D, E, F, nullptr);
          const double sol_usec = sol_timer.Seconds() * 1000000.0;
          local_timing.push_back(sol_usec);

          if (sols.interesting_coverage) {
            count_interesting++;
            printf("\n\n" APURPLE("Coverage!")
                   " %lld %lld %lld %lld %lld %lld\n\n",
                   a, b, c, d, e, f);
            MutexLock ml(&file_mutex);
            FILE *file = fopen("interesting-coverage.txt\n", "ab");
            CHECK(file != nullptr);
            fprintf(file, "%lld %lld %lld %lld %lld %lld\n",
                    a, b, c, d, e, f);
            fclose(file);
          }

          // Check solutions.
          if (sols.any_integers) {
            count_any++;
            Assert("any", BigInt(3), BigInt(7));
            Assert("any", BigInt(-31337), BigInt(27));
          }

          for (const LinearSolution &linear : sols.linear) {
            count_linear++;

            Assert("linear",
                   linear.MX * BigInt(0) + linear.BX,
                   linear.MY * BigInt(0) + linear.BY);

            Assert("linear",
                   linear.MX * BigInt(11) + linear.BX,
                   linear.MY * BigInt(11) + linear.BY);

            Assert("linear",
                   linear.MX * BigInt(-27) + linear.BX,
                   linear.MY * BigInt(-27) + linear.BY);
          }

          for (const PointSolution &point : sols.points) {
            count_point++;
            Assert("point", point.X, point.Y);
          }

          for (const QuadraticSolution &quad : sols.quadratic) {
            count_quad++;

            Assert("quad",
                   quad.VX * BigInt(0) + quad.MX * BigInt(0) + quad.BX,
                   quad.VY * BigInt(0) + quad.MY * BigInt(0) + quad.BY);

            Assert("quad",
                   quad.VX * BigInt(9) + quad.MX * BigInt(-3) + quad.BX,
                   quad.VY * BigInt(9) + quad.MY * BigInt(-3) + quad.BY);

            Assert("quad",
                   quad.VX * BigInt(100) + quad.MX * BigInt(10) + quad.BX,
                   quad.VY * BigInt(100) + quad.MY * BigInt(10) + quad.BY);
          };

          // TODO: Test recursive!
          for (const std::pair<RecursiveSolution,
                 RecursiveSolution> &rec : sols.recursive) {
            count_recursive++;
            (void)rec;
          }

          if (!sols.any_integers &&
              sols.linear.empty() &&
              sols.points.empty() &&
              sols.quadratic.empty() &&
              sols.recursive.empty()) {
            count_none++;

            if (COMPUTE_F) {
              std::string problem =
                StringPrintf(" %s %s %s %s %s %s\n\n",
                             A.ToString().c_str(),
                             B.ToString().c_str(),
                             C.ToString().c_str(),
                             D.ToString().c_str(),
                             E.ToString().c_str(),
                             F.ToString().c_str());

              printf("\n\n\n\n\n"
                     "Did not solve problem: %s\n\n\n",
                     problem.c_str());
              fflush(stdout);

              printf("Even though it should have a solution (%d, %d)\n\n\n",
                     sol_x, sol_y);

              printf("\n\n" ARED("Problem") ": %s\n\n\n",
                     problem.c_str());
              abort();

            }
          }

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
            std::string timing = StringPrintf("%.3f solved/sec (%s ea.)",
                                              qps,
                                              ANSI::Time(spq).c_str());
            std::string counters = CounterString();
            std::string bar =
              ANSI::ProgressBar(
                  batches_done, batches_total,
                  StringPrintf("%lld %lld %lld %lld %lld * ",
                               a, b, c, d, e),
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
