
#include "ansi.h"

#include <set>
#include <unordered_set>
#include <cstdint>
#include <cstdio>
#include <mutex>

#include "ansi.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "interval-cover.h"
#include "periodically.h"
#include "sos-util.h"
#include "threadutil.h"
#include "timer.h"
#include "util.h"

#include "clutil.h"
#include "brute-gpu.h"

static constexpr bool SELF_CHECK = false;

#define USE_GPU 1

static constexpr const char *DONEFILE = "brute-done.txt";

static CL *cl = nullptr;

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

struct Done {
  // Or abort if invalid.
  static Done FromString(const std::string &contents) {
    std::vector<std::string> lines =
      Util::NormalizeLines(Util::SplitToLines(contents));

    Cover cover(0);
    for (std::string &line : lines) {
      if (line.empty()) continue;
      if (line[0] == '#') continue;
      std::vector<std::string> parts = Util::Split(line, ' ');
      CHECK(parts.size() == 3) << line;
      uint64_t start = atoll(parts[0].c_str());
      uint64_t end = atoll(parts[1].c_str());
      uint64_t y = atoll(parts[2].c_str());
      cover.SetSpan(start, end, y);
      printf("[" AWHITE("%llu") ", " AWHITE("%llu") "): <"
             APURPLE("%llu") "\n",
             start, end, y);
    }

    Done d;
    d.cover = std::move(cover);
    return d;
  }

  Done() : cover(0) {}

  // Get the number n for which we have done all y < n for the given base.
  uint64_t MinY(uint64_t base) const {
    Cover::Span s = cover.GetPoint(base);
    return s.data;
  }

  void SetMinY(uint64_t base, uint64_t y_end) {
    cover.SetPoint(base, y_end);
  }


  void Save(const std::string &filename) {
    FILE *f = fopen(filename.c_str(), "wb");
    CHECK(f != nullptr);
    uint64_t pt = 0;

    while (!Cover::IsAfterLast(pt)) {
      Cover::Span s = cover.GetPoint(pt);
      fprintf(f, "%llu %llu %d\n",
              s.start, s.end, s.data);
      pt = s.end;
    }
    fclose(f);
  }

  // We can say that for a pair (base, y) to be done, we've
  // tested every (base, y, x) for x in [1, y). That makes this
  // a 2D representation. In this interval cover, the key is
  // the base, and the value indicates that we're done
  // for y in [0, value).
  using Cover = IntervalCover<int>;
  Cover cover;
};

// History:
// Ran this on 28-29 Feb 2024.
// cover.SetSpan(0, 32768, std::make_pair(0, 32768));
// And then I weirdly ran y [32768, 65536) for base 0-65536,
// finishing on 6 Mar 2024.
// Then I ran the gap I left, y [0, 32768) for base [32768, 65536),
// finishing on 7 Mar 2024.
// Then did 65536-131072 from from 0 to 65536 through March 13.
// cover.SetSpan(0, 131072, 65536);
// 500 hour run completed March 15 to 9 Apr 2024.
// cover.SetSpan(0, 16384, 262144);

static std::string FormatNum(uint64_t n) {
  if (n > 1'000'000) {
    double m = n / 1000000.0;
    if (m >= 1000.0) {
      return StringPrintf("%.1fB", m / 1000.0);
    } else if (m >= 100.0) {
      return StringPrintf("%dM", (int)std::round(m));
    } else if (m > 10.0) {
            return StringPrintf("%.1fM", m);
    } else {
      // TODO: Integer division. color decimal place and suffix.
      return StringPrintf("%.2fM", m);
    }
  } else {
    return Util::UnsignedWithCommas(n);
  }
}


[[maybe_unused]]
static constexpr int best[10] = {
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
  // 8
  14,
  // 9
  15,
};

// Threshold to show
static constexpr int show[10] = {
  // best with 0 non-squares
  999999999,
  // best with 1 non-square
  999999999,
  // best with 2 non-squares
  1000,
  // best with 3 non-squares
  100,
  // best with 4 non-squares
  20,
  // best with 5 non-squares
  5,
  // best with 6 non-squares
  6,
  // best with 7 non-squares
  9,
  // 8
  14,
  // 9
  15,
};

static std::mutex output_mutex;

