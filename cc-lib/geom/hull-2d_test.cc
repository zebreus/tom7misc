
#include "geom/hull-2d.h"

#include <cstdlib>
#include <format>
#include <numbers>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "ansi.h"
#include "arcfour.h"
#include "base/logging.h"
#include "base/print.h"
#include "bounds.h"
#include "dirty.h"
#include "image.h"
#include "randutil.h"
#include "yocto-math.h"
// XXX these methods should probably be in like polygon.h?
#include "geom/polyhedra.h"

using vec2 = yocto::vec<double, 2>;

static constexpr bool VERBOSE = false;

double IsNear(double a, double b) {
  return std::abs(a - b) < 0.0000001;
}

#define CHECK_NEAR(f, g) do {                                           \
  const double fv = (f);                                                \
  const double gv = (g);                                                \
  const double e = std::abs(fv - gv);                                   \
  CHECK(e < 0.0000001) << "Expected " << #f << " and " << #g <<         \
    " to be close, but got: " <<                                        \
    std::format("{:.17g} and {:.17g}, with err {:.17g}", fv, gv, e);   \
  } while (0)

// This is copied from Ruperts but probably should not be here;
// it's only used for debugging.
static std::vector<uint32_t> COLORS = {
  0xFF0000FF,
  0xFFFF00FF,
  0x00FF00FF,
  0x00FFFFFF,
  0x0000FFFF,
  0xFF00FFFF,
  0x770000FF,
  0x777700FF,
  0x007700FF,
  0x007777FF,
  0x000088FF, // distinguish more from 55 below
  0x770077FF,
  0xFF7777FF,
  0xFFFF77FF,
  0x77FF77FF,
  0x77FFFFFF,
  0x7777FFFF,
  0xFF77FFFF,
  0x330000FF,
  0x333300FF,
  0x003300FF,
  0x003333FF,
  0x000055FF, // 33 is a little too dark
  0x330033FF,
  0x773333FF,
  0x777733FF,
  0x337733FF,
  0x337777FF,
  0x333377FF,
  0x773377FF,
  0xFFAAAAFF,
  0xFFFFAAFF,
  0xAAFFAAFF,
  0xAAFFFFFF,
  0xAAAAFFFF,
  0xFFAAFFFF,
  0xFF3333FF,
  0xFFFF33FF,
  0x33FF33FF,
  0x33FFFFFF,
  0x3333FFFF,
  0xFF33FFFF,
  0x773333FF,
  0x77FF33FF,
  0x3377FFFF,
  0x7733FFFF,
  0xFF7733FF,
  0x33FF77FF,
  0x3333FFFF,
  0xFF3377FF,
  0x77AAAAFF,
  0x77FFAAFF,
  0xAA77FFFF,
  0x77AAFFFF,
  0xFF77AAFF,
  0xAAFF77FF,
  0xAAAAFFFF,
  0xFFAA77FF,
  0x33AAAAFF,
  0x33FFAAFF,
  0xAA33FFFF,
  0x33AAFFFF,
  0xFF33AAFF,
  0xAAFF33FF,
  0xAAAAFFFF,
  0xFFAA33FF,
};

static uint32_t Color(int i) {
  return COLORS[i % COLORS.size()];
}

template<typename T>
static inline int sgn(T val) {
  return (T(0) < val) - (val < T(0));
}

