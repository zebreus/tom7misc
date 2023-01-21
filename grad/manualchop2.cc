// Semi-manual search for Choppy (256) functions that are useful in
// fluint8. (We can construct any choppy function from the basis, but
// they are often much larger than needed.)

#include "expression.h"
#include "timer.h"

#include <array>
#include <algorithm>
#include <functional>
#include <array>
#include <utility>
#include <unordered_set>

#include "half.h"

#include "grad-util.h"
#include "makefn-ops.h"
#include "arcfour.h"
#include "randutil.h"
#include "color-util.h"
#include "state.h"

#include "choppy.h"
#include "ansi.h"

using Choppy = ChoppyGrid<256>;
using Table = Exp::Table;
using uint32 = uint32_t;
using uint8 = uint8_t;
using Allocator = Exp::Allocator;
using DB = Choppy::DB;

static constexpr int IMAGE_SIZE = 1920;

static inline uint16 U(double d) {
  return Exp::GetU16((half)d);
}

// Abstract choppy goal.
// This one is the "Canonize" function, which is basically
// floor (rounding towards -inf) for every value in [-1, 1)
// except -0.
struct GoalCanonize {
  static_assert(Choppy::GRID == 256);
  constexpr static DB::key_type TargetKey() {
    DB::key_type key;
    for (int i = 0; i < 256; i++)
      key[i] = -128 + i;
    return key;
  }

  // TODO:
  static constexpr std::initializer_list<uint16> critical = {
    // -1 and epsilon less
    0xbc00,
    0xbbff,
    // -1/128 and epsilon less
    0xa000,
    0x9fff,
    // very small negative
    0x8002,
    0x8001,
    // negative zero, our foe!
    0x8000,

    // Positive small values
    0x0000,
    0x0001,
    0x0002,
    // almost 1/128, then 1/128
    0x1fff,
    0x2000,
    // almost 1
    0x3bff,
    0x3c00,
  };


};

using Goal = GoalCanonize;

static bool IsCorrect(const DB::key_type &chop) {
  static constexpr DB::key_type correct_chop = Goal::TargetKey();
  return chop == correct_chop;
}

static bool IsCorrect(const Exp *e) {
  auto chopo = Choppy::GetChoppy(e);
  if (!chopo.has_value())
    return false;

  return IsCorrect(chopo.value());
}

// Annoyingly we have to pass around the allocator pointer...
struct ExpWrapper {
  ExpWrapper(Allocator *alloc,
             const Exp *exp) : alloc(alloc), exp(exp) {}
  Allocator *alloc = nullptr;
  const Exp *exp = nullptr;
};

static ExpWrapper operator+ (ExpWrapper e, uint16_t c) {
  if (e.exp->type == PLUS_C && e.exp->c == c) {
    return ExpWrapper(e.alloc,
                      e.alloc->PlusC(e.exp->a, c, e.exp->iters + 1));
  } else {
    return ExpWrapper(e.alloc, e.alloc->PlusC(e.exp, c));
  }
}

static ExpWrapper operator- (ExpWrapper e, uint16_t c) {
  return e + (uint16)(0x8000 ^ c);
}

static ExpWrapper operator* (ExpWrapper e, uint16_t c) {
  if (e.exp->type == TIMES_C && e.exp->c == c) {
    return ExpWrapper(e.alloc,
                      e.alloc->TimesC(e.exp->a, c, e.exp->iters + 1));
  } else {
    return ExpWrapper(e.alloc, e.alloc->TimesC(e.exp, c));
  }
}

// Just iteratively applying * will do this, but it also creates
// a lot of garbage (and reduces locality).
static ExpWrapper IteratedTimes(ExpWrapper e, uint16_t c, int iters) {
  return ExpWrapper(e.alloc, e.alloc->TimesC(e.exp, c, iters));
}

static ExpWrapper IteratedPlus(ExpWrapper e, uint16_t c, int iters) {
  return ExpWrapper(e.alloc, e.alloc->PlusC(e.exp, c, iters));
}


static ExpWrapper operator+ (ExpWrapper a, ExpWrapper b) {
  return ExpWrapper(a.alloc, a.alloc->PlusE(a.exp, b.exp));
}

static ExpWrapper operator- (ExpWrapper a, ExpWrapper b) {
  return ExpWrapper(a.alloc,
                    a.alloc->PlusE(a.exp,
                                   a.alloc->Neg(b.exp)));
}

