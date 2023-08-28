
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

DECLARE_COUNTERS(rows_done, img_rows_done, u00_, u1_, u2_, u3_, u4_, u5_);

// x is the a dimension, y is b.
// y increases downward, with origin at top left.
// b=0 is invalid and a=0 is degenerate.
static constexpr uint64_t startx = 1, starty = 1;
static constexpr int WIDTH = 8192;
static constexpr int HEIGHT = 8192;

static constexpr int XSTRIDE = 1;
// 42 seems to be the best factor to put the
// second prong on the diagonal...
// static constexpr int YSTRIDE = 42;

// this could use a little more tweaking but makes the
// angle roughly even between the two prongs.
// static constexpr int YSTRIDE = 243;
static constexpr int YSTRIDE = 42;

static constexpr ColorUtil::Gradient ERROR_GRADIENT{
  GradRGB(0.0f,  0x000000),
  GradRGB(0.05f, 0x007700),
  GradRGB(0.2f,  0x7700BB),
  GradRGB(0.5f,  0xFF0000),
  GradRGB(0.8f,  0xFFFF00),
  GradRGB(1.0f,  0xFFFFFF)
};

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
          /*
          BigInt gcd = BigInt::GCD(a, b);
          if (gcd != 1) {
            matrix[sy * WIDTH + sx] = BigInt{0};
            continue;
          }
          */

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

  // Color by magnitude.
  /*
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
  */

  // Color by rank.
  std::vector<BigInt> all_values = matrix;
  // Compute rank on absolute values.
  for (BigInt &b : all_values) b = BigInt::Abs(std::move(b));
  printf("Sort...\n");
  std::sort(all_values.begin(), all_values.end());
  printf("Unique...\n");
  all_values.erase(std::unique(all_values.begin(), all_values.end()),
                   all_values.end());
  printf("%lld/%lld unique elements.\n",
         (int64_t)all_values.size(), WIDTH * (int64_t)HEIGHT);
  auto GetRank = [&all_values](const BigInt &z) -> size_t {
      return std::lower_bound(all_values.begin(), all_values.end(), z) -
        all_values.begin();
    };

  auto Color = [&GetRank](const BigInt &z) -> uint32_t {
      return ColorUtil::LinearGradient32(
          ERROR_GRADIENT,
          GetRank(BigInt::Abs(z)) / (double)(WIDTH * HEIGHT));
    };

  printf("Write pixels...\n");
  Timer img_timer;
  ParallelComp(
      HEIGHT,
      [&](int64_t sy) {
        for (int sx = 0; sx < WIDTH; sx++) {
          uint32_t c = Color(matrix[sy * WIDTH + sx]);
          img.SetPixel32(sx, sy, c);
        }
        img_rows_done++;
        status_per.RunIf([&](){
            printf(ANSI_UP
                   "%s\n",
                   ANSI::ProgressBar(img_rows_done.Read(),
                                     HEIGHT,
                                     "img",
                                     img_timer.Seconds()).c_str());
          });
      },
      12);

  // Absolute legend
  int yy = 32;
  for (float f : {0.0, 0.125, 0.25, 0.375, 0.5, 0.75, 1.0}) {
    const BigInt &pctile = all_values[std::clamp(
          (size_t)std::round(f * all_values.size()),
          (size_t)0, all_values.size() - 1)];
    uint32_t color = Color(pctile);
    string num = LongNum(pctile);
    img.BlendText2x32(WIDTH - (num.size() * ImageRGBA::TEXT2X_WIDTH) - 40,
                      yy, 0x4444FFFF, num);
    int bx = WIDTH - 32;
    img.BlendBox32(bx, yy, ImageRGBA::TEXT2X_WIDTH, ImageRGBA::TEXT2X_HEIGHT,
                   0x000000FF, 0x00000077);
    img.BlendRect32(bx + 1, yy + 1,
                    ImageRGBA::TEXT2X_WIDTH - 2, ImageRGBA::TEXT2X_HEIGHT - 2,
                    color);
    yy += ImageRGBA::TEXT2X_HEIGHT + 4;
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
