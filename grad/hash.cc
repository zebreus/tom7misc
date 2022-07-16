
#include <string>
#include <cmath>

#include "base/logging.h"
#include "base/stringprintf.h"

#include "util.h"
#include "image.h"
#include "bounds.h"
#include "opt/optimizer.h"
#include "half.h"
#include "color-util.h"
#include "arcfour.h"
#include "randutil.h"
#include "threadutil.h"

#include "grad-util.h"
#include "expression.h"
#include "choppy.h"
#include "ansi.h"

using namespace std;

using namespace half_float::literal;

using DB = Choppy::DB;
using Table = GradUtil::Table;
using Step = GradUtil::Step;
using State = GradUtil::State;

static const uint8_t PERMS[16][16] = {
  {11, 0, 4, 9, 14, 12, 2, 1, 7, 8, 5, 15, 13, 6, 3, 10},
  {14, 0, 6, 9, 12, 13, 5, 2, 10, 1, 4, 3, 15, 8, 7, 11},
  {9, 8, 7, 4, 5, 13, 3, 11, 10, 6, 2, 12, 14, 15, 0, 1},
  {1, 10, 8, 9, 5, 3, 13, 6, 4, 14, 7, 0, 2, 15, 11, 12},
  {11, 4, 15, 10, 8, 1, 0, 9, 12, 5, 14, 3, 6, 7, 2, 13},
  {11, 10, 15, 5, 0, 12, 4, 13, 14, 1, 3, 9, 2, 6, 7, 8},
  {6, 10, 3, 4, 15, 1, 8, 0, 9, 13, 2, 5, 14, 11, 7, 12},
  {3, 4, 15, 12, 9, 13, 5, 11, 2, 0, 6, 10, 8, 1, 7, 14},
  {5, 6, 3, 12, 11, 2, 9, 13, 7, 15, 0, 8, 14, 1, 4, 10},
  {3, 2, 9, 10, 14, 7, 4, 15, 12, 5, 13, 1, 6, 8, 11, 0},
  {3, 15, 12, 1, 11, 6, 9, 0, 14, 2, 5, 7, 13, 8, 4, 10},
  {3, 10, 12, 8, 13, 1, 9, 11, 6, 4, 2, 14, 7, 5, 15, 0},
  {2, 3, 10, 14, 8, 0, 5, 4, 1, 7, 13, 9, 11, 15, 6, 12},
  {5, 11, 1, 13, 3, 15, 0, 6, 9, 12, 4, 10, 14, 8, 7, 2},
  {15, 13, 10, 11, 12, 4, 0, 3, 1, 14, 8, 6, 2, 7, 5, 9},
  {13, 12, 7, 1, 2, 4, 3, 0, 5, 11, 6, 10, 15, 9, 8, 14},
};

static Table tables[16];

static inline half GetHalf(uint16 u) {
  return GradUtil::GetHalf(u);
}

static inline uint16 GetU16(half h) {
  return GradUtil::GetU16(h);
}

// Run one of the 16 substitution tables. A function from
// (-1, 1) to (-1, 1).
static half Subst(int n, half h) {
  const half HALF_GRID = (half)(0.5 / (Choppy::GRID * 2));
  return GetHalf(tables[n][GetU16(h)]) + HALF_GRID;
}

struct HashState {
  // Four halves in (-1,1].
  half a = GetHalf(0x38b9);
  half b = GetHalf(0x2e36);
  half c = GetHalf(0x159c);
  half d = GetHalf(0x00bc);
};

string StateString(HashState s) {
  return StringPrintf("[%.5g %.5g %.5g %.5g]",
                      (float)s.a,
                      (float)s.b,
                      (float)s.c,
                      (float)s.d);
}

HashState NextState(HashState s) {
  // Only linear functions!

  CHECK(s.a > -1.0_h && s.a < 1.0_h) << s.a;
  CHECK(s.b > -1.0_h && s.b < 1.0_h) << s.b;
  CHECK(s.c > -1.0_h && s.c < 1.0_h) << s.c;
  CHECK(s.d > -1.0_h && s.d < 1.0_h) << s.d;

  // red
  half aa = Subst(1, s.c);
  // green
  half bb = Subst(2, s.a);
  // blue
  half cc = Subst(3, s.d);
  // purple
  half dd = Subst(4, s.b);

  CHECK(aa > -1.0_h && aa < 1.0_h) << StateString(s) << " " << aa;
  CHECK(bb > -1.0_h && bb < 1.0_h) << StateString(s) << " " << bb;
  CHECK(cc > -1.0_h && cc < 1.0_h) << StateString(s) << " " << cc;
  CHECK(dd > -1.0_h && dd < 1.0_h) << StateString(s) << " " << dd;

  half xx = aa;
  half yy = bb;
  half zz = cc;
  half ww = dd;

  s.a = xx;
  s.b = yy;
  s.c = zz;
  s.d = ww;

  return s;
}

static std::vector<const Exp *> MakeExps(DB *basis) {
  std::vector<const Exp *> ret;
  for (int p = 0; p < 16; p++) {
    std::vector<const Exp *> parts;
    for (int col = 0; col < 16; col++) {
      // magnitude at this position (as number of GRID cells).
      int m = PERMS[p][col] - 8;
      if (m != 0) {
        DB::key_type key = DB::BasisKey(col);
        auto it = basis->fns.find(key);
        CHECK(it != basis->fns.end()) << "Incomplete basis: "
                                      << DB::KeyString(key);
        const Exp *e = it->second;
        if (m != 1) {
          e = basis->alloc.TimesC(e, Exp::GetU16((half)m), 1);
        }
        parts.push_back(e);
      }
    }
    CHECK(!parts.empty()) << "Function was zero everywhere?";
    const Exp *perm = parts[0];
    for (int i = 1; i < parts.size(); i++) {
      perm = basis->alloc.PlusE(perm, parts[i]);
    }
    ret.push_back(perm);
  }
  return ret;
}

static void InitTables(const std::vector<const Exp *> &exps) {
  CHECK(exps.size() == 16);
  //  for (int i = 0; i < exps.size(); i++) {
  ParallelComp(16, [&](int i) {
      CPrintf("Table " ACYAN("%d") ":\n", i);
      tables[i] = Exp::TabulateExpression(exps[i]);
    }, 4);
}

int main(int argc, char **argv) {
  AnsiInit();
  DB db;
  db.LoadFile("basis.txt");

  std::vector<const Exp *> perm_exps = MakeExps(&db);
  InitTables(perm_exps);
  printf("Initialized 16 tables.\n");

  static constexpr int IMG_WIDTH = 1920 / 2;
  static constexpr int IMG_HEIGHT = 1080 / 2;
  ImageRGBA out(IMG_WIDTH, IMG_HEIGHT);
  out.Clear32(0x000000FF);

  HashState hs;
  for (int x = 0; x < IMG_WIDTH; x++) {
    auto Plot = [&out, x](half h, uint32_t color) {
        double y = (IMG_HEIGHT / 2.0) - h * ((IMG_HEIGHT * 0.9) / 2.0);
        out.BlendPixel32(x, std::clamp((int)std::round(y), 0, IMG_HEIGHT - 1),
                         color);
      };

    Plot(0.0_h, 0xFFFFFF22);
    Plot(1.0_h, 0xFFFFFF22);

    Plot(hs.a, 0xFF0000AA);
    Plot(hs.b, 0x00FF00AA);
    Plot(hs.c, 0x0000FFAA);
    Plot(hs.d, 0xAA33AAAA);
    hs = NextState(hs);
  }

  out.ScaleBy(3).Save("hash.png");

  return 0;
}