// sets f(0) = 0.
static ExpWrapper operator~ (ExpWrapper a) {
  const uint16 v = Exp::EvaluateOn(a.exp, 0x0000);
  return a - v;
}

// sets f(1) = 1.
static ExpWrapper operator! (ExpWrapper a) {
  const uint16 v = Exp::EvaluateOn(a.exp, 0x3c00);
  half inv_h = 1.0_h / Exp::GetHalf(v);
  return a * U(inv_h);
}

struct ColorPool {

  int GetOrAdd(uint16 u) {
    auto it = ids.find(u);
    if (it == ids.end()) {
      ids[u] = next++;
    }
    return ids[u];
  }

  // Or -1 if not present.
  int Get(uint16 u) const {
    auto it = ids.find(u);
    if (it == ids.end()) return -1;
    return it->second;
  }

  int next = 0;
  std::map<uint16, int> ids;
};

static void PrintExpressionStats(const Table &result) {
  ColorPool color_pool;

  // First tally the colors.
  for (uint16 ux : Goal::critical) {
    uint16 uy = result[ux];
    color_pool.GetOrAdd(uy);
  }

  auto GetColor = [&](uint16 u) ->
    std::pair<const char *, const char *> {
    static std::array C = {
        ANSI_BLUE,
        ANSI_CYAN,
        ANSI_YELLOW,
        ANSI_GREEN,
        ANSI_PURPLE,
        ANSI_RED,
      };
    int i = color_pool.Get(u);
    if (i >= 0 && i < C.size()) {
      return make_pair(C[i], ANSI_RESET);
    } else {
      return make_pair("", "");
    }
  };

  auto HexColor = [&](uint16 u) {
      auto [s, e] = GetColor(u);
      return StringPrintf("%s%04x%s", s, u, e);
    };

  CPrintf("Critical points:\n");
  for (uint16 ux : Goal::critical) {
    uint16 uy = result[ux];
    half x = Exp::GetHalf(ux);
    half y = Exp::GetHalf(uy);

    string xp = Util::Pad(17, StringPrintf("%.11g", (float)x));
    string yp = Util::Pad(17, StringPrintf("%.11g", (float)y));

    CPrintf("%s (%s) yields %s (%s)\n",
            xp.c_str(), HexColor(ux).c_str(),
            yp.c_str(), HexColor(uy).c_str());
  }

  // TODO: note nan/inf in range

  std::optional<uint16_t> example_ntop, example_pton;

  std::map<uint16, std::pair<uint16, uint16>> values;
  std::unordered_set<uint16> pos_values, neg_values;
  const half low = (half)-1.0;
  const uint16 ulow = Exp::GetU16(low);
  const half high = (half)1.0;
  const uint16 uhigh = Exp::GetU16(high);
  for (uint16 upos = ulow; upos != uhigh; upos = Exp::NextAfter16(upos)) {
    uint16 x = upos;
    uint16 y = result[x];

    half xx = Exp::GetHalf(x);
    half yy = Exp::GetHalf(y);

    if (!example_ntop.has_value() &&
        xx < (half)0.0 && x != 0x8000 && yy > (half)0.0) {
      example_ntop.emplace(x);
    }

    if (!example_pton.has_value() &&
        xx > (half)0.0 && x != 0x8000 && yy < (half)0.0) {
      example_pton.emplace(x);
    }

    if (xx < (half)0.0 && x != 0x8000) {
      neg_values.insert(y);
    } else if (xx >= (half)0.0) {
      pos_values.insert(y);
    }

    if (values.find(y) == values.end()) {
      values[y] = make_pair(x, x);
    } else {
      half pos = Exp::GetHalf(upos);
      auto &[first, last] = values[y];
      if (pos < Exp::GetHalf(first)) first = x;
      if (pos > Exp::GetHalf(last)) last = x;
    }
  }

  if (example_ntop.has_value() ||
      example_pton.has_value()) {
    CPrintf("The sign was " ANSI_RED "not preserved" ANSI_RESET ":\n");
    auto Pr = [&](std::optional<uint16> xo) {
        if (xo.has_value()) {
          uint16 x = xo.value();
          uint16 y = result[x];
          CPrintf("  %.11g (%s) maps to %.11g (%s)\n",
                  (float)Exp::GetHalf(x), HexColor(x).c_str(),
                  (float)Exp::GetHalf(y), HexColor(y).c_str());
        }
      };
    Pr(example_ntop);
    Pr(example_pton);
    printf("\n");
  } else {
    CPrintf("The sign was " ANSI_GREEN "preserved" ANSI_RESET " "
            ANSI_GREY "(ignoring -0)" ANSI_RESET ".\n");
  }


  // Nice if we could output these in sorted order (by first I guess?)
  CPrintf("\n"
          ANSI_BLUE "%d" ANSI_RESET " distinct values in [-1,-0).\n"
          ANSI_PURPLE "%d" ANSI_RESET " distinct values in [0,1).\n"
          ANSI_YELLOW "%d" ANSI_RESET " distinct values in [-1,1]\n",
          (int)neg_values.size(),
          (int)pos_values.size(),
          (int)values.size());
  if (values.size() < 48) {
    for (const auto &[v, fl] : values) {
      const auto &[first, last] = fl;
      string val = Util::Pad(17, StringPrintf("%.11g", (float)Exp::GetHalf(v)));
      CPrintf("%s (%s): %.11g - %.11g (%s-%s)\n",
              val.c_str(), HexColor(v).c_str(),
              (float)Exp::GetHalf(first), (float)Exp::GetHalf(last),
              HexColor(first).c_str(), HexColor(last).c_str());
    }
  }
}

