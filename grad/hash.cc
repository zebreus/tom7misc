
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

using namespace std;

using namespace half_float::literal;

using Table = GradUtil::Table;
using Step = GradUtil::Step;
using State = GradUtil::State;

static inline half GetHalf(uint16 u) {
  return GradUtil::GetHalf(u);
}

static inline uint16 GetU16(half h) {
  return GradUtil::GetU16(h);
}

State table1;
static half F1(half h) {
  return GetHalf(table1.table[GetU16(h)]);
}

State table2;
static half F2(half h) {
  // This function is only interesting in the full [-1,1]
  // range, so stretch to that:
  half h2 = h * 2.0_h - 1.0_h;
  half h3 = GetHalf(table2.table[GetU16(h2)]);
  // and squash back
  half h4 = (h3 + 1.0_h) * 0.5_h;
  // Finally, don't center on zero because F1 does that.
  return h4 - 0.25_h;
}

/*
static const half C1 = GetHalf(0x2fb1);
static const half C1INV = (half)((half)1.0f / C1);
*/

struct HashState {
  // Four halves in (-1,1).
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

  CHECK(s.a >= 0.0_h && s.a <= 1.0_h) << s.a;
  CHECK(s.b >= 0.0_h && s.b <= 1.0_h) << s.b;
  CHECK(s.c >= 0.0_h && s.c <= 1.0_h) << s.c;
  CHECK(s.d >= 0.0_h && s.d <= 1.0_h) << s.d;

  // red
  half aa = F1(s.c);
  // green
  half bb = F2(1.0_h - s.a);
  // blue
  half cc = 1.0_h - F1(s.d);
  // purple
  half dd = 1.0_h - F2(s.b);

  CHECK(aa >= 0.0_h && aa <= 1.0_h) << StateString(s) << " " << aa;
  CHECK(bb >= 0.0_h && bb <= 1.0_h) << StateString(s) << " " << bb;
  CHECK(cc >= 0.0_h && cc <= 1.0_h) << StateString(s) << " " << cc;
  CHECK(dd >= 0.0_h && dd <= 1.0_h) << StateString(s) << " " << dd;

  /*
  half xx = GetHalf(0x3651) * bb + (1.0_h - GetHalf(0x3651)) * cc;
  half yy = GetHalf(0x3b53) * aa + (1.0_h - GetHalf(0x3b53)) * dd;
  half zz = GetHalf(0x37fd) * aa + (1.0_h - GetHalf(0x37fd)) * cc;
  half ww = GetHalf(0x325d) * bb + (1.0_h - GetHalf(0x325d)) * dd;
  */
  half xx = aa;
  half yy = bb;
  half zz = cc;
  half ww = dd;

  s.a = 0.75_h * xx + 0.25_h;
  s.b = yy;
  s.c = zz;
  s.d = ww; // 0.25_h * ww + 0.75_h;

  return s;
}

int main(int argc, char **argv) {
  table1 = GradUtil::MakeTable1();
  table2 = GradUtil::MakeTable2();

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