static void DrawPoints(std::span<const vec2> pts,
                       // Can be empty
                       std::span<const int> hull,
                       std::string_view filename) {
  constexpr int WIDTH = 1920;
  constexpr int HEIGHT = 1080;
  ImageRGBA img(WIDTH, HEIGHT);
  Dirty dirty(WIDTH, HEIGHT);
  img.Clear32(0x000000FF);

  Bounds bounds;
  for (const vec2 &pt : pts) {
    bounds.Bound(pt.x, pt.y);
  }
  bounds.AddMarginFrac(0.05);
  Print("Bounds: {:.11f},{:.11f} to {:.11f},{:.11f}\n",
        bounds.MinX(), bounds.MinY(),
        bounds.MaxX(), bounds.MaxY());

  Bounds::Scaler scaler = bounds.ScaleToFit(WIDTH, HEIGHT, true);

  std::unordered_set<int> in_hull;
  if (!hull.empty()) {
    for (int i = 0; i < hull.size(); i++) {
      in_hull.insert(hull[i]);
      const int v0 = hull[i];
      const int v1 = hull[(i + 1) % hull.size()];
      const auto &[x0, y0] = scaler.Scale(pts[v0].x, pts[v0].y);
      const auto &[x1, y1] = scaler.Scale(pts[v1].x, pts[v1].y);
      img.BlendThickLine32(x0, y0, x1, y1, 3, 0x00FFFF44);
    }
  }

  for (int i = 0; i < pts.size(); i++) {
    const auto &[sx, sy] = scaler.Scale(pts[i].x, pts[i].y);
    img.BlendFilledCircleAA32(sx, sy, 5.0, 0xFFFFFF66);
    dirty.MarkUsed(sx - 12, sy - 12, 24, 24);
  }


  // Now label them.
  for (int i = 0; i < pts.size(); i++) {
    const auto &[sx, sy] = scaler.Scale(pts[i].x, pts[i].y);
    std::string label =
      in_hull.contains(i) ? std::format("{}+", i) :
      std::format("{}", i);
    const int w = label.size() * ImageRGBA::TEXT2X_WIDTH;
    const int h = ImageRGBA::TEXT2X_HEIGHT;
    const auto &[x, y] =
      dirty.PlaceNearby(sx - (w >> 1) - 1, sy - (h >> 1) - 1, w + 1, h + 1,
                        60);
    img.BlendText2x32(x, y, Color(i), label);
    dirty.MarkUsed(x, y, w, h);
  }

  // And label the hull points in order.
  for (int i = 0; i < hull.size(); i++) {
    int v = hull[i];
    const auto &[sx, sy] = scaler.Scale(pts[v].x, pts[v].y);
    std::string label = std::format("={}", i);
    const int w = label.size() * ImageRGBA::TEXT2X_WIDTH;
    const int h = ImageRGBA::TEXT2X_HEIGHT;
    const auto &[x, y] =
      dirty.PlaceNearby(sx - (w >> 1) - 1, sy - (h >> 1) - 1, w + 1, h + 1,
                        60);
    img.BlendText2x32(x, y, 0xFFFFFFFF, label);
    dirty.MarkUsed(x, y, w, h);
  }

  img.Save(filename);
  dirty.used.MonoRGBA(0xFFFFFFFF).Save("dirty.png");
  Print("Wrote " AGREEN("{}") "\n", filename);
}


static void CheckHullProperties(int line_num,
                                const std::vector<vec2> &v,
                                const std::vector<int> &hull,
                                const char *what) {
  std::unordered_set<int> inhull;
  for (int a : hull) inhull.insert(a);

  auto FailWithImage = [line_num, what, &v, &hull](const char *err) {
      DrawPoints(v, hull, std::format("{}-test-fail.png",
                                      what));
      LOG(FATAL) << "\nFrom line " << line_num << "<" << what << ">"
                 << ": Bad hull (" << err << ")";
    };

  if (!Hull2D::IsHullConvex(v, hull)) {
    FailWithImage("not convex");
  }

  for (int i = 0; i < v.size(); i++) {
    if (!inhull.contains(i)) {
      vec2 pt = v[i];

      if (!PointInPolygon(pt, v, hull) &&
          DistanceToHull(v, hull, pt) > 0.0000001) {
        Print("Erroneous point #{} at {:.17g},{:.17g}\n",
              i, pt.x, pt.y);
        Print("Points:\n");
        for (int j = 0; j < v.size(); j++) {
          Print("  vec2{{" "{:.17g},{:.17g}" "}}\n", v[j].x, v[j].y);
        }

        Print("Hull:\n");
        for (int a : hull) {
          vec2 pp = v[a];
          Print("  #{}, {:.17g},{:.17g}\n", a, pp.x, pp.y);
        }
        FailWithImage("doesn't contain all points");
      }
    }
  }
}