static Table PrintExpressionStats(const Exp *exp) {
  Timer tabulate_timer;
  Table result = Exp::TabulateExpressionIn(exp, (half)-1.0, (half)1.0);
  CPrintf("Tabulated in " ANSI_YELLOW "%.3fs" ANSI_RESET ".\n",
          tabulate_timer.Seconds());
  PrintExpressionStats(result);

  auto chopo = Choppy::GetChoppy(exp);
  if (chopo.has_value()) {
    const auto &k = chopo.value();
    CPrintf("Choppy: " ANSI_GREEN "%s\n" ANSI_RESET,
            DB::KeyString(k).c_str());
  } else {
    CPrintf(ANSI_RED "Not choppy.\n" ANSI_RESET);
  }

  return result;
}

static int NumDistinctValues(const Table &table) {
  std::unordered_set<uint16> values;
  const half low = (half)-1.0;
  const uint16 ulow = Exp::GetU16(low);
  const half high = (half)1.0;
  const uint16 uhigh = Exp::GetU16(high);
  for (uint16 x = ulow; x != uhigh; x = Exp::NextAfter16(x)) {
    values.insert(table[x]);
  }

  return (int)values.size();
}

// Checks loop invariants for Search.
static bool HasSearchInvariants(const State &state) {
  // Quick check first.
  if (state.table[0x8001] == state.table[0x0000])
    return false;

  // from -epsilon to -1.0 inclusive. Note that we skip
  // 0x8000, which is -0.
  for (uint16 x = 0x8001; x <= 0xBC00; x++) {
    uint16 y = state.table[x];
    half yh = Exp::GetHalf(y);
    if (yh > (half)0.0) return false;
    if (!isfinite(yh)) return false;
  }

  for (uint16 x = 0x0001; x <= 0x3C00; x++) {
    uint16 y = state.table[x];
    half yh = Exp::GetHalf(y);

    if (yh <= (half)0.0) {
      /*
      printf("at %04x we have %04x = %.11g\n",
             x, y, (float)yh);
      */
      return false;
    }
    if (!isfinite(yh)) return false;
  }

  return true;
}

template<class... Fs>
inline int PickRandom(ArcFour *rc, Fs... fs) {
  // PERF: Can we do this without copying?
  std::vector v{std::function<bool(void)>(fs)...};
  // Keep calling until one returns true.
  for (;;) {
    const int idx = RandTo(rc, sizeof...(fs));
    if (v[idx]()) return idx;
  }
}

static half RandHalfIn(ArcFour *rc, half low, half high) {
  double dl = low, dh = high;
  double width = dh - dl;
  return (half)(dl + RandDouble(rc) * width);
}

