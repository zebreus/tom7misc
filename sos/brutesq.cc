
#include "ansi.h"

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
#include "brutesq-gpu.h"
#include "brute-util.h"

static constexpr bool SELF_CHECK = false;

// This is a variant of the "simple" brute force search.
// The goal is to find proper magic squares whose elements
// are close to square numbers. It uses this enumeration:
//
//  n+2x+y  n       n+x+2y          a    b^2 c
//  n+2y    n+x+y   n+2x            d^2  e   f
//  n+x     n+2x+2y n+y             g    h   i
//
// Each row/column/diagonal sums to 3n + 3x + 3y.
//
// Here we are constraining the search to cases where n is exactly a
// square, and n + 2y is as well. So we'll have n = b^2 and d^2 = b^2
// + 2y. In order for y to be an integer, d and b must both be even or
// both be odd. Without loss of generality we say y > 0 (otherwise we
// can just transpose). So we loop over integers (u, v) with 2v < u,
// and the resulting square is
//
//   n = b^2 = u^2
//   d^2 = (2*v + u mod 2)^2
//   2y = u^2 - (2*v + u mod 2)^2

// Get n,y such that the cells 'b' and 'd' will be square.
// Note that this returns a nonpositive y.
std::pair<int64_t, int64_t> BaseAndY(int64_t u, int64_t v) {
  if (SELF_CHECK) {
    CHECK(2 * v < u) << u << " " << v;
  }
  int64_t n = u * u;
  int64_t d = (2 * v + (u & 1));
  // d^2 = n + 2y
  // -2y + d^2 = n
  // -2y = n - d^2
  // 2y = d^2 - n
  int64_t two_y = d * d - n;
  if (SELF_CHECK) {
    CHECK((two_y & 1) == 0) << u << " " << v;
    CHECK(u >= 0);
    CHECK(two_y <= 0);
  }

  int64_t y = two_y / 2;

  if (SELF_CHECK) {
    int64_t ne = SqrtError(n);
    int64_t de = SqrtError(n + 2 * y);
    CHECK(ne == 0 && de == 0) <<
      StringPrintf("u=%lld, v=%lld\n"
                   "d = %lld\n"
                   "2y = %lld - %lld^2 = %lld\n"
                   "n: %lld, y=%lld\n"
                   "n + 2 * y = %lld\n"
                   "nerr: %lld, derr=%lld\n",
                   u, v,
                   d,
                   n, d, two_y,
                   n, y,
                   n + 2 * y,
                   ne, de);
  }

  return {n, y};
}

// Now the search is very similar to brute.cc. We have n and y
// determined, and we search all x in (0, X) where X is some
// bound.

static constexpr const char *DONEFILE = "brutesq-done.txt";

static CL *cl = nullptr;

DECLARE_COUNTERS(
    // bases observed
    counter_bases,
    // bases skipped because the starting error is too high
    counter_filtered,
    // returned from GPU
    counter_reported,
    counter_degenerate,
    counter_not_better, u4_, u5_, u6_);

inline std::array<int64_t, 9> ProgSquare(int64_t n, int64_t x, int64_t y) {
  return {
    n + 2 * x + y,  n,                  n + x + 2 * y,
    n + 2 * y,      n + x + y,          n + 2 * x,
    n + x,          n + 2 * x + 2 * y,  n + y};
}

// In this "done" representation, the key is the value u, and
// a value X means that we have done every (u, v, x) for
// 2v < u and 0 < x < X.
namespace {
struct Done {
  static constexpr int64_t INITIAL_VALUE = int64_t{0};

  // Or abort if invalid.
  static Done FromString(const std::string &contents) {
    std::vector<std::string> lines =
      Util::NormalizeLines(Util::SplitToLines(contents));

    Cover cover(INITIAL_VALUE);
    for (std::string &line : lines) {
      if (line.empty()) continue;
      if (line[0] == '#') continue;
      std::vector<std::string> parts = Util::Split(line, ' ');
      CHECK(parts.size() == 3) << line;
      int64_t start = atoll(parts[0].c_str());
      int64_t end = atoll(parts[1].c_str());
      int64_t y = atoll(parts[2].c_str());
      cover.SetSpan(start, end, y);
      printf("[" AWHITE("%llu") ", " AWHITE("%llu") "): <"
             APURPLE("%llu") "\n",
             start, end, y);
    }

    Done d;
    d.cover = std::move(cover);
    return d;
  }

