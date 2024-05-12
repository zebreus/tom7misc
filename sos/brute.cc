
#include "ansi.h"

#include <set>
#include <unordered_set>
#include <cstdint>
#include <cstdio>
#include <mutex>

#include "base/logging.h"
#include "sos-util.h"
#include "periodically.h"
#include "timer.h"
#include "threadutil.h"
#include "base/stringprintf.h"

#include "interval-cover.h"

static constexpr bool SELF_CHECK = false;

// Dead-simple exhaustive search!
// Everything must be an actual magic square, but some of the
// cells can be non-squares. We're looking for a small
// total error.

// jcreed showed me a nice way of enumerating magic squares.
// If you have any nondegenerate tuple of integers <n, x, y>
// then this gives a magic square:
//
//  n+2x+y  n       n+x+2y
//  n+2y    n+x+y   n+2x
//  n+x     n+2x+2y n+y
//
// So we can loop over (n,x,y), which is a much smaller search
// space than looping over 9 cells!
// (Do we need to consider negative y, z?)
//
// We know there are no proper squares of squares with small
// coefficients like this, but we're hoping to minimize the
// error from squares.

inline std::array<int, 9> ProgSquare(int n, int x, int y) {
  return {
    n + 2 * x + y,  n,                  n + x + 2 * y,
    n + 2 * y,      n + x + y,          n + 2 * x,
    n + x,          n + 2 * x + 2 * y,  n + y};
}

static void Brute() {

  [[maybe_unused]]
  constexpr int best[8] = {
    // best with 0 non-squares
    999999999,
    // best with 1 non-square
    999999999,
    // best with 2 non-squares
    333,
    // best with 3 non-squares
    25,
    // best with 4 non-squares
    15,
    // best with 5 non-squares
    5,
    // best with 6 non-squares
    6,
    // best with 7 non-squares
    9,
  };

  // Threshold to show
  constexpr int show[8] = {
    // best with 0 non-squares
    999999999,
    // best with 1 non-square
    999999999,
    // best with 2 non-squares
    500,
    // best with 3 non-squares
    25,
    // best with 4 non-squares
    15,
    // best with 5 non-squares
    5,
    // best with 6 non-squares
    6,
    // best with 7 non-squares
    9,
  };

  Periodically status_per(5.0);
  int64_t done_bases = 0;
  Timer run_timer;

  std::mutex m;

  // Already did base = 0..32768, y=2..32768, x = 1..y.
  // We can say that for a pair (base, y) to be done, we always
  // do every x in [1, y). That makes this a 2D representation.
  // And then we can store that as an interval tree, where
  // the key is the base, and the value indicates that we've
  // tried y [0, value).

  IntervalCover<int> cover(0);
  // TODO: Save/load from file

  // Ran this on 28-29 Feb 2024.
  // cover.SetSpan(0, 32768, std::make_pair(0, 32768));

  // And then I weirdly ran y [32768, 65536) for base 0-65536,
  // finishing on 6 Mar 2024.
  // Then I ran the gap I left, y [0, 32768) for base [32768, 65536),
  // finishing on 7 Mar 2024.
  // Then did 65536-131072 from from 0 to 65536 through March 13.
  cover.SetSpan(0, 131072, 65536);

  // 500 hour run completed 9 Apr 2024.
  cover.SetSpan(0, 16384, 262144);

  // Running this starting 15 Mar 2024...
  // cover.SetSpan(0, 16384, 262144);

  std::set<int> in_progress;
  const int base_start = 0, base_end = 16384;
  // const int y_start = 2, y_end = 32768;
  const int y_start = 65536, y_end = 262144;

  const int total_bases = base_end - base_start;

  ParallelComp(
      base_end - base_start,
      [&](int offset) {
        const int base = base_start + offset;

        {
          std::unique_lock ml(m);
          in_progress.insert(base);
        }

        // We want x != y, so wlog we require x < y.
        for (int y = y_start; y < y_end; y++) {
          for (int x = 1; x < y; x++) {
            std::array<int, 9> sq = ProgSquare(base, x, y);
            const auto &[a, b, c,
                         d, e, f,
                         g, h, i] = sq;
            if constexpr (SELF_CHECK) {
              // horiz
              const int sum = a + b + c;
              CHECK(d + e + f == sum);
              CHECK(g + h + i == sum);
              // vertical
              CHECK(a + d + g == sum);
              CHECK(b + e + h == sum);
              CHECK(c + f + i == sum);
              // diag
              CHECK(a + e + i == sum);
              CHECK(g + e + c == sum);
            }

            int not_square = 0, total_err = 0;
            for (int cell : sq) {
              int err = SqrtError(cell);
              if (err != 0) {
                not_square++;
                total_err += err;
              }
            }

            if (not_square < 8 && total_err < show[not_square]) {
              // Reject duplicates, though.
              std::unordered_set<int> unique;
              unique.reserve(9);
              for (int cell : sq) unique.insert(cell);
              if (unique.size() == 9) {
                std::unique_lock ml(m);
                std::string result =
                  StringPrintf("# %d not square, %d total error\n",
                               not_square, total_err);
                StringAppendF(&result, "SQUARE");
                for (int cell : sq) StringAppendF(&result, " %d", cell);
                StringAppendF(&result, " prog%d_%d_%d\n", base, x, y);
                printf("%s", result.c_str());
                FILE *f = fopen("brute.txt", "ab");
                fprintf(f, "%s", result.c_str());
                fclose(f);
              }
            }
          }
        }

        int min_pending = 0;
        {
          std::unique_lock ml(m);
          done_bases++;
          in_progress.erase(base);
          if (!in_progress.empty()) {
            min_pending = *in_progress.begin();
          }
        }
        if (status_per.ShouldRun()) {
          double sec = run_timer.Seconds();
          std::string bar = ANSI::ProgressBar(
              done_bases, total_bases,
              StringPrintf("brute force complete <%d", min_pending),
              sec);

          printf(ANSI_PREVLINE ANSI_BEGINNING_OF_LINE ANSI_CLEARLINE
                 ANSI_BEGINNING_OF_LINE "%s\n",
                 bar.c_str());
        }
      },
      6);
}

int main(int argc, char **argv) {
  ANSI::Init();

  Brute();

  return 0;
}