#if 0
static void Search() {
  // It looks like this might have a solution but that
  // it may require a large number of operations. So
  // this function automates the search.

  // As a loop invariant, we require:
  //  - The sign is preserved. Everything in [-1, -0) is
  //    <= 0, and everything in [0, 1] is > 0. Note that
  //    we ignore the value at -0, which will typically
  //    be the same as the value at 0.
  //  - As a consequence of the above, the value at 0x8001
  //    (smallest magnitude negative value) is not equal
  //    to the value at 0x0000 (+0).
  //  - Every value in [-1, 1] is finite (not nan or inf).

  State state;

  // Initialize. This has to have the properties above
  // (the identity does not, because 0 maps to 0) but
  // we don't have to do this much; it's just a reasonable
  // looking starting point.
  if (true) {
    Allocator alloc;
    ExpWrapper init(&alloc, alloc.Var());
    init = init + 0x0201;
    init = IteratedTimes(init, 0x3c01, 10000);
    init = init * U(1.0 / 8321);
    init = init + 0x2fff;
    init = init - 0x2fff;
    init = init - 0x2fff;
    init = init + 0x2fff;
    init = init * U(4100.0);
    init = init + U(510);
    init = init * U(4);
    init = init - U(2040);
    init = IteratedPlus(init, U(1), 2040);
    init = IteratedPlus(init, U(-1), 4080);
    init = init + U(2040.0);
    state = State(init.exp);
  } else {
    printf("Continuing from disk...\n");
    Allocator alloc;
    string s = Util::ReadFile("search3.txt");
    string error;
    const Exp *e = Exp::Deserialize(&alloc, s, &error);
    CHECK(e != nullptr) << error;
    state = State(e);
  }

  const State start_state = state;

  ArcFour rc(StringPrintf("search.%lld", time(nullptr)));
  auto P = [&rc](float p) {
      return RandFloat(&rc) < p;
    };

  int64 violations = 0;
  int64 attempts = 0;
  int64 improved = 0;
  int64 backtracked = 0;
  int64 restarts = 0;

  {
    Allocator alloc;
    PrintExpressionStats(state.GetExpression(&alloc));
  }

  int prev_distinct_values = NumDistinctValues(state.table);
  Timer loop_timer;

  std::map<int, int64> successes;
  for (;;) {
    if (attempts % 1000 == 0) {
      double aps = attempts / loop_timer.Seconds();
      CPrintf(ANSI_YELLOW "%lld" ANSI_RESET " att ("
              ANSI_PURPLE "%.2f" ANSI_RESET "/s) "
              ANSI_GREEN "%lld" ANSI_RESET " improved "
              ANSI_RED "%lld" ANSI_RESET " viol "
              ANSI_BLUE "%lld" ANSI_RESET " back "
              ANSI_RED "%lld" ANSI_RESET " rest. "
              "Size: " ANSI_CYAN "%d" ANSI_RESET "\n",
              attempts, aps,
              improved,
              violations,
              backtracked,
              restarts,
              (int)state.steps.size());
    }

    if (state.steps.size() == 4000) {
      CPrintf(ANSI_RED "RESTARTING." ANSI_RESET);
      state = start_state;
      prev_distinct_values = NumDistinctValues(state.table);
      restarts++;
    }

    // PERF
    // CHECK(HasSearchInvariants(state));
    // CHECK(prev_distinct_values == NumDistinctValues(state.table));

    State back = state;

    static constexpr uint16_t POS_ONE = 0x3c00;
    static constexpr uint16_t NEG_ONE = 0xbc00;

    attempts++;
    // Apply some random operation.
    const int picked =
    PickRandom(
        &rc,
        [&](){
          // Actually if we pick the direction first, we don't
          // have to take max.
          half cur = fmax(fabs(Exp::GetHalf(state.table[POS_ONE])),
                          fabs(Exp::GetHalf(state.table[NEG_ONE])));

          // this is not actually the highest representable number...
          // we just get close.
          half maximum = (half)65408.0 - cur;

          half amt = RandHalfIn(&rc, Exp::GetHalf(0x0001), maximum);

          uint16 uamt = Exp::GetU16(amt);

          if (P(0.5)) uamt ^= 0x8000;

          state.DoStep(Step(STEP_PLUS, uamt, 1));
          state.DoStep(Step(STEP_PLUS, uamt ^ 0x8000, 1));

          return true;
        },

        [&](){
          // Here the scale applies across the board.
          half cur = fmax(fabs(Exp::GetHalf(state.table[POS_ONE])),
                          fabs(Exp::GetHalf(state.table[NEG_ONE])));

          double frac = 64000.0 / (double)cur;
          if (frac <= 1.5) return false;
          half amt = RandHalfIn(&rc, (half)1.5, (half)frac);
          double inv = 1.0 / (double)amt;
          double uiamt = Exp::GetU16((half)inv);
          uint16 uamt = Exp::GetU16(amt);

          state.DoStep(Step(STEP_TIMES, uamt, 1));
          state.DoStep(Step(STEP_TIMES, uiamt, 1));
          return true;
        },

        [&](){
          half neg_small = Exp::GetHalf(state.table[0x8001]);
          half small = Exp::GetHalf(state.table[0x0000]);

          CHECK(neg_small != small) << neg_small << " " << small <<
            "(Should be impossible with invariants satisfied)";

          // Nothing to do.
          if (neg_small == 0.0)
            return false;

          if (fabs(neg_small) >= fabs(small)) {
            // in this case we could shift, or do the opposite...
            return false;
          }

          int iters = 0;
          while (neg_small != 0) {
            // For any finite value other than zero, this always gives us
            // a different (smaller) finite value.
            neg_small = neg_small * (half)0.5;
            small = small * (half)0.5;
            iters++;
            CHECK(iters < 256);
          }

          if (small == 0.0)
            return false;

          uint16 uinv = Exp::GetU16((half)1.0 / small);

          state.DoStep(Step(STEP_TIMES, Exp::GetU16((half)0.5), iters));
          state.DoStep(Step(STEP_TIMES, uinv, 1));
          return true;
        },

        [&](){

          // PrintExpressionStats(state.table);

          half small = Exp::GetHalf(state.table[0x0000]);

          // Here the scale applies across the board.
          half cur = fmax(fabs(Exp::GetHalf(state.table[POS_ONE])),
                          fabs(Exp::GetHalf(state.table[NEG_ONE])));
          if (cur > (half)62000) {
            // CHECK(false) << cur;
            return false;
          }

          int max_iters = 0;
          while (cur < (half)62000 && max_iters < 16384) {
            cur = cur * Exp::GetHalf(0x3c01);
            max_iters++;
          }

          if (max_iters <= 100) {
            // CHECK(false) << cur << " " << max_iters;
            return false;
          }

          int iters = 100 + RandTo(&rc, max_iters - 100);

          // how big does this make small?
          for (int i = 0; i < iters; i++)
            small = small * Exp::GetHalf(0x3c01);

          uint16 uinvamt = Exp::GetU16((half)(1.0 / (double)small));

          state.DoStep(Step(STEP_TIMES, 0x3c01, iters));
          state.DoStep(Step(STEP_TIMES, uinvamt, 1));

          // PrintExpressionStats(state.table);
          // CHECK(false);

          return true;
        });

    const bool ok = HasSearchInvariants(state);
    if (!ok) {
      state = back;
      violations++;
      continue;
    }
    int new_distinct_values = NumDistinctValues(state.table);

    if (new_distinct_values < prev_distinct_values) {
      improved++;
      successes[picked]++;
      prev_distinct_values = new_distinct_values;
      string succ;
      for (const auto &[k, v] : successes)
        StringAppendF(&succ, " %d:%lld ", k, v);
      printf("Now " ANSI_CYAN "%lld" ANSI_RESET " steps, "
             ANSI_BLUE "%lld" ANSI_RESET " values  "
             ANSI_GREY "(%s)" ANSI_RESET "\n",
             state.steps.size(),
             prev_distinct_values,
             succ.c_str());

      #if 0
      if (new_distinct_values <= 100 &&
          (new_distinct_values % 20 == 0)) {
        string filename =
          StringPrintf("search%d.png",
                       new_distinct_values);
        GradUtil::SaveToImage(1920, state.table,
                              filename);
        printf("Wrote %s\n", filename.c_str());
      }
      #endif

      static constexpr int BEST = 16;
      if (new_distinct_values <= BEST) {
        {
          Allocator alloc;
          const Exp *exp = state.GetExpression(&alloc);
          string ser = Exp::Serialize(exp);

          auto chopo = Choppy::GetChoppy(exp);
          bool is_choppy = chopo.has_value();

          if (is_choppy || new_distinct_values < 5) {
            string filename = StringPrintf("search%d.txt",
                                           new_distinct_values);
            Util::WriteFile(filename, ser);
            printf("Wrote %s\n", filename.c_str());
          }

          if (is_choppy) {
            CPrintf("\n\n" ANSI_GREEN "Choppy!" ANSI_RESET "\n");
            const auto &k = chopo.value();
            CPrintf("Chop: " ANSI_CYAN "%s" ANSI_RESET "\n",
                    Choppy::DB::KeyString(k).c_str());
            CHECK(k[7] != k[8]) << "should not be possible if invariant "
              "is satisfied??";

            CPrintf("Check the file above.\n");
            return;

          } else {
            CPrintf(ANSI_RED "Not choppy" ANSI_RESET ".\n");
          }
        }

        PrintExpressionStats(state.table);
      }

      continue;
    } else if (new_distinct_values == prev_distinct_values) {
      if (P(0.90)) {
        // Usually just backtrack.
        state = back;
        backtracked++;
        continue;
      } else {
        // But sometimes keep it anyway.
        prev_distinct_values = new_distinct_values;
        continue;
      }
    } else {
      // This should not even be possible?
      printf("Impossible?!\n");
      GradUtil::SaveFunctionToFile(back.table, "error-before.png");
      GradUtil::SaveFunctionToFile(state.table, "error-after.png");
      CHECK(false);
    }
  }
}
#endif

