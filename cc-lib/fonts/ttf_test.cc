
#include "ttf.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"

#define CHECK_NEAR(a, b) do {                   \
    double aa = (a), bb = (b);                  \
    double diff = std::abs(aa - bb);            \
    CHECK(diff < 1e-4) << "Comparing the "      \
      "values of " #a " and " #b ", got:\n" <<  \
      aa << " and " << bb << "\n" <<            \
      "which differ by " << diff << "\n";       \
  } while (false)

static void CreateAndDestroy() {
  TTF ttf("DFXPasement9px.ttf");
}

static void TestMetrics() {
  TTF ttf("DFXPasement9px.ttf");
  CHECK(ttf.FontInfo() != nullptr);

  float line_height = ttf.NormLineHeight();
  float baseline = ttf.Baseline();
  CHECK_NEAR(line_height, 1.1111);
  CHECK_NEAR(baseline, 0.7776);

  auto [w, h] = ttf.MeasureString("Hello, world!", 9);
  CHECK_NEAR(w, 83);
  CHECK_NEAR(h, 4551);

  auto [minx, miny, maxx, maxy] = ttf.BoundingBox();
  CHECK_NEAR(minx, 0);
  CHECK_NEAR(miny, 0);
  CHECK_NEAR(maxx, 1);
  CHECK_NEAR(maxy, 1);

  float kern_av = ttf.NormKernAdvance('A', 'V');
  CHECK_NEAR(kern_av, 0.7778);

  std::vector<TTF::Contour> contours = ttf.GetContours('A');
  CHECK(contours.size() == 2);
  CHECK(contours[0].paths.size() == 12);
  CHECK(contours[1].paths.size() == 4);
}

int main(int argc, char **argv) {
  ANSI::Init();

  CreateAndDestroy();
  TestMetrics();

  Print("OK\n");
  return 0;
}
