
#include "sos-util.h"

#include <vector>
#include <string>
#include <cstdint>

#include "atomic-util.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "image.h"
#include "bounds.h"
#include "threadutil.h"
#include "periodically.h"
#include "ansi.h"
#include "timer.h"
#include "bignum/big.h"
#include "bignum/big-overloads.h"
#include "bhaskara-util.h"
#include "util.h"
#include "color-util.h"

using namespace std;

DECLARE_COUNTERS(rows_done, u0_, u00_, u1_, u2_, u3_, u4_, u5_);

// x is the a dimension, y is b.
// y increases downward, with origin at top left.
// b=0 is invalid and a=0 is degenerate.
static constexpr uint64_t startx = 1, starty = 1;
static constexpr int WIDTH = 2048;
static constexpr int HEIGHT = 2048;

static constexpr int XSTRIDE = 256;
static constexpr int YSTRIDE = 256;


static void MakeGrid() {
  Periodically status_per(1.0);
  Timer run_timer;

  rows_done.Reset();

  // best cell in each row.
  std::vector<int64_t> bestx(HEIGHT);

  //  ImageRGBA img(WIDTH, HEIGHT);
  std::vector<BigInt> matrix(WIDTH * HEIGHT);
  ParallelComp(
      HEIGHT,
      [&](int64_t sy) {
        BigInt b(starty + BigInt(sy) * YSTRIDE);
        std::optional<BigInt> best;
        int64_t bestsx = 0;
        for (int64_t sx = 0; sx < WIDTH; sx++) {
          BigInt a(startx + BigInt(sx) * XSTRIDE);

          BigInt aa = a * a;
          BigInt bb = b * b;
          BigInt aaaa = aa * aa;
          BigInt bbbb = bb * bb;

          BigInt k = 1165684 * aa * bb +
            -2030090816 * aaaa -
            bbbb;

          if (!best.has_value() ||
              BigInt::Abs(k) < BigInt::Abs(best.value())) {
            bestsx = sx;
            best = {k};
          }

          matrix[sy * WIDTH + sx] = std::move(k);
        }

        bestx[sy] = bestsx;

        rows_done++;

        status_per.RunIf([&](){
            printf(ANSI_UP
                   "%s\n",
                   ANSI::ProgressBar(rows_done.Read(),
                                     HEIGHT,
                                     "compute k",
                                     run_timer.Seconds()).c_str());
          });
      },
      12);

  // Get overall best.
  BigInt besta(startx + 0 * XSTRIDE);
  BigInt bestb(starty + 0 * YSTRIDE);
  BigInt bestk = matrix[0];
  for (int sy = 0; sy < HEIGHT; sy++) {
    int64_t sx = bestx[sy];
    const BigInt &k = BigInt::Abs(matrix[sy * WIDTH + sx]);
    if (k < BigInt::Abs(bestk)) {
      bestk = k;
      besta = startx + BigInt(sx) * XSTRIDE;
      bestb = starty + BigInt(sy) * YSTRIDE;
    }
  }

  BigInt maxk = matrix[0];
  for (const BigInt &k : matrix) {
    if (BigInt::Abs(k) > maxk) maxk = BigInt::Abs(k);
  }

  printf("\n\n\n\n");
  printf("Overall best at a=%s, b=%s. k: %s\n",
         besta.ToString().c_str(),
         bestb.ToString().c_str(),
         bestk.ToString().c_str());
  printf("|maxk|: %s\n",
         maxk.ToString().c_str());

  if (WIDTH * HEIGHT < 512) {
    for (int y = 0; y < HEIGHT; y++) {
      for (int x = 0; x < WIDTH; x++) {
        const BigInt &k = matrix[y * WIDTH + x];
        printf("%s ", LongNum(k).c_str());
      }
      printf("\n");
    }
  }

  printf("Make image...\n");
  ImageRGBA img(WIDTH, HEIGHT);
  img.Clear32(0x000000FF);

  auto Color = [&maxk](const BigInt &z) -> uint32_t {
      BigRat q(z, maxk);
      double d = q.ToDouble();
      if (!std::isfinite(d)) {
        return 0xFFFFFFFF;
      } else if (d < -1.0) {
        return 0xFFFF00FF;
      } else if (d > 1.0) {
        return 0xFF00FFFF;
      } else {
        return ColorUtil::LinearGradient32(ColorUtil::NEG_POS, d);
      }
    };

  Timer img_timer;

  for (int sy = 0; sy < HEIGHT; sy++) {
    for (int sx = 0; sx < WIDTH; sx++) {
      uint32_t c = Color(matrix[sy * WIDTH + sx]);
      img.SetPixel32(sx, sy, c);
    }
    status_per.RunIf([&](){
        printf(ANSI_UP
               "%s\n",
               ANSI::ProgressBar(sy,
                                 HEIGHT,
                                 "img",
                                 img_timer.Seconds()).c_str());
      });
  }

  string filename = "kgrid.png";
  img.Save(filename);
  printf("Wrote %s\n", filename.c_str());
}


int main(int argc, char **argv) {
  ANSI::Init();

  MakeGrid();

  return 0;
}
