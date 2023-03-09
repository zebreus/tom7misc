

#include <string>
#include <vector>
#include <optional>
#include <cstdint>

#include "util.h"
#include "image.h"
#include "ansi.h"
#include "half.h"
#include "color-util.h"

#include "grad-util.h"

// Show the paths of two numbers as we iterate "function1" on them.
static void Race() {
  static constexpr uint16 SCALE_U = 0x3bffu;
  static constexpr half SCALE = GradUtil::GetHalf(SCALE_U);

  // half a = GradUtil::GetHalf(0x37ff);
  // half b = GradUtil::GetHalf(0x3800); // 0.5
  // half c = GradUtil::GetHalf(0x3801);

  half a = (half)0.5f;
  half b = (half)1.0f;
  half c = (half)1.5f;


  const double sd = SCALE;
  double ad = a;
  double bd = b;
  double cd = c;

  for (int i = 0; i < 500; i++) {
    a *= SCALE;
    b *= SCALE;
    c *= SCALE;

    ad *= sd;
    bd *= sd;
    cd *= sd;

    double ea = a - ad;
    double eb = b - bd;
    double ec = c - cd;

    printf("%.9g %.9g %.9g  %.11g %.11g %.11g\n",
           (double)a, (double)b, (double)c,
           ea, eb, ec);
  }

}

static inline NextAfter16(uint16_t pos) {
  // Zero comes immediately after -0.
  if (pos == 0x8000) return 0x0000;
  else if (pos > 0x8000) return pos - 1;
  else return pos + 1;
}

static inline NextAfterHalf(half h) {
  return GradUtil::GetHalf(NextAfter16(GradUtil::GetU16(h)));
}

// Find a constant where f(x) = x.
static void Self() {
  static constexpr half SCALE = GradUtil::GetHalf(0x3bffu);
  for (uint16_t u = 2; // GradUtil::GetU16((half)1.0f);
       u < 0x7c00; u++) {
    half h = GradUtil::GetHalf(u);
    half hf = h * SCALE;

    if (h == hf) {
      printf("Fixed point at %04x (%.9g)\n",
             u, (double)h);
      // return;
    } else if (u < 0x400) {
      printf("NOT AT %04x\n", u);
    }
  }
  printf("No fixed point\n");
}

// Show all the paths, by coloring individual pixels.
static void Rainbow() {
  static constexpr ColorUtil::Gradient SIGN{
    GradRGB(-1.00f, 0xFF7777),
    GradRGB(-0.50f, 0xFF0000),
    GradRGB(-0.01f, 0x550000),
    GradRGB(+0.01f, 0x005500),
    GradRGB(+0.50f, 0x00FF00),
    GradRGB(+1.00f, 0x77FF77),
  };

  static constexpr half SCALE = GradUtil::GetHalf(0x3bffu);
  static constexpr double DSCALE = 1.0 - (1.0 / 2048.0);

  static constexpr int PX = 4;
  static constexpr int WIDTH = PX * 3840;
  static constexpr int HEIGHT = PX * 2160;
  ImageRGBA img(WIDTH, HEIGHT);
  img.Clear32(0x000000FF);
  ImageRGBA eimg(WIDTH, HEIGHT);
  eimg.Clear32(0xFFFFFFFF);

  // Sample a value in the [0, 1] interval.
  std::vector<half> values(WIDTH);
  std::vector<double> dvalues(WIDTH);
  for (int i = 0; i < WIDTH; i++) {
    double d = (i / (double)WIDTH);
    dvalues[i] = d;
    half h = (half)d;
    values[i] = h;
  }

  for (int y = 0; y < HEIGHT; y++) {
    // draw the pixels
    for (int i = 0; i < WIDTH; i++) {
      float frac = i / (float)WIDTH;
      const auto [r, g, b] = ColorUtil::HSVToRGB(frac, 1.0f, 1.0f);
      uint32_t color = ColorUtil::FloatsTo32(r, g, b, 0.25f);
      half h = values[i];
      int x = (int)((double)h * (double)WIDTH);
      img.BlendPixel32(x, y, color);

      {
        double d = dvalues[i];
        double err = (double)h - d;

        uint32_t color = ColorUtil::LinearGradient32(SIGN, err);
        eimg.BlendPixel32(x, y, color & 0xFFFFFFAA);
      }
    }

    for (int i = 0; i < WIDTH; i++) {
      values[i] *= SCALE;
      dvalues[i] *= DSCALE;
    }
  }

  img.ScaleDownBy(PX).Save("rainbow.png");
  eimg.ScaleDownBy(PX).Save("rainbow-error.png");
}

int main(int argc, char **argv) {
  // Self();
  // Race();
  Rainbow();

  return 0;
}
