
#include "hcomplex.h"

#include <cstdio>

#include "image.h"
#include "threadutil.h"
#include "color-util.h"
#include "base/logging.h"

using uint32 = uint32_t;

using half = half_float::half;

static void Basic() {
  hcomplex z;
}

static void Mandelbrot() {
  static constexpr int SIZE = 2160;
  static constexpr int NUM_THREADS = 12;
  static constexpr int MAX_ITERS = 32;
  ImageRGBA img(SIZE, SIZE);

  static constexpr float XMIN = -2.3f, XMAX = 1.3f;
  static constexpr float YMIN = -1.8f, YMAX = 1.8f;
  static constexpr float WIDTH = XMAX - XMIN;
  static constexpr float HEIGHT = YMAX - YMIN;

  ParallelComp2D(
      SIZE, SIZE,
      [&img](int yp, int xp) {

        float x = (xp / (float)SIZE) * WIDTH + XMIN;
        float y = ((SIZE - yp) / (float)SIZE) * HEIGHT + YMIN;

        hcomplex z((half)0, (half)0);
        hcomplex c((half)x, (half)y);
        for (int i = 0; i < MAX_ITERS; i++) {
          if (z.Abs() > (half)2) {
            // Escaped. The number of iterations gives the pixel.
            float f = i / (float)MAX_ITERS;
            uint32 color = ColorUtil::LinearGradient32(
                ColorUtil::HEATED_METAL, f);
            img.SetPixel32(xp, yp, color);
            return;
          }

          z = z * z + c;
        }

        // Possibly in the set.
        img.SetPixel32(xp, yp, 0xFFFFFFFF);
      },
      NUM_THREADS);

  img.Save("hcomplex-mandelbrot.png");
}

int main(int argc, char **argv) {
  Mandelbrot();
  printf("OK\n");
  return 0;
}
