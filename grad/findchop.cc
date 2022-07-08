
#include <optional>
#include <array>

#include "base/logging.h"
#include "base/stringprintf.h"
#include "image.h"
#include "expression.h"
#include "half.h"
#include "hashing.h"

#include "choppy.h"
#include "grad-util.h"
#include "color-util.h"

// Makes a database of "choppy" functions.

using DB = Choppy::DB;
using Allocator = Exp::Allocator;
using Table = Exp::Table;

static const Exp *GetOp5(Exp::Allocator *alloc,
                         const std::array<int, 2> &ints,
                         const std::array<double, 3> &dbls,
                         const Exp::Table &target) {
  const auto &[off1, off2] = ints;
  const auto &[prescale, postscale, xd] = dbls;

  const Exp *fa =
    alloc->TimesC(
        // x - 4
        alloc->PlusC(alloc->TimesC(alloc->Var(),
                                   Exp::GetU16((half)prescale)), off1),
        // * 0.999 ...
        0x3bffu,
        200);

  const Exp *fb =
    alloc->TimesC(
        // x - 4
        alloc->PlusC(alloc->TimesC(alloc->Var(),
                                   Exp::GetU16((half)prescale)), off2),
        // * 0.999 ...
        0x3bffu,
        300);

  const Exp *f0 =
    alloc->TimesC(
        alloc->PlusE(fa,
                    alloc->Neg(fb)),
        Exp::GetU16((half)postscale));

  // Pick a point at which to analytically set the y values
  // equal (within the limits of precision).
  uint16_t x = Exp::GetU16((half)xd);
  half fy = Exp::GetHalf(Exp::EvaluateOn(f0, x));
  half ty = Exp::GetHalf(target[x]);
  uint16 yneg = Exp::GetU16(ty - fy);

  return alloc->PlusC(f0, yneg);
}


