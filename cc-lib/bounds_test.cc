#include "bounds.h"

#include <utility>
#include <initializer_list>

#include "base/stringprintf.h"
#include "base/logging.h"
#include "arcfour.h"
#include "randutil.h"

using namespace std;

// Note: Evaluates a and b a second time if check fails!
#define CHECK_FEQ(a, b) CHECK(fabs((a) - (b)) < 0.00001) \
  << #a " = " << (a) << " vs " #b " = " << (b)

#define CHECK_PEQ(a, x, y) do { \
  const auto [xx, yy] = (a); \
  CHECK_FEQ(xx, (x)); \
  CHECK_FEQ(yy, (y)); \
  } while (false)

static void TestSimple() {
  Bounds bounds;

  CHECK(bounds.Empty());

  bounds.Bound(1.0, 3.0);
  CHECK_FEQ(bounds.MinX(), 1.0);
  CHECK_FEQ(bounds.MaxX(), 1.0);
  CHECK_FEQ(bounds.MinY(), 3.0);
  CHECK_FEQ(bounds.MaxY(), 3.0);
  CHECK_FEQ(bounds.Width(), 0.0);
  CHECK_FEQ(bounds.Height(), 0.0);
  CHECK(!bounds.Empty());

  bounds.Bound(-2.0, -5.0);
  CHECK_FEQ(bounds.MinX(), -2.0);
  CHECK_FEQ(bounds.MaxX(), 1.0);
  CHECK_FEQ(bounds.MinY(), -5.0);
  CHECK_FEQ(bounds.MaxY(), 3.0);
  CHECK_FEQ(bounds.Width(), 3.0);
  CHECK_FEQ(bounds.Height(), 8.0);
  CHECK(!bounds.Empty());

  Bounds bounds2;
  bounds2.Bound(2.0, -1.0);
  bounds2.Bound(-1.0, 2.0);
  CHECK(!bounds2.Empty());
  CHECK_FEQ(bounds2.Width(), 3.0);
  CHECK_FEQ(bounds2.Height(), 3.0);

  bounds.Union(bounds2);
  CHECK_FEQ(bounds.Width(), 4.0);
  CHECK_FEQ(bounds.Height(), 8.0);
  CHECK_FEQ(bounds.MaxX(), 2.0);
  CHECK_FEQ(bounds.MinX(), -2.0);
  CHECK_FEQ(bounds.MaxY(), 3.0);
  CHECK_FEQ(bounds.MinY(), -5.0);

  bounds.AddMargin(0.5);
  CHECK_FEQ(bounds.Width(), 5.0);
  CHECK_FEQ(bounds.Height(), 9.0);
  CHECK_FEQ(bounds.MaxX(), 2.5);
  CHECK_FEQ(bounds.MinX(), -2.5);
  CHECK_FEQ(bounds.MaxY(), 3.5);
  CHECK_FEQ(bounds.MinY(), -5.5);
}

