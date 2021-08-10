#include "color-util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <cstdint>

#include "base/logging.h"

#include "stb_image_write.h"
#include "randutil.h"
#include "arcfour.h"

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

int main () {
  ArcFour rc("color-util");

  TestLab();

  printf("OK\n");
  return 0;
}
