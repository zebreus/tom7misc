#include "expression.h"
#include "timer.h"

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

using Table = Exp::Table;
using uint32 = uint32_t;
using uint8 = uint8_t;
using Allocator = Exp::Allocator;
using DB = Choppy::DB;

static constexpr int IMAGE_SIZE = 1920;

// Annoyingly we have to pass around the allocator pointer...
struct ExpWrapper {
  ExpWrapper(Allocator *alloc,
             const Exp *exp) : alloc(alloc), exp(exp) {}
  Allocator *alloc = nullptr;
  const Exp *exp = nullptr;
};

static ExpWrapper operator+ (ExpWrapper e, uint16_t c) {
  return ExpWrapper(e.alloc, e.alloc->PlusC(e.exp, c));
}

static ExpWrapper operator- (ExpWrapper e, uint16_t c) {
  return ExpWrapper(e.alloc, e.alloc->PlusC(e.exp, 0x8000 ^ c));
}

static ExpWrapper operator* (ExpWrapper e, uint16_t c) {
  return ExpWrapper(e.alloc, e.alloc->TimesC(e.exp, c));
}

static ExpWrapper operator+ (ExpWrapper a, ExpWrapper b) {
  return ExpWrapper(a.alloc, a.alloc->PlusE(a.exp, b.exp));
}

static ExpWrapper operator- (ExpWrapper a, ExpWrapper b) {
  return ExpWrapper(a.alloc,
                    a.alloc->PlusE(a.exp,
                                   a.alloc->Neg(b.exp)));
}

const Exp *TweakExpressions(Allocator *caller_alloc) {
  ArcFour rc(StringPrintf("tweak.%lld", time(nullptr)));
  auto P = [&rc](float f) { return RandFloat(&rc) < f; };
  auto U = [](double d) { return Exp::GetU16((half)d); };

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

    auto RandomExp = [&]() -> ExpWrapper {
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

  std::map<uint16, std::pair<uint16, uint16>> values;
  const half low = (half)-1.0;
  const half high = (half)1.0;
  for (half pos = low; pos < high; pos = nextafter(pos, high)) {
    uint16 x = Exp::GetU16(pos);
    uint16 y = result[x];
    if (values.find(y) == values.end()) {
      values[y] = make_pair(x, x);
    } else {
      auto &[first, last] = values[y];
      if (pos < Exp::GetHalf(first)) first = x;
      if (pos > Exp::GetHalf(last)) last = x;
    }
  }

  printf("%d distinct values in [-1,1]\n", (int)values.size());
  if (values.size() < 48) {
    for (const auto &[v, fl] : values) {
      const auto &[first, last] = fl;
      printf("%.11g (%04x): %.11g - %.11g (%04x-%04x)\n",
             (float)Exp::GetHalf(v), v,
             (float)Exp::GetHalf(first), (float)Exp::GetHalf(last),
             first, last);
    }
  }

  auto chopo = Choppy::GetChoppy(exp);
  if (chopo.has_value()) {
    const auto &k = chopo.value();
    printf("Choppy: %s\n", DB::KeyString(k).c_str());
  } else {
    printf("Not choppy.\n");
  }

  img.Save("manual.png");
  printf("Wrote manual.png");
}




int main(int argc, char **argv) {
  MakeChop();
  return 0;
}
