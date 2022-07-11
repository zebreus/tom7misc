#include "expression.h"
#include "timer.h"

#include <array>
#include <algorithm>
#include <functional>
#include <array>
#include <utility>

#include "half.h"

#include "grad-util.h"
#include "makefn-ops.h"
#include "arcfour.h"
#include "randutil.h"
#include "color-util.h"

#include "choppy.h"
#include "ansi.h"

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

static Table PrintExpressionStats(const Exp *exp) {
  /*
  string exps = Exp::ExpString(exp);
  string sers = Exp::Serialize(exp);
  if (exps.size() > 256) exps = "(long)";
  // but save the serialized version, so I don't lose e.g.
  // a randomly-generated useful one
  CPrintf("For this expression:\n%s\nSerialized as:\n%s\n\n",
          exps.c_str(),
          sers.c_str());
  */
  Timer tabulate_timer;
  Table result = Exp::TabulateExpressionIn(exp, (half)-1.0, (half)1.0);
  CPrintf("Tabulated in " ANSI_YELLOW "%.3fs" ANSI_RESET ".\n",
          tabulate_timer.Seconds());

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

  std::map<uint16, std::pair<uint16, uint16>> values;
  const half low = (half)-1.0;
  const uint16 ulow = Exp::GetU16(low);
  const half high = (half)1.0;
  const uint16 uhigh = Exp::GetU16(high);
  for (uint16 upos = ulow; upos != uhigh; upos = Exp::NextAfter16(upos)) {
    uint16 x = upos;
    uint16 y = result[x];
    if (values.find(y) == values.end()) {
      values[y] = make_pair(x, x);
    } else {
      half pos = Exp::GetHalf(upos);
      auto &[first, last] = values[y];
      if (pos < Exp::GetHalf(first)) first = x;
      if (pos > Exp::GetHalf(last)) last = x;
    }
  }

  // Nice if we could output these in sorted order (by first I guess?)
  CPrintf("\n%d distinct values in [-1,1]\n", (int)values.size());
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


static void Study() {
  Allocator alloc;
  ExpWrapper var(&alloc, alloc.Var());

  // var = var + (uint16)0x247f;

  var = var + 0x0201;
  // var = var - 0x0001;

  // This approach is promising.
  // Each time we do this, it does seem
  // to reduce the number of distinct
  // values. But it eventually seems to
  // bottom out at 152. (And is verrrry slow)


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


int main(int argc, char **argv) {
  AnsiInit();

  // MakeChop();
  Study();
  return 0;
}