  Done() : cover(INITIAL_VALUE) {}

  // For the given u, get the number X for which we have
  // tried all squares (base, x, y)
  // with (base, y) = BaseAndY(u, v)
  // for all v | 2v < u
  // and all x | 0 < x < X.
  int64_t BoundX(uint64_t u) const {
    Cover::Span s = cover.GetPoint(u);
    // printf("BoundX %llu = %lld\n", u, s.data);
    return s.data;
  }

  void SetBoundX(uint64_t u, int64_t x_end) {
    cover.SetPoint(u, x_end);
  }

  void Save(const std::string &filename) {
    FILE *f = fopen(filename.c_str(), "wb");
    CHECK(f != nullptr);
    uint64_t pt = 0;

    while (!Cover::IsAfterLast(pt)) {
      Cover::Span s = cover.GetPoint(pt);
      fprintf(f, "%llu %llu %lld\n",
              s.start, s.end, s.data);
      pt = s.end;
    }
    fclose(f);
  }

  using Cover = IntervalCover<int64_t>;
  Cover cover;
};
}


static std::mutex output_mutex;

static std::mutex histo_mutex;
static std::vector<AutoHisto> *histos = nullptr;

static std::mutex result_mutex;
namespace {
struct Result {
  int64_t base = 0, x = 0, y = 0;
  std::array<int64_t, 9> square;
  int not_square = 0;
  int64_t total_err = 0;
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
    } else {
      counter_not_better++;
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

      auto C = [](int64_t a) {
          int64_t e = SqrtError(a);

          auto P = [](const std::string &s) { return Util::Pad(-11, s); };

          if (e == 0) return P(StringPrintf(AGREEN("%lld"), a));
          else if (e == 1) return P(StringPrintf(AYELLOW("%lld"), a));
          else return P(StringPrintf(AORANGE("%lld"), a));
        };

      printf("%s %s %s | %lld,%lld,%lld\n"
             "%s %s %s | #%d %s ago\n"
             "%s %s %s | err: %lld\n",
             C(a).c_str(), C(b).c_str(), C(c).c_str(),
             r.base, r.x, r.y,
             C(d).c_str(), C(e).c_str(), C(f).c_str(),
             r.not_square, ANSI::Time(now - r.timestamp).c_str(),
             C(g).c_str(), C(h).c_str(), C(i).c_str(),
             r.total_err);
    }
  }
}

static std::string ShowSquare(const std::array<int64_t, 9> &sq) {
  const auto &[a, b, c,
               d, e, f,
               g, h, i] = sq;

  return StringPrintf("%lld %lld %lld\n"
                      "%lld %lld %lld\n"
                      "%lld %lld %lld\n",
                      a, b, c,
                      d, e, f,
                      g, h, i);
}

