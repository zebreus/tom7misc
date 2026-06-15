#include <cstdio>
#include <cmath>

#include "ansi.h"
#include "base/logging.h"
#include "geom/bezier.h"

using namespace std;

#define CHECK_FEQ(a, b) CHECK(fabs((a) - (b)) < 0.00001)

static void DistanceToQuad() {
  const auto [x, y, d] =
    DistanceFromPointToQuadBezier(
        // The point to test
        1.0f, 2.0f,
        // Bezier start point
        2.0f, -1.0f,
        // Bezier control point
        -1.0f, 4.0f,
        // Bezier end point
        4.0f, 5.0f);

  CHECK_FEQ(0.88100123f, x);
  CHECK_FEQ(1.99277639f, y);
  CHECK_FEQ(0.01421289f, d);

  // TODO: Test endpoints, etc.
}

static void TesselateQuad() {
  // Collinear points should result in a single line segment.
  auto line = TesselateQuadBezier<float>(0.0f, 0.0f, 1.0f, 1.0f, 2.0f, 2.0f);
  CHECK(line.size() == 1);
  CHECK_FEQ(2.0f, line[0].first);
  CHECK_FEQ(2.0f, line[0].second);

  // A noticeable curve will be subdivided.
  auto curve = TesselateQuadBezier<float>(0.0f, 0.0f, 0.0f, 10.0f, 10.0f, 0.0f);
  CHECK(curve.size() > 1);
  // The last point is always the destination.
  CHECK_FEQ(10.0f, curve.back().first);
  CHECK_FEQ(0.0f, curve.back().second);
}

static void TesselateCubic() {
  // Collinear points should result in a single line segment.
  auto line = TesselateCubicBezier<float>(
      0.0f, 0.0f, 1.0f, 1.0f, 2.0f, 2.0f, 3.0f, 3.0f);
  CHECK(line.size() == 1);
  CHECK_FEQ(3.0f, line[0].first);
  CHECK_FEQ(3.0f, line[0].second);

  // A noticeable curve will be subdivided.
  auto curve = TesselateCubicBezier<float>(
      0.0f, 0.0f, 0.0f, 10.0f, 10.0f, 10.0f, 10.0f, 0.0f);
  CHECK(curve.size() > 1);
  // The last point is always the destination.
  CHECK_FEQ(10.0f, curve.back().first);
  CHECK_FEQ(0.0f, curve.back().second);
}

int main(int argc, char **argv) {
  ANSI::Init();

  DistanceToQuad();
  TesselateQuad();
  TesselateCubic();

  printf("OK\n");
  return 0;
}