static inline void ConsiderOne(uint32_t base, uint32_t x, uint32_t y) {
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

  if (total_err < show[not_square]) {
    // Reject duplicates, though.
    std::unordered_set<int> unique;
    unique.reserve(9);
    for (int cell : sq)
      unique.insert(cell);
    if (unique.size() == 9) {
      std::unique_lock ml(output_mutex);
      std::string result = StringPrintf("# %d not square, %d total error\n",
                                        not_square, total_err);
      StringAppendF(&result, "SQUARE");
      for (int cell : sq)
        StringAppendF(&result, " %d", cell);
      StringAppendF(&result, " prog%d_%d_%d\n", base, x, y);
      printf("\n\n%s\n\n", result.c_str());
      FILE *f = fopen("brute.txt", "ab");
      fprintf(f, "%s", result.c_str());
      fclose(f);
    }
  }
}



static void Brute() {
  #if USE_GPU
  BruteGPU brute_gpu(cl);
  #endif

  Periodically status_per(5.0);
  Timer last_save;
  Periodically save_per(60.0 * 5.0);

  int64_t done_bases = 0;
  Timer run_timer;

  std::mutex m;

  // Already did base = 0..32768, y=2..32768, x = 1..y.

  std::string done_txt = Util::ReadFile(DONEFILE);
  Done done = done_txt.empty() ? Done() : Done::FromString(done_txt);

  // Running this starting 24 Aug 2024.
  // cover.SetSpan(0, 16384, 270000);

  std::set<int> in_progress;
  const int base_start = 0, base_end = 16384;
  // const int y_start = 2, y_end = 32768;
  // const int y_start = 65536, y_end = 262144;
  const int y_end = 270000;

  const int total_bases = base_end - base_start;
  int64_t total_squares = 0;

  auto MaybeStatus =
    [&]() {
      if (status_per.ShouldRun()) {
        int min_pending = 0;
        int64_t db = 0;
        double saved_ago = 0.0;
        {
          std::unique_lock ml(m);
          db = done_bases;
          if (!in_progress.empty()) {
            min_pending = *in_progress.begin();
          }
          saved_ago = last_save.Seconds();
        }

        double sec = run_timer.Seconds();
        std::string bar = ANSI::ProgressBar(
            db, total_bases,
            StringPrintf(
                "[%s; %s/s] done <%d saved %d ago",
                FormatNum(total_squares).c_str(),
                FormatNum(total_squares / run_timer.Seconds()).c_str(),
                min_pending, (int)saved_ago),
            sec);

        printf(ANSI_PREVLINE ANSI_BEGINNING_OF_LINE ANSI_CLEARLINE
               ANSI_BEGINNING_OF_LINE "%s\n",
               bar.c_str());
      }
    };

  printf("Running base %d to %d, y to <%d\n",
         base_start, base_end,
         y_end);

  ParallelComp(
      total_bases,
      [&](int offset) {
        const int base = base_start + offset;

        int y_start = 0;

        {
          std::unique_lock ml(m);
          in_progress.insert(base);
          y_start = done.MinY(base);
        }

        int64_t task_squares = 0;

        #if USE_GPU
        std::vector<std::pair<uint32_t, uint32_t>> interesting =
          brute_gpu.RunOne(base, y_start, y_end);
        for (const auto &[x, y] : interesting) {
          ConsiderOne(base, x, y);
        }

        // (This may not be exactly right, but we just use it for
        // reporting status.)
        const int n = (y_end - y_start) * (y_end - 1) / 2;
        task_squares += n;

        #else
        // We want x != y, so wlog we require x < y.
        for (int y = y_start; y < y_end; y++) {
          for (int x = 1; x < y; x++) {
            ConsiderOne(base, x, y);
            task_squares++;
            if (task_squares % 65536 == 0) {
              MaybeStatus();
            }
          }
        }
        #endif

        {
          std::unique_lock ml(m);
          total_squares += task_squares;
          done_bases++;
          in_progress.erase(base);
          done.SetMinY(base, y_end);
          if (save_per.ShouldRun()) {
            done.Save(DONEFILE);
            last_save.Reset();
          }
        }

        MaybeStatus();
      },
      #if USE_GPU
      2
      #else
      6
      #endif
               );

  done.Save(DONEFILE);
}

int main(int argc, char **argv) {
  ANSI::Init();
  cl = new CL;
  CHECK(cl);

  Brute();

  return 0;
}
