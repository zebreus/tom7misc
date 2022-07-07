#include "color-util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <cstdint>

#include "base/logging.h"

#include "randutil.h"
#include "image.h"

using namespace std;

#define CHECK_NEAR(a, b)                            \
  do {                                              \
  auto aa = (a);                                    \
  auto bb = (b);                                    \
  auto d = fabs(aa - bb);                           \
  CHECK(d < 0.0001f) << "Expected nearly equal: "   \
                      << "\n" #a << ": " << aa      \
                      << "\n" #b << ": " << bb;     \
  } while (false)

// This utility comes up a lot and should perhaps be part of
// color util!
static uint32 MixRGB(float r, float g, float b, float a) {
  uint32 rr = std::clamp((int)(r * 255.0f), 0, 255);
  uint32 gg = std::clamp((int)(g * 255.0f), 0, 255);
  uint32 bb = std::clamp((int)(b * 255.0f), 0, 255);
  uint32 aa = std::clamp((int)(a * 255.0f), 0, 255);
  return (rr << 24) | (gg << 16) | (bb << 8) | aa;
}

static void TestHSV() {
  ImageRGBA out(256, 256);
  for (int y = 0; y < 256; y++) {
    for (int x = 0; x < 256; x++) {
      float fx = x / 255.0;
      float fy = y / 255.0;
      const auto [r, g, b] = ColorUtil::HSVToRGB(fx, fy, 1.0f);
      const uint32_t color = MixRGB(r, g, b, 1.0f);
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

int main () {
  TestHSV();
  TestLab();
  TestGradient();

  printf("OK\n");
  return 0;
}
