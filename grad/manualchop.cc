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

using Choppy = ChoppyGrid<16>;
using Table = Exp::Table;
using uint32 = uint32_t;
using uint8 = uint8_t;
using Allocator = Exp::Allocator;
using DB = Choppy::DB;

static constexpr int IMAGE_SIZE = 1920;

static inline uint16 U(double d) {
  return Exp::GetU16((half)d);
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
  std::vector<uint16> critical = {
    // -1 and epsilon less
    0xbc00,
    0xbbff,
    // -0.125 and epislon less
    0xb000,
    0xafff,
    // very small negative
    0x8002,
    0x8001,
    // negative zero, our foe!
    0x8000,

    // Positive small values
    0x0000,
    0x0001,
    0x0002,
    // almost .125
    0x2fff,
    0x3000,
    // almost 1
    0x3bff,
    0x3c00,
  };

  ColorPool color_pool;

  // First tally the colors.
  for (uint16 ux : critical) {
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
  for (uint16 ux : critical) {
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

const Exp *TweakExpressions(Allocator *caller_alloc) {
  ArcFour rc(StringPrintf("tweak.%lld", time(nullptr)));
  auto P = [&rc](float f) { return RandFloat(&rc) < f; };

  ImageRGBA img(1920, 1920);
  img.Clear32(0x000000FF);
  GradUtil::Grid(&img);

  int64 quickok = 0;
  int imaged = 0;
  for (int64 iters = 0; true; iters++) {

    if (iters && ((iters < 1000 && iters % 10 == 0) || iters % 10000 == 0)) {
      printf("%d iters, %.3f%% qok\n", iters, (quickok * 100.0) / iters);
    }

    Allocator alloc;
    ExpWrapper ret(&alloc, alloc.Var());

    auto RandBetween = [&](uint16 lo, uint16 hi) {
        int range = hi - lo;
        int x = RandTo(&rc, range);
        return (uint16)(lo + x);
      };

    auto RandomExp = [&]() -> ExpWrapper {
        ExpWrapper var(&alloc, alloc.Var());

        float pp = RandFloat(&rc);
        uint16 r = (uint16)RandBetween(0, 0x2500);
        if (pp < 0.33) {
          var = var + r;
          if (P(0.5)) var = var - r;
        } else if (pp < 0.66) {
          var = var - r;
          if (P(0.5)) var = var + r;
        }

        var = var * U(0.125 / 4.0);

        float pp2 = RandFloat(&rc);
        if (pp2 < 0.33) {
          var = var + (uint16)RandBetween(0x0000, 0x0100);
          var = var - U(-0.125);
        } else if (pp2 < 0.66) {
          var = var - (uint16)RandBetween(0x0000, 0x0100);
          var = var + U(-0.125);
        }


        for (int i = 0; i < (int)RandBetween(59, 66); i++)
          var = var + (U(1.0) + i);

        var = ~var;
        return var;
      };

    [[maybe_unused]]
    auto RandomExp2 = [&]() -> ExpWrapper {
        do {
          // XXX do these in a random order.
          if (P(0.40)) {
            uint16_t small = RandTo(&rc, 0x68e);
            ret = ret + small;
            if (P(0.60)) {
              ret = ret - small;
              if (P(0.66)) {
                ret = ret - small;
                ret = ret + small;
              }
            }
          }

          if (P(0.50)) {
            int num = RandTo(&rc, 64);
            while (num--)
              ret = ret + U(RandDouble(&rc) * 2.0 - 1.0);
          }

          if (P(0.50)) {
            ret = ret * U(1.0 + RandDouble(&rc) * 64.0);
          }

          if (P(0.40)) {
            int ones = P(0.33) ? 0 : RandTo(&rc, 512);

            // PERF: iterated
            for (int i = 0; i < ones; i++)
              ret = ret + U(1.0);
            for (int i = 0; i < ones; i++)
              ret = ret + U(-1.0);
          }

          if (P(0.50)) {
            ret = ret * U(1.0 + RandDouble(&rc) * 64.0);
          }

          if (P(0.20)) {
            ret = ret * U(1024.0);
            ret = ret * U(512.0);
            double d = RandDouble(&rc) * 128.0;
            ret = ret * U(d);
            if (P(0.20)) {
              ret = ret + U(1.0);
              ret = ret + U(1.0);
              ret = ret + U(-1.0);
              ret = ret + U(-1.0);
              ret = ret + U(-1.0);
              ret = ret + U(1.0);
            }

            ret = ret * U(1.0 / d);
            ret = ret * U(1.0/512.0);
            ret = ret * U(1.0/1024.0);
          }

          if (P(0.50)) {
            ret = ret * U(1.0 / (RandDouble(&rc) * 512.0));
          }

          if (P(0.50)) {
            int num = 1 + RandTo(&rc, 64);
            while (num--)
              ret = ret + U(RandDouble(&rc) * 16.0 - 8.0);
          }

          // If none of the dice came up, it will be useless, so loop.
        } while (ret.exp->type == VAR);

        return ret;
      };

    ret = RandomExp();
    while (P(0.33))
      ret = ret - RandomExp();

    const Exp *e = ret.exp;
    // Quick check
    uint16 ypos = Exp::EvaluateOn(e, Exp::GetU16((half)(1.0 / 16.0)));
    uint16 yneg = Exp::EvaluateOn(e, Exp::GetU16((half)(-1.0 / 16.0)));

    if (ypos != yneg) {
      quickok++;

      // offset so it's within range
      e = alloc.PlusC(e, ypos ^ 0x8000);

      static constexpr int NUM_TO_PLOT = 1000;
      if (imaged < NUM_TO_PLOT) {
        Table result = Exp::TabulateExpressionIn(e, (half)-1.0, (half)+1.0);
        const auto [r, g, b] = ColorUtil::HSVToRGB(RandFloat(&rc), 1.0, 1.0);
        uint32 color = ColorUtil::FloatsTo32(r, g, b, 0x11 / (float)0xFF);
        GradUtil::Graph(result, color, &img);
        printf("%");
      } else if (imaged == NUM_TO_PLOT) {
        img.Save("tweak.png");
        printf("Wrote tweak.png\n");
      }
      imaged++;

      auto chopo = Choppy::GetChoppy(e);
      if (chopo.has_value()) {
        const auto &k = chopo.value();
        if (k[7] != k[8]) {
          printf("k: %s\n"
                 "%s",
                 DB::KeyString(k).c_str(),
                 Exp::ExpString(e).c_str());
          return caller_alloc->Copy(e);
        }
      }
    }
  }
}

// Just need something that has different and nonzero
// values around 0, and we should be done. Seems like
// this should be easy to do manually?
static void MakeChop() {
  ImageRGBA img(IMAGE_SIZE, IMAGE_SIZE);
  img.Clear32(0x000000FF);
  GradUtil::Grid(&img);

  DB db;

  // Allocator *alloc = &db.alloc;
  Allocator *alloc = &db.alloc;

  #if 0
  half scale = (half)4.0;
  int iters = 8;

  half offset = (half)64.0;
  int oiters = 8;

  const Exp *exp =
      alloc->TimesC(
        alloc->PlusC(
            alloc->PlusC(
                alloc->TimesC(alloc->Var(),
                              Exp::GetU16(scale),
                              iters),
                Exp::GetU16(offset),
                oiters),
            Exp::GetU16(-offset),
            oiters),
        Exp::GetU16((half)1.0 / scale),
        iters);
#endif

  #if 0
  half offset = (half)256;
  int iters = 1;

  // This is VERY close.. maybe with some local search it could
  // be made to work?
  const Exp *exp =
    alloc->PlusC(
    alloc->PlusC(
        alloc->PlusC(
            alloc->PlusC(
                alloc->PlusC(
                    alloc->PlusC(
                        alloc->Var(),
                        Exp::GetU16((half)(1 - 0.0625*2))),
                    Exp::GetU16(offset),
                    iters),
                Exp::GetU16(-offset),
                iters),
            Exp::GetU16(-offset),
            iters),
        Exp::GetU16(offset),
        iters),
    Exp::GetU16((half)-1.0));
  #endif


  #if 0
  half offset = (half)511;

  uint16 one = Exp::GetU16((half)1);
  uint16 negone = Exp::GetU16((half)-1);

  uint16 eps = 0x0001;

  ExpWrapper base =
    ((ExpWrapper(alloc->Var()) + one + one + one + negone) *
     Exp::GetU16((half)(1.0/512.0)) *
     Exp::GetU16((half)(1.0/512.0)) *
     Exp::GetU16((half)(1.0/64))) *
    Exp::GetU16((half)64.0) *
    Exp::GetU16((half)512.0) *
    Exp::GetU16((half)512.0) + one + negone;

  ExpWrapper ew = base;
  /*
    base +
    one + one + one +
    negone + negone + negone;
  */
  /*
    Exp::GetU16(-offset) +
    Exp::GetU16(-offset) +
    Exp::GetU16(offset)
  */
  const Exp *exp = ew.exp;

  // This is VERY close.. maybe with some local search it could
  // be made to work?
    /*
  const Exp *exp =
    alloc->PlusC(
    alloc->PlusC(
        alloc->PlusC(
            alloc->PlusC(
                alloc->PlusC(
                    alloc->PlusC(
                        alloc->Var(),
                        Exp::GetU16((half)(1 - 0.0625*2)) + 0),
                    Exp::GetU16(offset),
                    iters),
                Exp::GetU16(-offset) + 1,
                iters),
            Exp::GetU16(-offset),
            iters),
        Exp::GetU16(offset),
        iters),
    Exp::GetU16((half)-1.0));
    */
  #endif

#if 0
  half offset = (half)64.0;
  int iters = 8;

  const Exp *exp =
    alloc->PlusC(
        alloc->PlusC(
            // alloc->PlusC(
            alloc->Var(),
            // 0xb17f),
            Exp::GetU16(offset),
            iters),
        Exp::GetU16(-offset),
        iters);
#endif

#if 0
  half offset = (half)1024;

  const Exp *exp = [&]() {
      int64 parity = 0;
      half lo0 = (half)0.1; // (half)-1;
      half hi0 = (half)1;
      for (half pos0 = lo0; pos0 < hi0; pos0 = nextafter(pos0, hi0)) {
        parity++;
        if (parity % 100 == 0)
          printf("%.6g/%.6g\n", (float)pos0, (float)hi0);
        half lo1 = (half)0.95;
        half hi1 = (half)1.05;
        for (half pos1 = lo1; pos1 < hi1; pos1 = nextafter(pos1, hi1)) {

          for (int dx = -100; dx < 100; dx++) {
          const Exp *e =
            alloc->PlusC(
                alloc->PlusC(
                    alloc->PlusC(
                        alloc->PlusC(
                            alloc->PlusC(
                                alloc->PlusC(
                                    alloc->TimesC(
                                        alloc->PlusC(
                                            alloc->Var(),
                                            pos0),
                                        pos1),
                                    Exp::GetU16((half)(1 - 0.0625*8)) + dx),
                                Exp::GetU16(offset)),
                            Exp::GetU16(-offset)),
                        Exp::GetU16(-offset)),
                    Exp::GetU16(offset)),
                Exp::GetU16((half)-1.0));

          // Quick check
          uint16 ypos = Exp::EvaluateOn(e, Exp::GetU16((half)(1.0 / 16.0)));
          uint16 yneg = Exp::EvaluateOn(e, Exp::GetU16((half)(-1.0 / 16.0)));

          if (ypos != yneg) {
            auto chopo = Choppy::GetChoppy(e);
            if (chopo.has_value()) {
              const auto &k = chopo.value();
              if (k[7] != k[8]) {
                printf("pos0: %04x pos1: %04x dx: %d\n",
                       Exp::GetU16(pos0), Exp::GetU16(pos1),
                       dx);
                return e;
              }
            }
          }
          }
        }
      }
      printf("Failed\n");
      return alloc->Var();
    }();
  #endif

  const Exp *exp = TweakExpressions(alloc);

  Table result = Exp::TabulateExpression(exp);
  GradUtil::Graph(result, 0xFFFFFF88, &img);

  // Note this also tabulates.
  PrintExpressionStats(exp);

  img.Save("manual.png");
  printf("Wrote manual.png");
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

static void Study() {
  Allocator alloc;

  /*
  // This will flatten out everything in the middle,
  // but doesn't move values > 32768.

  var = var + 0x0201;
  var = var * U(65000);
  var = IteratedPlus(var, U(15.9921875), 10000);
  var = IteratedPlus(var, U(-15.9921875), 10000);
  */

  #if 0
  // Does not work.
  ExpWrapper e1(&alloc, alloc.Var());
  e1 = e1 + 0x0201;
  e1 = e1 * U(32768);
  e1 = IteratedPlus(e1, U(-8.0), 10000);
  e1 = e1 * U(1/128.0);

  ExpWrapper e2(&alloc, alloc.Var());
  e2 = e2 + 0x0201;
  e2 = e2 * U(32768);
  e2 = IteratedPlus(e2, U(8.0), 10000);
  e2 = e2 * U(1/128.0);

  ExpWrapper var = e2 - e1;
  var = var * U(1/512.0);

  var = ~ var;
  #endif

  ExpWrapper var(&alloc, alloc.Var());

  // Based on the below.
  var = var + 0x0201;
  var = IteratedTimes(var, 0x3c01, 10000);
  var = var * U(1.0 / 8321);
  var = var + 0x2fff;
  var = var - 0x2fff;
  var = var - 0x2fff;
  var = var + 0x2fff;
  var = var * U(4100.0);
  var = var + U(510);
  var = var * U(4);
  var = var - U(2040);
  var = IteratedPlus(var, U(1), 2040);
  var = IteratedPlus(var, U(-1), 4080);
  var = var + U(2040.0);


#if 0
  // also promising, and could come back to this!


  // var = var + (uint16)0x247f;

  // First we shift this over. This places
  // small values on both sides of zero.
  var = var + 0x0201;

  // This iterated multiplication stretches
  // the line out nonlinearly, because of
  // different rounding error.
  var = IteratedTimes(var, 0x3c01, 10000);
  // This division reduces the number of
  // distinct values. It also rounds some of
  // the positive values less than zero to
  // zero.
  var = var * U(1.0 / 8321);

  // Shifting up and back adds more discretization
  // error, but this is the largest value that
  // does not round the small values near zero
  // (0x027e, 0x027f) to zero. They become 0x400.
  var = var + 0x2fff;
  var = var - 0x2fff;

  // Same thing on the other side.
  var = var - 0x2fff;
  var = var + 0x2fff;

  var = var * U(4100.0);
  // now the small values are slightly more than 0.25

  // So when shifting, we preserve the quarter.
  var = var + U(510.0);

  // Values look like this now.
  /*
-1                (bc00) yields -3590             (eb03)
-0.99951171875    (bbff) yields -3586             (eb01)
-0.125            (b000) yields -1.75             (bf00)
-0.12493896484    (afff) yields -1.75             (bf00)
-1.1920928955e-07 (8002) yields 510               (5ff8)
-5.9604644775e-08 (8001) yields 510               (5ff8)
-0                (8000) yields 510.25            (5ff9)
0                 (0000) yields 510.25            (5ff9)
5.9604644775e-08  (0001) yields 510.25            (5ff9)
1.1920928955e-07  (0002) yields 510.25            (5ff9)
0.12493896484     (2fff) yields 1022.5            (63fd)
0.125             (3000) yields 1022.5            (63fd)
0.99951171875     (3bff) yields 4608              (6c80)
1                 (3c00) yields 4608              (6c80)
  */

  var = var * U(4.0);
  // now we have 2040 < 0, and 2041 at zero.
  var = var - U(2040.0);

  var = var * U(0.25);
  var = var + U(511.0);

  var = var * U(11.66);
  var = var - U(5959);

  var = var * U(1 / 999.0);
  var = var * U(1 / 47.8125);

  var = IteratedTimes(var, 0x3c01, 10000);

  for (int i = 0; i < 512; i += 3) {
    var = var - U(i);
    var = var + U(i);
  }

  var = var * U(1 / 3.6);
  var = var - U(511);
  var = var * U(8.0);

  var = IteratedPlus(var, U(2.0), 2044);

  var = IteratedPlus(var, U(1.0), 2048);

  var = var - U(2048);
  var = IteratedPlus(var, U(-1.0), 2000);
  var = var + U(2000);

  var = var * U(1.0 / 12360.0);

  var = ~ var;

  var = var * U(20000);
#endif


#if 0
  // This approach is promising.
  // Each time we do this, it does seem
  // to reduce the number of distinct
  // values. But it eventually seems to
  // bottom out at 152. (And is verrrry slow)

  // I think this was the starting condition?
  // check r4659.
  var = var + 0x0201;
  // 10 loops, 432 distinct values, 25.5s
  // 20 loops, 152 values, 53.159s
  // 30 loops, 152 values, 88.814s
  for (int j = 0; j < 30; j++) {

    // with 100 iters, 5262
    for (int i = 0; i < 100; i++) {

      int iters = 10000;
      var = IteratedTimes(var, 0x3c01, iters);
      // for (int i = 0; i < iters; i++)
      // var = var * 0x3c01;

      var = var * U(1.0 / 1024);
      var = var * U(1.0 / 8.0);

      var = IteratedTimes(var, 0x3c01, iters);
      // for (int i = 0; i < iters; i++)
      // var = var * 0x3c01;

      var = var * U(1.0 / 1024);
      var = var * U(1.0 / 8.0);

      var = IteratedTimes(var, 0x3c01, iters);
      // for (int i = 0; i < iters; i++)
      // var = var * 0x3c01;

      var = var * U(1.0 / 1024);
      var = var * U(1.0 / 8.0);

      var = var * U(1.0 / 1.03125);

    }

    var = var * U(1.0 / 2.16796875);
  }
  #endif

#if 0

  var = var * U(1024.0);
  // var = var * U(1.0 / 1024.);

  var = var + U(10.0);
  var = var - U(10.0);

  var = var + U(64.0);
  var = var - U(64.0);

  // Now we have a lot of values (from old tiny ones)
  // but -1 is -1056, 1 is 1056, so we have some room
  // to scale this out.
  var = var * U(60.0);
  var = var * U(1 / 3.75);

  var = var + U(512.0);
  var = var - U(512.0);
#endif

  /*
  var = var + U(512.0);
  var = var - U(512.0);

  var = var + U(4096.0);
  var = var - U(4096.0);
  */
  /*
  var = var + 1;
  var = var * U(1.0 / 2.0);
  */

  // var = var * U(1024);
  // var = var * 0x000c;

  #if 0
  var = var + 2;

  var = var * U(3.0);
  // var = var * U(1.0 / 3.0);

  //  = var - 0x0300;

  int iters = 24;

  for (int i = 0; i < iters; i++)
    var = var * U(1.0 / 2.0);

  for (int i = 0; i < iters; i++)
    var = var * U(2.0);
  #endif

  // var = var + (uint16)0x0030;

  /*
  var = var - U(-0.125);

  for (int i = 0; i < 63 ; i++)
    var = var + (U(1.0) + i);
  */

  /*
  for (int i = 0; i < 1024 ; i++)
    var = var - U(1.0);
  */

  #if 0
  var = var * U(64);
  var = var * U(1.0/64);

  var = var + U(1024);
  var = var + U(-1024);

  var = var + U(-1024);
  var = var + U(1024);
  #endif

  // var = ~var;
  const Exp *exp = var.exp;

  Table result = PrintExpressionStats(exp);

  ImageRGBA img(1024, 1024);
  img.Clear32(0x000000FF);
  GradUtil::Grid(&img);
  GradUtil::Graph(result, 0xFFFFFF88, &img);
  img.Save("study.png");
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
  HalfStats();

  // MakeChop();
  // Study();

  Search();

  return 0;
}
