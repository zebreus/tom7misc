
#include "ansi.h"

#include <set>
#include <unordered_set>
#include <cstdint>
#include <cstdio>
#include <mutex>

#include "ansi.h"
#include "atomic-util.h"
#include "auto-histo.h"
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
#include "brute-util.h"

static constexpr bool SELF_CHECK = false;

#define USE_GPU 1

static constexpr const char *DONEFILE = "brute-done.txt";

static CL *cl = nullptr;

DECLARE_COUNTERS(
    // bases observed
    counter_bases,
    // bases skipped because the starting error is too high
    counter_filtered,
    u1_, u2_, u3_, u4_, u5_, u6_);

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
// Each row/column/diagonal sums to 3n + 3x + 3y.
//
// So we can loop over (n,x,y), which is a much smaller search
// space than looping over 9 cells!
// (Do we need to consider negative x, y?)
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

namespace {
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
  // tested every (base, y, x) for x in [-base/2, y). That makes this
  // a 2D representation. In this interval cover, the key is
  // the base, and the value indicates that we're done
  // for y in [0, value).
  using Cover = IntervalCover<int>;
  Cover cover;
};
}  // namespace

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

// Note that all of these were just positive x. Starting 28 Aug 2024
// I expanded the definition of "done" to include x in -base/2 to 0.
// At that point, we had done all of the positive x in:
// [0, 16384) 274000
// [16384, 131072) 65536
// [131072, 2621440] 32768
//
// Then I re-ran this range, so the simple definition is all you
// need to think about.

//   a b c
//   d e f
//   g h i
//
// This program loops over base values to occupy the 'b' slot in the
// square. The first thing we do is see how far b is from a square
// (the err). If the error is already at least as high as all our
// records, we don't even bother, since we could never do better. The
// 0 case is excluded because this has error 0 by default (so we will
// try it if the base is a square). Also, sos.exe is a faster approach
// for finding these. The one non-square case can also be ignored
// because sos.exe is a faster way of finding such squares (as long as
// the non-square is in positions b, d, f, or h).
static constexpr int ERROR_FILTER_THRESHOLD =
  std::min(std::initializer_list<int>{
      best_threshold[2],
      best_threshold[3],
      best_threshold[4],
      best_threshold[5],
      best_threshold[6],
      best_threshold[7],
      best_threshold[8],
      best_threshold[9]});

static std::mutex output_mutex;

static std::mutex histo_mutex;
static std::vector<AutoHisto> *histos = nullptr;

static std::mutex result_mutex;
namespace {
struct Result {
  int base = 0, x = 0, y = 0;
  std::array<int, 9> square;
  int not_square = 0;
  int total_err = 0;
  int64_t timestamp = 0;
};
}

static std::array<Result, 10> recent_results;

static double OBSERVE_AGE = 3600.0 * 4.0;
static void ObserveResult(const Result &r) {
  {
    MutexLock ml(&histo_mutex);
    (*histos)[r.not_square].Observe(r.total_err);
  }

  {
    MutexLock ml(&result_mutex);

    const Result &old = recent_results[r.not_square];

    // Overwrite if better or old is too old.
    if (r.timestamp - old.timestamp > OBSERVE_AGE ||
        r.total_err <= old.total_err) {
      recent_results[r.not_square] = r;
    }
  }
}

static void DisplayResults() {
  int64_t now = time(nullptr);

  MutexLock ml1(&histo_mutex);
  MutexLock ml2(&result_mutex);
  for (int m = 0; m < 10; m++) {
    if (!(*histos)[m].Empty()) {

      printf("%s " ABLUE("#%d") "\n",
             (*histos)[m].SimpleHorizANSI(12).c_str(), m);
    }

    const Result &r = recent_results[m];
    if (r.base > 0) {
      const auto &[a, b, c,
                   d, e, f,
                   g, h, i] = r.square;

      auto C = [](int a) {
          int e = SqrtError(a);

          auto P = [](const std::string &s) { return Util::Pad(-7, s); };

          if (e == 0) return P(StringPrintf(AGREEN("%d"), a));
          else if (e == 1) return P(StringPrintf(AYELLOW("%d"), a));
          else return P(StringPrintf(AORANGE("%d"), a));
        };

      printf("%s %s %s | %d,%d,%d\n"
             "%s %s %s | #%d %s ago\n"
             "%s %s %s | err: %d\n",
             C(a).c_str(), C(b).c_str(), C(c).c_str(),
             r.base, r.x, r.y,
             C(d).c_str(), C(e).c_str(), C(f).c_str(),
             r.not_square, ANSI::Time(now - r.timestamp).c_str(),
             C(g).c_str(), C(h).c_str(), C(i).c_str(),
             r.total_err);
    }
  }
}

