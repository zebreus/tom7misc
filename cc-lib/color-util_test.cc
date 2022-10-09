#include "color-util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <cstdint>

#include "base/logging.h"
#include "base/stringprintf.h"

#include "randutil.h"
#include "image.h"
#include "timer.h"

using namespace std;

using uint8 = uint8_t;
using uint32 = uint32_t;

#define CHECK_NEAR(a, b)                            \
  do {                                              \
  auto aa = (a);                                    \
  auto bb = (b);                                    \
  auto d = fabs(aa - bb);                           \
  CHECK(d < 0.0001f) << "Expected nearly equal: "   \
                      << "\n" #a << ": " << aa      \
                      << "\n" #b << ": " << bb;     \
  } while (false)

static void TestHSV() {
  ImageRGBA out(256, 256);
  for (int y = 0; y < 256; y++) {
    for (int x = 0; x < 256; x++) {
      float fx = x / 255.0;
      float fy = y / 255.0;
      const auto [r, g, b] = ColorUtil::HSVToRGB(fx, fy, 1.0f);
      const uint32_t color = ColorUtil::FloatsTo32(r, g, b, 1.0f);
      out.SetPixel32(x, y, color);
    }
  }
  out.Save("hsv.png");
}

static void TestLab() {
  {
    const auto [l, a, b] =
      ColorUtil::RGBToLAB(1.0, 1.0, 1.0);
    CHECK_NEAR(100.0f, l);
    CHECK_NEAR(0.0f, a);
    CHECK_NEAR(0.0f, b);
  }

  {
    const auto [l, a, b] =
      ColorUtil::RGBToLAB(0.0, 0.0, 0.0);
    CHECK_NEAR(0.0f, l);
    CHECK_NEAR(0.0f, a);
    CHECK_NEAR(0.0f, b);
  }

  // This is very close to what ColorMine produces, and close but not
  // quite what Photoshop does.
  {
    const auto [l, a, b] =
      ColorUtil::RGBToLAB(37.0 / 255.0,
                          140.0 / 255.0,
                          227.0 / 255.0);
    CHECK_NEAR(56.7746f, l);
    CHECK_NEAR(2.3497f, a);
    CHECK_NEAR(-52.0617f, b);
  }

  // TODO: Some other references...

}

static void TestGradient() {
  // Endpoints
  CHECK_EQ(0xFFFFFFFF,
           ColorUtil::LinearGradient32(ColorUtil::HEATED_METAL, 1.1f));
  CHECK_EQ(0x000000FF,
           ColorUtil::LinearGradient32(ColorUtil::HEATED_METAL, -0.1f));

  {
    // On ramp point.
    auto [r, g, b] =
      ColorUtil::LinearGradient(ColorUtil::HEATED_METAL, 0.2f);
    CHECK_NEAR(0x77 / 255.0f, r);
    CHECK_NEAR(0.0f, g);
    CHECK_NEAR(0xBB / 255.0f, b);
  }
}

static void TestConvert() {
  for (int x = 0; x < 256; x++) {
    uint8 r = x;
    uint8 g = x ^ 0x10;
    uint8 b = x + 33;
    uint8 a = ~x;

    uint32 rgba = ColorUtil::Pack32(r, g, b, a);
    const auto [rr, gg, bb, aa] = ColorUtil::Unpack32(rgba);
    CHECK(r == rr && g == gg && b == bb && a == aa);

    const auto [rf, gf, bf, af] = ColorUtil::U32ToFloats(rgba);
    const uint32_t rgba2 = ColorUtil::FloatsTo32(rf, gf, bf, af);

    CHECK(rgba == rgba2) <<
      StringPrintf("%08x vs %08x\n", rgba, rgba2);
  }
}

static void BenchLinearGradient() {
  ImageRGBA out(256, 256);
  out.Clear32(0x000000FF);

  Timer timer;
  static constexpr int ITERS = 2500;
  for (int iters = 0; iters < ITERS; iters++) {
    for (int y = 0; y < 256; y++) {
      float fy = cosf(y * (3.14159f / 32.0f)) * 0.5f + 1.0f;
      for (int x = 0; x < 256; x++) {
        float fx = sinf(x * (3.14159f / 16.0f)) * 0.5f + 1.0f;
        uint32_t c = ColorUtil::LinearGradient32(
            ColorUtil::HEATED_METAL, fx * fy);
        out.SetPixel32(x, y, c);
      }
    }
  }
  double sec = timer.Seconds();
  printf("%d passes in %.3fs = %.3f p/s\n",
         ITERS, sec, ITERS / sec);

  out.Save("bench-lineargradient.png");
}

int main () {
  BenchLinearGradient();

  TestHSV();
  TestLab();
  TestGradient();

  TestConvert();

  printf("OK\n");
  return 0;
}
