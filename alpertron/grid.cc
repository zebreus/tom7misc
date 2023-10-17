
#include "quad.h"

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

static constexpr int MAX_COEFF = 6;
// Positive and negative, zero
static constexpr int RADIX = MAX_COEFF * 2 + 1;

using namespace std;

std::mutex file_mutex;

DECLARE_COUNTERS(count_any,
                 count_quad,
                 count_linear,
                 count_point,
                 count_recursive,
                 count_interesting,
                 count_none,
                 u2_);

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
  Periodically stats_per(5.0);
  Timer start_time;

  std::array<int64_t, 6> dims = {
    RADIX, RADIX, RADIX,
    RADIX, RADIX, RADIX,
  };

  ParallelCompND<dims.size()>(
      dims,
      [&](const std::array<int64_t, 6> &arg,
          int64_t idx, int64_t total) {
        // Center on 0.
        int64_t a = arg[0] - MAX_COEFF;
        int64_t b = arg[1] - MAX_COEFF;
        int64_t c = arg[2] - MAX_COEFF;
        int64_t d = arg[3] - MAX_COEFF;
        int64_t e = arg[4] - MAX_COEFF;
        int64_t f = arg[5] - MAX_COEFF;

        /*
        printf("do %lld %lld %lld %lld %lld %lld\n",
               a, b, c, d, e, f);
        */

        BigInt A(a);
        BigInt B(b);
        BigInt C(c);
        BigInt D(d);
        BigInt E(e);
        BigInt F(f);

        auto Assert = [&](const char *type,
                          const BigInt &x, const BigInt &y) {
            BigInt r =
              A * (x * x) +
              B * (x * y) +
              C * (y * y) +
              D * x +
              E * y +
              F;
            CHECK(r == 0) << "\n\n\nInvalid solution (" << x.ToString()
                          << ", " << y.ToString() << ") of type " << type
                          << ".\nWant 0; result was: " << r.ToString()
                          << "\nProblem: "
                          << A.ToString() << " "
                          << B.ToString() << " "
                          << C.ToString() << " "
                          << D.ToString() << " "
                          << E.ToString() << " "
                          << F.ToString() << "\n\n\n\n";
          };

        Solutions sols =
          QuadBigInt(A, B, C, D, E, F, nullptr);

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
        }

        stats_per.RunIf([&]() {
            std::string counters = CounterString();
            std::string bar =
              ANSI::ProgressBar(
                  idx, total,
                  StringPrintf("%lld %lld %lld %lld %lld %lld ",
                               a, b, c, d, e, f),
                  start_time.Seconds());
            printf(ANSI_PREVLINE ANSI_BEGINNING_OF_LINE ANSI_CLEARLINE
                   ANSI_PREVLINE ANSI_BEGINNING_OF_LINE ANSI_CLEARLINE
                   "%s\n%s\n",
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