static std::string ShowSquare(const std::array<int, 9> &sq) {
  const auto &[a, b, c,
               d, e, f,
               g, h, i] = sq;

  return StringPrintf("%d %d %d\n"
                      "%d %d %d\n"
                      "%d %d %d\n",
                      a, b, c,
                      d, e, f,
                      g, h, i);
}

static inline void ConsiderOne(uint32_t base, int32_t x, uint32_t y) {
  // These result in degenerate squares (duplicates).
  if (x == 0 || y == 0 || x == y || -x == y ||
      x == 2 * y || y == 2 * x) {
    return;
  }

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
    CHECK(err >= 0) << "base = " << base << " x = " << x
                    << " y = " << y << "\n" << ShowSquare(sq);
    if (err != 0) {
      not_square++;
      total_err += err;
    }
  }

  Result result;
  result.base = base;
  result.x = x;
  result.y = y;
  result.timestamp = time(nullptr);
  result.square = sq;
  result.not_square = not_square;
  result.total_err = total_err;
  ObserveResult(result);

  if (total_err < save_threshold[not_square]) {
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
      if (total_err <= save_threshold[not_square]) {
        FILE *f = fopen("brute.txt", "ab");
        fprintf(f, "%s", result.c_str());
        fclose(f);
      }
    }
  }
}



static void Brute() {
  #if USE_GPU
  BruteGPU brute_gpu(cl, report_threshold);
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

  int base_start = 0, base_end = 2621440;
  int y_end = 65536 * 2;

  // int base_start = 131072, base_end = 1048576;
  // [131072, 2621440] 32768 <- in progress

  // x can be negative, in which case the smallest
  // cell will be n + 2*x. That means that we want
  // to run y from 1 to y_end, and x from -n/2 to y.

  // Skip bases that are totally complete before we
  // get into the parallel phase.
  while (base_start < base_end &&
         done.MinY(base_start) >= y_end) {
    base_start++;
  }

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

        DisplayResults();

        printf(ACYAN("%lld") " done; " APURPLE("%lld") " filtered\n",
               counter_bases.Read(),
               counter_filtered.Read());

        double sec = run_timer.Seconds();
        std::string bar = ANSI::ProgressBar(
            db, total_bases,
            StringPrintf(
                "[%s; %s/s] ✓ <%d; %s ago",
                FormatNum(total_squares).c_str(),
                FormatNum(total_squares / run_timer.Seconds()).c_str(),
                min_pending,
                ANSI::Time(saved_ago).c_str()),
            sec);

        printf(// ANSI_PREVLINE ANSI_BEGINNING_OF_LINE ANSI_CLEARLINE
               // ANSI_BEGINNING_OF_LINE
               "%s\n",
               bar.c_str());
      }
    };

  printf("Running base %d to %d, y to <%d\n\n",
         base_start, base_end,
         y_end);

  ParallelComp(
      total_bases,
      [&](int offset) {

        const int base = base_start + offset;

        const int starting_err = SqrtError(base);

        int64_t task_squares = 0;

        if (starting_err >= ERROR_FILTER_THRESHOLD) {
          counter_filtered++;
        } else {

          int y_start = 0;

          {
            std::unique_lock ml(m);
            y_start = done.MinY(base);
            /*
            printf("\n%d. y_start = " AYELLOW("%d")
                   " y_end = " AORANGE("%d") "\n", base, y_start, y_end);
            */
            if (y_start >= y_end) return;
            in_progress.insert(base);
          }

          #if USE_GPU
          int64_t executed = 0;
          std::vector<std::pair<int32_t, uint32_t>> interesting =
            brute_gpu.RunOne(base, y_start, y_end, &executed);
          for (const auto &[x, y] : interesting) {
            ConsiderOne(base, x, y);
          }

          // (This may not be exactly right, but we just use it for
          // reporting status.)
          // XXX it's really wrong now that x starts at -n/2.
          // const int n = (y_end - y_start) * (y_end - 1) / 2;

          // This overcounts because of the many tasks that
          // die immediately.
          task_squares += executed;


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
        }

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

        counter_bases++;

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
  for (int i = 0; i < 10; i++) {
    recent_results[i] = Result();
  }
  histos = new std::vector<AutoHisto>;
  histos->resize(10);

  Brute();

  return 0;
}