template <class F> static void TestHullRegression2(
    const char *what, F ComputeHull) {
  std::vector<vec2> pts{
      vec2{1, -4.2360679774997898},
      vec2{-1.6180339887498953, -3.2360679774997902},
      vec2{-3.2360679774997898, -2.6180339887498949},
      vec2{-1, -4.2360679774997898},
      vec2{-1, -4.2360679774997898},
      vec2{-2.6180339887498949, -3.6180339887498949},
      vec2{-3.2360679774997898, 2.6180339887498949},
      vec2{1.0000000000000004, 1.0000000000000004},
      vec2{3.6180339887498949, -3.3306690738754696e-16},
      vec2{-4.2360679774997898, 1},
      vec2{1.0000000000000002, -0.99999999999999956},
      vec2{2.6180339887498949, -1.6180339887498953},
      vec2{1.6180339887498949, -3.2360679774997898},
      vec2{-4.4408920985006262e-16, -2.6180339887498953},
      vec2{-4.2360679774997898, -1},
      vec2{0, -2.6180339887498945},
      vec2{1.6180339887498947, -3.2360679774997902},
      vec2{-4.2360679774997898, -1},
      vec2{-2.6180339887498949, 3.6180339887498949},
      vec2{2.6180339887498953, 1.6180339887498953},
      vec2{2.6180339887498949, 1.6180339887498945},
      vec2{-3.2360679774997898, 2.6180339887498953},
      vec2{3.6180339887498949, 4.4408920985006262e-16},
      vec2{0.99999999999999978, 0.99999999999999967},
      vec2{2.6180339887498949, -3.6180339887498949},
      vec2{-2.6180339887498953, -1.6180339887498953},
      vec2{-2.6180339887498949, -1.6180339887498945},
      vec2{3.2360679774997898, -2.6180339887498953},
      vec2{-3.6180339887498949, -4.4408920985006262e-16},
      vec2{-0.99999999999999978, -0.99999999999999967},
      vec2{-1.6180339887498949, 3.2360679774997898},
      vec2{4.4408920985006262e-16, 2.6180339887498953},
      vec2{4.2360679774997898, 1},
      vec2{0, 2.6180339887498945},
      vec2{-1.6180339887498947, 3.2360679774997902},
      vec2{4.2360679774997898, 1},
      vec2{3.2360679774997898, -2.6180339887498949},
      vec2{-1.0000000000000004, -1.0000000000000004},
      vec2{-3.6180339887498949, 3.3306690738754696e-16},
      vec2{4.2360679774997898, -1},
      vec2{-1.0000000000000002, 0.99999999999999956},
      vec2{-2.6180339887498949, 1.6180339887498953},
      vec2{-1, 4.2360679774997898},
      vec2{1.6180339887498953, 3.2360679774997902},
      vec2{3.2360679774997898, 2.6180339887498949},
      vec2{1, 4.2360679774997898},
      vec2{1, 4.2360679774997898},
      vec2{2.6180339887498949, 3.6180339887498949},
      vec2{-1.6180339887498949, -3.2360679774997898},
      vec2{0.99999999999999978, -4.2360679774997898},
      vec2{-3.2360679774997898, -2.6180339887498953},
      vec2{-4.2360679774997898, 1.0000000000000002},
      vec2{2.6180339887498949, -1.6180339887498947},
      vec2{0.99999999999999978, -1.0000000000000004},
      vec2{4.2360679774997898, -1.0000000000000002},
      vec2{-2.6180339887498949, 1.6180339887498947},
      vec2{-0.99999999999999978, 1.0000000000000004},
      vec2{1.6180339887498949, 3.2360679774997898},
      vec2{-0.99999999999999978, 4.2360679774997898},
      vec2{3.2360679774997898, 2.6180339887498953},
  };

  // DrawPoints(pts, {}, "regression2.png");
  auto hull = ComputeHull(pts);
  DrawPoints(pts, hull, std::format("regression-{}-2.png", what));

  CHECK(hull.size() >= 3) << hull.size();
}

template<class F>
static void TestHullRegression1(const char *what, F ComputeHull) {

  std::vector<vec2> pts{
    // This point is not necessary to exhibit the bug, but
    // the hull is degenerate otherwise.
    vec2{0.0, 0.0},
    // Three colinear points.
    vec2{-3.5005117466674776, -2.4075427785279846},
    vec2{-2.4320380042894287, -3.7314729325952465},
    vec2{-4.1608648355439106, -1.589308944583532},
  };

  (void)ComputeHull(pts);
}

template<class F>
static void TestHullRegression3(const char *what, F ComputeHull) {

  // Three colinear points.
  std::vector<vec2> pts{
    vec2{-12,-1},
    vec2{-13,-1},
    vec2{-10,-1},
  };

  std::vector<int> hull = ComputeHull(pts);
  CheckHullProperties(__LINE__, pts, hull, what);
}

