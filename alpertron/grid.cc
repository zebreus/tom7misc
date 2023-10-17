
#include "quad.h"

#include <array>
#include <string>
#include <cstdio>
#include <cstdint>

#include "base/logging.h"
#include "threadutil.h"
#include "bignum/big.h"
#include "timer.h"
#include "periodically.h"
#include "ansi.h"

static constexpr int MAX_COEFF = 10;
// Positive and negative, zero
static constexpr int RADIX = MAX_COEFF * 2 + 1;

std::mutex file_mutex;

static void RunGrid() {
  Periodically stats_per(5.0);
  Timer start_time;

  std::array<int64_t, 6> dims = {
    RADIX, RADIX, RADIX,
    RADIX, RADIX, RADIX,
  };

  ParallelCompND<dims.size()>(
      dims,
      [&](const std::array<int64_t, 6> &arg) {
        // Center on 0.
        int64_t a = arg[0] - MAX_COEFF;
        int64_t b = arg[1] - MAX_COEFF;
        int64_t c = arg[2] - MAX_COEFF;
        int64_t d = arg[3] - MAX_COEFF;
        int64_t e = arg[4] - MAX_COEFF;
        int64_t f = arg[5] - MAX_COEFF;

        printf("do %lld %lld %lld %lld %lld %lld\n",
               a, b, c, d, e, f);

        BigInt A(a);
        BigInt B(b);
        BigInt C(c);
        BigInt D(d);
        BigInt E(e);
        BigInt F(f);

        Solutions sols =
          QuadBigInt(A, B, C, D, E, F, nullptr);

        if (sols.interesting_coverage) {
          printf(APURPLE("Coverage!")
                 " %lld %lld %lld %lld %lld %lld\n",
                 a, b, c, d, e, f);
          MutexLock ml(&file_mutex);
          FILE *file = fopen("interesting-coverage.txt\n", "ab");
          CHECK(file != nullptr);
          fprintf(file, "%lld %lld %lld %lld %lld %lld\n",
                  a, b, c, d, e, f);
          fclose(file);
        }

        // XXX Check solutions.
        stats_per.RunIf([&]() {
            printf("%lld %lld %lld %lld %lld %lld in %s\n",
                   a, b, c, d, e, f,
                   ANSI::Time(start_time.Seconds()).c_str());
          });
        printf("...\n");
      },
      1);

  printf("Done in %s\n",
         ANSI::Time(start_time.Seconds()).c_str());
}



int main(int argc, char **argv) {
  ANSI::Init();

  RunGrid();

  return 0;
}