static inline void ConsiderOne(int64_t base, int64_t x, int64_t y) {
  if constexpr (SELF_CHECK) {
    // x and y can have either sign, but in this particular search
    // we expect a nonnegative x and a nonpositive y.
    // expected in this particular search.
    CHECK(base >= 0 && x >= 0 && y <= 0) << base << " " << x << " " << y;
  }

  // These result in degenerate squares (duplicates).
  if (x == 0 || y == 0 || x == y || -x == y ||
      x == 2 * y || y == 2 * x) {
    printf("Example degenerate: %lld %lld %lld\n", base, x, y);
    counter_degenerate++;
    return;
  }

  std::array<int64_t, 9> sq = ProgSquare(base, x, y);
  const auto &[a, b, c,
               d, e, f,
               g, h, i] = sq;
  if constexpr (SELF_CHECK) {
    // horiz
    const int64_t sum = a + b + c;
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

  int not_square = 0;
  int64_t total_err = 0;
  for (int64_t cell : sq) {
    int64_t err = SqrtError(cell);
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
    std::unordered_set<int64_t> unique;
    unique.reserve(9);
    for (int64_t cell : sq)
      unique.insert(cell);
    if (unique.size() == 9) {
      std::unique_lock ml(output_mutex);
      std::string result = StringPrintf("# %d not square, %lld total error\n",
                                        not_square, total_err);
      StringAppendF(&result, "SQUARE");
      for (int cell : sq)
        StringAppendF(&result, " %lld", cell);
      StringAppendF(&result, " prog%lld_%lld_%lld\n", base, x, y);
      printf("\n\n%s\n\n", result.c_str());
      if (total_err <= save_threshold[not_square]) {
        FILE *f = fopen("brutesq.txt", "ab");
        fprintf(f, "%s", result.c_str());
        fclose(f);
      }
    }
  }
}



static void Brute() {
  BruteSqGPU brutesq_gpu(cl, report_threshold);

  Periodically status_per(5.0);
  Timer last_save;
  Periodically save_per(60.0 * 5.0);

  int64_t done_bases = 0;
  Timer run_timer;

  std::mutex m;

  std::string done_txt = Util::ReadFile(DONEFILE);
  Done done = done_txt.empty() ? Done() : Done::FromString(done_txt);

  // v always ranges up to u/2.
  uint64_t u_start = 0, u_end = 65536;
  int64_t x_end = 32768;

  // Skip bases that are already totally complete before we get into
  // the parallel phase.
  while (u_start < u_end &&
         done.BoundX(u_start) >= x_end) {
    u_start++;
  }

  const int64_t total_u = u_end - u_start;
  int64_t total_squares = 0;

  auto MaybeStatus =
    [&]() {
      if (status_per.ShouldRun()) {
        int64_t db = 0;
        double saved_ago = 0.0;
        {
          std::unique_lock ml(m);
          db = done_bases;
          saved_ago = last_save.Seconds();
        }

        printf("\n");
        DisplayResults();

        printf(ACYAN("%lld") " done; " APURPLE("%s") " reptd "
               AORANGE("%s") " degen; " AYELLOW("%s") " not better\n",
               counter_bases.Read(),
               FormatNum(counter_reported.Read()).c_str(),
               FormatNum(counter_degenerate.Read()).c_str(),
               FormatNum(counter_not_better.Read()).c_str());

        double sec = run_timer.Seconds();
        std::string bar = ANSI::ProgressBar(
            db, total_u,
            StringPrintf(
                "[%s; %s/s] %s ago",
                FormatNum(total_squares).c_str(),
                FormatNum(total_squares / run_timer.Seconds()).c_str(),
                ANSI::Time(saved_ago).c_str()),
            sec);

        printf("%s\n",
               bar.c_str());
      }
    };

  printf("Running u %lld to %lld (%lld), x to <%lld\n\n",
         u_start, u_end, total_u,
         x_end);

  UnParallelComp(
      total_u,
      [&](int64_t offset) {

        const int64_t u = u_start + offset;

        int64_t task_squares = 0;

        int64_t x_start = 0;

        {
          std::unique_lock ml(m);
          x_start = done.BoundX(u);
          if (x_start >= x_end) {
            return;
          }
        }

        for (int64_t v = 1; 2 * v < u; v++) {
          const auto &[base, y] = BaseAndY(u, v);

          if (SELF_CHECK) {
            CHECK(base >= 0 && y <= 0) <<
              StringPrintf("(u, v) (%lld,%lld) = tuple (%lld, _, %lld)\n",
                           u, v, base, y);
          }

          int64_t executed = 0;
          std::vector<int64_t> interesting =
            brutesq_gpu.RunOne(base, y, x_start, x_end, &executed);
          counter_reported += interesting.size();
          for (const int64_t x : interesting) {
            ConsiderOne(base, x, y);
          }

          task_squares += executed;
        }

        {
          std::unique_lock ml(m);
          total_squares += task_squares;
          done_bases++;
          done.SetBoundX(u, x_end);
          if (save_per.ShouldRun()) {
            done.Save(DONEFILE);
            last_save.Reset();
          }
        }

        counter_bases++;

        MaybeStatus();
      },
      2);

  printf("Done! bases %lld, squares %lld, reported %lld\n",
         counter_bases.Read(), total_squares,
         counter_reported.Read());
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