template<class F>
static void TestHull(const char *what, F ComputeHull) {
  {
    std::vector<vec2> square = {
      vec2(1.0, 1.0),
      vec2(-1.0, 1.0),
      vec2(-1.0, -1.0),
      vec2(1.0, -1.0),
    };

    std::vector<int> hull = ComputeHull(square);
    CHECK(hull.size() == 4);
    CheckHullProperties(__LINE__, square, hull, what);
  }

  {
    std::vector<vec2> degenerate_triangle = {
      vec2(-5.3333333333333333333333333,-2.666666666666666666666666666),
      vec2(-5.3333333333333333333333333,-2.666666666666666666666666666),
      vec2(-3.6666666666666666666666666,-5.333333333333333333333333333),
    };
    std::vector<int> hull = ComputeHull(degenerate_triangle);
    CHECK(hull.size() == 2);
    CheckHullProperties(__LINE__, degenerate_triangle, hull, what);
  }

  {

    constexpr double u = 1.0 + std::numbers::sqrt2;
    std::vector<vec3> cubo;

    for (int b = 0b000; b < 0b1000; b++) {
      double s1 = (b & 0b100) ? -1 : +1;
      double s2 = (b & 0b010) ? -1 : +1;
      double s3 = (b & 0b001) ? -1 : +1;

      cubo.emplace_back(s1 * u, s2, s3);
      cubo.emplace_back(s1, s2 * u, s3);
      cubo.emplace_back(s1, s2, s3 * u);
    }
    std::vector<vec2> shadow;
    for (const vec3 &v : cubo) {
      shadow.push_back(vec2{v.x, v.y});
    }

    std::vector<int> hull = ComputeHull(shadow);
    CheckHullProperties(__LINE__, shadow, hull, what);
    DrawPoints(shadow, hull, std::format("cubo-{}.png", what));
    CHECK(hull.size() == 8) << hull.size();
  }

  {
    // This used to create infinite loops in the original
    // OldConvexHull code due to coincident vertices at the start point.
    std::vector<vec2> dupes = {
      vec2{2.4142135623730949, 1},
      vec2{1, 2.4142135623730949},
      vec2{1, 1},
      vec2{2.4142135623730949, 1},
      vec2{1, 2.4142135623730949},
      vec2{1, 1},
      vec2{2.4142135623730949, -1},
      vec2{1, -2.4142135623730949},
      vec2{1, -1},
      vec2{2.4142135623730949, -1},
      vec2{1, -2.4142135623730949},
      vec2{1, -1},
      vec2{-2.4142135623730949, 1},
      vec2{-1, 2.4142135623730949},
      vec2{-1, 1},
      vec2{-2.4142135623730949, 1},
      vec2{-1, 2.4142135623730949},
      vec2{-1, 1},
      vec2{-2.4142135623730949, -1},
      vec2{-1, -2.4142135623730949},
      vec2{-1, -1},
      vec2{-2.4142135623730949, -1},
      vec2{-1, -2.4142135623730949},
      vec2{-1, -1},
    };

    (void)ComputeHull(dupes);
  }

  {
    ArcFour rc("hi");
    [[maybe_unused]]
    int count = 0;
    for (int num_pts = 3; num_pts < 16; num_pts++) {
      for (int i = 0; i < num_pts * num_pts * num_pts; i++) {
        std::vector<vec2> v;
        for (int j = 0; j < num_pts; j++) {
          v.push_back(vec2{
              .x = (double)RandTo(&rc, num_pts * 4) / num_pts - num_pts * 2,
              .y = (double)RandTo(&rc, num_pts * 4) / num_pts - num_pts * 2,
            });
          if (VERBOSE) {
            Print("Test:");
            for (const vec2 &x : v) {
              Print(" {}", VecString(x));
            }
            Print("\n");
          }

          if (v.size() > 2) {
            std::vector<int> hull = ComputeHull(v);
            CheckHullProperties(__LINE__, v, hull, what);

            if (VERBOSE) {
              Print("Hull size: {}\n", hull.size());
            }

            /*
            DrawPoints(v, hull, std::format("{}-{}-test.png",
                                            what, count));
            */
            count++;
          }
        }
      }
    }
  }
}

int main(int argc, char **argv) {
  ANSI::Init();
  Print("\n");

  TestHullRegression3("graham", Hull2D::GrahamScan);
  TestHullRegression2("graham", Hull2D::GrahamScan);
  TestHullRegression1("graham", Hull2D::GrahamScan);
  TestHull("graham", Hull2D::GrahamScan);

  // Gift wrapping algorithm has problems with
  // colinear/coincident points :(
  // TestHullRegression3("wrap", OldConvexHull);
  TestHullRegression2("wrap", Hull2D::GiftWrap);
  TestHullRegression1("wrap", Hull2D::GiftWrap);
  // TestHull("wrap", OldConvexHull);

  TestHullRegression3("quick", Hull2D::QuickHull);
  TestHullRegression2("quick", Hull2D::QuickHull);
  TestHullRegression1("quick", Hull2D::QuickHull);
  TestHull("quick", Hull2D::QuickHull);

  Print("OK\n");
  return 0;
}