static void Study() {
  Allocator alloc;

  ExpWrapper var(&alloc, alloc.Var());


  var = var + U(1.0 / Choppy::GRID);
  var = var * U(128.0);
  /*
  var = var + U(1024);
  var = var - U(1024);
  var = var - U(1024);
  var = var + U(1024);
  */
  var = var * U(1 / 128.0);

  // center and rescale it
  var = ~var;
  var = !var;

  const Exp *exp = var.exp;

  Table result = PrintExpressionStats(exp);
  if (IsCorrect(exp)) {
    printf(AGREEN("== Correct! ==") "\n");
    string out = Exp::Serialize(exp) + "\n";
    Util::WriteFile("manual-chop.txt", out);
    printf("Wrote manual-chop.txt.\n");
    printf("Result: %s\n", out.c_str());
  } else {
    ImageRGBA verbose(2048, 2048);
    verbose.Clear32(0x000000FF);
    GradUtil::Grid(&verbose);
    string message;
    auto target_key = Goal::TargetKey();
    const auto [k, c] = Choppy::VerboseChoppy(exp, &verbose, &message);
    for (int i = 0; i < Choppy::GRID; i++) {
      if (!c[i]) {
        if (k[i] == target_key[i]) printf(ARED(" %d"), k[i]);
        else printf(AYELLOW(" %d") AGREY("(%d)"), k[i], target_key[i]);
      } else {
        if (k[i] == target_key[i]) printf(AGREEN(" %d"), k[i]);
        else printf(ACYAN(" %d") AGREY("(%d)"), k[i], target_key[i]);
      }
    }
    printf("\n");
    printf(ARED("Example failure") ":\n%s\n", message.c_str());
    GradUtil::Graph(result, 0xFFFFFF77, &verbose);
    verbose.Save("verbose.png");
    printf("... wrote verbose.png\n");
  }

  ImageRGBA img(1024, 1024);
  img.Clear32(0x000000FF);
  GradUtil::Grid(&img);
  GradUtil::Graph(result, 0xFFFFFF88, &img);
  img.Save("study.png");
  printf("Wrote study.png\n");
}

static void HalfStats() {
  for (int i = 0; i < 65504; i++) {
    half h = (half)i;
    int j = (int)h;
    if (j != i) {
      printf("%d -> %.11g -> %d\n",
             i, (float)h, j);
      break;
    }
  }

  GradUtil::ForEveryFinite16([](uint16 u) {
      if (u == 0x0000 || u == 0x8000) return;
      half h = Exp::GetHalf(u);

      if ((h * (half)0.5) == h) {
        printf("%.11g (%04x) * 0.5 = same\n",
               (float)h, u);
      }
    });

  printf("OK.\n");
}

int main(int argc, char **argv) {
  AnsiInit();
  // HalfStats();

  // MakeChop();
  // Study();

  Study();
  // Search();

  return 0;
}