static void Explore5(DB *db) {
  static constexpr int IMAGE_SIZE = 1080;

  Table target =
    Exp::MakeTableFromFn([](half x) {
        return (half)0.0;
      });

  ImageRGBA img(IMAGE_SIZE, IMAGE_SIZE);
  img.Clear32(0x000000FF);
  GradUtil::Grid(&img);

  static constexpr int STROBE = 50;

  const std::array<int, 2> BASE_INTS = {49758, 49152};
  const std::array<double, 3> BASE_DOUBLES =
    {0.0039, -3.9544, 0.0760};

  Exp::Allocator *alloc = &db->alloc;

  // Can return nullptr if not feasible.
  auto MakeExp = [&](int a, int b,
                     double x, double y) -> const Exp * {
      const double z = 0.0;
      const std::array<int, 2> INTS = {a, b};
      const std::array<double, 3> DOUBLES = {x, y, z};

      // PERF: Use a simplified version of op5 without the xd param.
      // Derive the scale.
      const Exp *exp0 =
        GetOp5(alloc, INTS, DOUBLES, target);

      const half mid_x = (half)(1.0/(Choppy::GRID * 2.0));

      // First put x=0 (really, the midpoint of zero) at y=0.
      uint16 offset = Exp::EvaluateOn(exp0, mid_x);

      // flip sign bit so that we are subtracting the computed
      // offset.
      const Exp *exp1 = alloc->PlusC(exp0, 0x8000 ^ offset);

      // The result should be exactly zero (or negative zero).
      const uint16 new_offset = Exp::EvaluateOn(exp1, mid_x) & 0x7FFF;
      if (new_offset != 0x000) {
        printf("After offsetting, unexpectedly got %04x??\n",
               new_offset);
        return nullptr;
      }

      // XXX this loop does not necessarily find a scale for a function
      // that can in fact be scaled!
      // Now find any nonzero point.

      // If zero, we will fail.
      half multiplier = (half)0.0f;

      for (int i = 0; i < Choppy::GRID; i++) {
        half x = (half)((i / (double)(Choppy::GRID/2)) - 1.0);
        x += (half)(1.0/(Choppy::GRID * 2.0));

        // Ignore the sign so that scaling preserves it.
        half y = fabs(Exp::GetHalf(Exp::EvaluateOn(exp1, Exp::GetU16(x))));
        if (y != (half)0.0) {
          // ... and just scale it to be 1.0.
          half yinv = (half)(1.0 / (Choppy::GRID/2.0)) / y;

          /*
          const Exp *exp2 = alloc->TimesC(exp1, Exp::GetU16(yinv));
          const half newy =
            Exp::GetHalf(Exp::EvaluateOn(exp2, Exp::GetU16(x)));
            printf("Scale y = %.5f by %.5f, expect %.5f got %.5f\n",
            (float)y, (float)yinv, (float)(y * yinv), (float)newy);
          */

          if (yinv > multiplier) multiplier = yinv;
        }
      }

      if (multiplier > (half)0.0) {
        return alloc->TimesC(exp1, Exp::GetU16(multiplier));
      }

      // Was all zero; this is common but not feasible!
      return nullptr;
    };

  int64 infeasible = 0;
  int64 added = 0;
  int64 done = 0;

  // run a full grid. bc00 = -1, c600 = -6.
  const int64 SIZE = 0xc600 - 0xbc00;
  const int64 TOTAL = SIZE * SIZE;

  for (int i1 = 0xbc00; i1 < 0xc600; i1++) {
    for (int i2 = 0xbc00; i2 < 0xc600; i2++) {
      done++;
      if (done % 25000 == 0) {

        fprintf(stderr,
                "Ran %lld/%d. (%.1f%%) %lld added, %lld infeasible (%.1f%%)\n",
                done, TOTAL, (done * 100.0) / TOTAL, added, infeasible,
                (infeasible * 100.0) / done);
      }

      auto [d1, d2, d3_] = BASE_DOUBLES;

      const Exp *exp = MakeExp(i1, i2, d1, d2);
      if (!exp) {
        infeasible++;
        continue;
      }

      if (db->Add(exp)) {
        added++;

        float r = (i1 - 0xbc00) / (float)(0xc600 - 0xbc00);
        float g = (i2 - 0xbc00) / (float)(0xc600 - 0xbc00);

        const uint32 color = ColorUtil::FloatsTo32(r, g, 0.25f, 0.05f);
        Table result = Exp::TabulateExpression(exp);
        GradUtil::Graph(result, color, &img);
      }
    }
  }


  {
    // original
    auto [i1, i2] = BASE_INTS;
    auto [d1, d2, d3_] = BASE_DOUBLES;
    const Exp *exp = MakeExp(i1, i2, d1, d2);
    Table result = Exp::TabulateExpression(exp);
    GradUtil::Graph(result, 0xFFFFFFAA, &img, 0);
    GradUtil::Graph(result, 0xFFFFFFAA, &img, 1);
    fprintf(stderr, "%s\n", Exp::ExpString(exp).c_str());

    for (int i = 0; i < 16; i++) {
      half x = (half)((i / (double)8) - 1.0);
      x += (half)(1.0/32.0);

      half y = Exp::GetHalf(Exp::EvaluateOn(exp, Exp::GetU16(x)));

      double yi = ((double)y + 1.0) * 8.0;
      int ypos =
        // put above the line
        -16 +
        // 0 is the center
        (IMAGE_SIZE / 2) +
        (double)-y * (IMAGE_SIZE / 2);
      img.BlendText32(i * (IMAGE_SIZE / 16) + 8,
                      ypos,
                      0xFFFFFFAA,
                      StringPrintf("%.5f", yi));
    }
  }

  img.Save("findchop5.png");
}

static void SeedDB(DB *db) {
  Allocator *alloc = &db->alloc;
  auto T = [alloc](const Exp *e, uint16_t c, uint16_t iters) {
      return alloc->TimesC(e, c, iters);
    };
  auto P = [alloc](const Exp *e, uint16_t c, uint16_t iters) {
      return alloc->PlusC(e, c, iters);
    };
  auto E = [alloc](const Exp *a, const Exp *b) {
      return alloc->PlusE(a, b);
    };
  const Exp *V = alloc->Var();

#include "chop-db.h"

}

int main(int argc, char **argv) {
  DB db;
  SeedDB(&db);

  // Explore5(&db);

  printf("\n\ndb has %d distinct fns\n", (int)db.fns.size());

  db.Dump();
  return 0;
}