static void TestScaler() {
  {
    // Data occupy the same bounds as the screen.
    Bounds bounds;
    bounds.Bound(800.0, 600.0);
    bounds.Bound(0.0, 0.0);
    Bounds::Scaler scaler = bounds.Stretch(800.0, 600.0);

    for (const auto &[x, y] :
           std::initializer_list<std::pair<double, double>>
           {{3.0, 4.0}, {-1.0, -1.0}, {0.0, 0.0}, {800.0, 600.0}}) {
      auto [xx, yy] = scaler.Scale({x, y});
      CHECK_FEQ(xx, x);
      CHECK_FEQ(yy, y);

      auto [xxx, yyy] = scaler.Unscale({xx, yy});
      CHECK_FEQ(xxx, x);
      CHECK_FEQ(yyy, y);
    }
  }

  {
    // As above, but flipped.
    Bounds bounds;
    bounds.Bound(800.0, 600.0);
    bounds.Bound(0.0, 0.0);
    Bounds::Scaler scaler =
      bounds.Stretch(800.0, 600.0).FlipY();

    for (const auto &[x, y] :
           std::initializer_list<std::pair<double, double>>
           {{3.0, 4.0}, {-1.0, -1.0}, {0.0, 0.0}, {800.0, 600.0}}) {
      auto [xx, yy] = scaler.Scale({x, y});

      // Y flip does not affect x coordinate.
      CHECK_FEQ(xx, x);
      // ... but y coordinate is flipped
      CHECK_FEQ(yy, 600.0 - y);

      auto [xxx, yyy] = scaler.Unscale({xx, yy});
      CHECK_FEQ(xxx, x);
      CHECK_FEQ(yyy, y);
    }
  }

  {
    // Same, but with input x data squashed by a factor of 10, so
    // that x scale needs to be 10.
    Bounds bounds;
    bounds.Bound(80.0, 600.0);
    bounds.Bound(0.0, 0.0);
    Bounds::Scaler scaler = bounds.Stretch(800.0, 600.0);

    for (const auto &[x, y] :
           std::initializer_list<std::pair<double, double>>
           {{0.3, 4.0}, {-0.1, -1.0}, {0.0, 0.0}, {80.0, 600.0}}) {
      auto [xx, yy] = scaler.Scale({x, y});

      CHECK_FEQ(xx, x * 10.0);
      CHECK_FEQ(yy, y);

      auto [xxx, yyy] = scaler.Unscale({xx, yy});
      CHECK_FEQ(xxx, x);
      CHECK_FEQ(yyy, y);
    }
  }

  {
    // Y data stretched 10x.
    Bounds bounds;
    bounds.Bound(800.0, 6000.0);
    bounds.Bound(0.0, 0.0);
    Bounds::Scaler scaler = bounds.Stretch(800.0, 600.0);

    for (const auto &[x, y] :
           std::initializer_list<std::pair<double, double>>
           {{3.0, 40.0}, {-1.0, -10.0}, {0.0, 0.0}, {800.0, 6000.0}}) {
      auto [xx, yy] = scaler.Scale({x, y});
      CHECK_FEQ(xx, x);
      CHECK_FEQ(yy, y * 0.1);

      auto [xxx, yyy] = scaler.Unscale({xx, yy});
      CHECK_FEQ(xxx, x);
      CHECK_FEQ(yyy, y);
    }

    // ... and flipped
    Bounds::Scaler flipped = scaler.FlipY();
    for (const auto &[x, y] :
           std::initializer_list<std::pair<double, double>>
           {{3.0, 40.0}, {-1.0, -10.0}, {0.0, 0.0}, {800.0, 6000.0}}) {
      auto [xx, yy] = flipped.Scale({x, y});
      CHECK_FEQ(xx, x);
      CHECK_FEQ(yy, 600.0 - y * 0.1);

      auto [xxx, yyy] = flipped.Unscale({xx, yy});
      CHECK_FEQ(xxx, x);
      CHECK_FEQ(yyy, y);
    }
  }

  {
    // With offset. This is a rectangle of size 8x6,
    // but whose bottom-left point is at 2,1.
    Bounds bounds;
    bounds.Bound(10.0, 5.0);
    bounds.Bound(5.0, 7.0);
    bounds.Bound(2.0, 1.0);
    bounds.Bound(6.0, 2.0);
    CHECK_FEQ(bounds.Width(), 8.0);
    CHECK_FEQ(bounds.Height(), 6.0);
    CHECK_FEQ(bounds.MinX(), 2.0);
    CHECK_FEQ(bounds.MinY(), 1.0);

    Bounds::Scaler scaler = bounds.Stretch(800.0, 600.0);
    /*
    printf("Scaler off [%.3f, %.3f] scale [%.3f, %.3f] size [%.3f, %.3f]\n",
           scaler.xoff, scaler.yoff,
           scaler.xs, scaler.ys,
           scaler.width, scaler.height);
    */
    CHECK_PEQ(scaler.Scale({2.0, 1.0}), 0.0, 0.0);
    CHECK_PEQ(scaler.Scale({5.0, 7.0}), 300.0, 600.0);
    CHECK_PEQ(scaler.Scale({6.0, 2.0}), 400.0, 100.0);

    Bounds::Scaler flipped = scaler.FlipY();
    /*
    CHECK_FEQ(flipped.yoff, -7.0);
    CHECK_FEQ(flipped.ys, -100.0);
    printf("Flipped off [%.3f, %.3f] scale [%.3f, %.3f] size [%.3f, %.3f]\n",
           flipped.xoff, flipped.yoff,
           flipped.xs, flipped.ys,
           flipped.width, flipped.height);
    */
    CHECK_PEQ(flipped.Scale({2.0, 1.0}), 0.0, 600.0);
    CHECK_PEQ(flipped.Scale({5.0, 7.0}), 300.0, 0.0);
    CHECK_PEQ(flipped.Scale({6.0, 2.0}), 400.0, 500.0);
    CHECK_PEQ(flipped.Scale({10.0, 5.0}), 800.0, 200.0);
  }

}

int main(int argc, char **argv) {
  TestSimple();
  TestScaler();

  printf("OK\n");
  return 0;
}
