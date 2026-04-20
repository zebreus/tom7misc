
#include "polyhedra.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <format>
#include <numbers>
#include <optional>
#include <string>
#include <tuple>
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
#include "rendering.h"
#include "ruperts-util.h"
#include "yocto-math.h"

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

template<typename T>
inline int sgn(T val) {
  return (T(0) < val) - (val < T(0));
}

static void DrawPoints(const std::vector<vec2> &pts,
                       // Can be empty
                       const std::vector<int> &hull,
                       const std::string &filename) {
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
    img.BlendText2x32(x, y, Rendering::Color(i), label);
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

  if (!IsHullConvex(v, hull)) {
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

static void TestCircle() {
  ArcFour rc("circle");

  for (int iters = 0; iters < 1000; iters++) {
    std::vector<vec2> points;
    for (int j = 0; j < 15; j++) {
      points.emplace_back(RandDouble(&rc) * 4.0 - 2.0,
                          RandDouble(&rc) * 4.0 - 2.0);
    }

    std::vector<int> hull = QuickHull(points);
    if (!PointInPolygon(vec2{0.0, 0.0}, points, hull)) {
      // This is a precondition, so try again.
      iters--;
      continue;
    }

    HullCircumscribedCircle circumscribed(points, hull);
    HullInscribedCircle inscribed(points, hull);

    // Test a bunch of points.
    for (int j = 0; j < 100; j++) {
      vec2 pt{
        .x = RandDouble(&rc) * 4.0 - 2.0,
        .y = RandDouble(&rc) * 4.0 - 2.0
      };

      if (circumscribed.DefinitelyOutside(pt)) {
        CHECK(!PointInPolygon(pt, points, hull)) << VecString(pt);
      }

      if (inscribed.DefinitelyInside(pt)) {
        CHECK(PointInPolygon(pt, points, hull)) << VecString(pt);
      }
    }
  }
}

static void TestUnpackRot() {
  ArcFour rc("unpack");
  for (int i = 0; i < 1000; i++) {
    const quat4 qi = RandomQuaternion(&rc);
    const frame3 frame = yocto::rotation_frame(qi);
    quat4 qo;
    vec3 trans;
    std::tie(qo, trans) = UnpackFrame(frame);
    CHECK_NEAR(trans.x, 0.0);
    CHECK_NEAR(trans.y, 0.0);
    CHECK_NEAR(trans.z, 0.0);

    /*
    Print("{}\n ** to **\n {}\n",
          QuatString(qi),
          QuatString(qo));
    */

    // the quaternion could either be qi or -qi.
    if (IsNear(qi.x, -qo.x) &&
        IsNear(qi.y, -qo.y) &&
        IsNear(qi.z, -qo.z) &&
        IsNear(qi.w, -qo.w)) {
      qo = quat4{.x = -qo.x, .y = -qo.y, .z = -qo.z, .w = -qo.w};
    }

    CHECK_NEAR(qi.x, qo.x);
    CHECK_NEAR(qi.y, qo.y);
    CHECK_NEAR(qi.z, qo.z);
    CHECK_NEAR(qi.w, qo.w);
  }
}

static void TestUnpackFull() {
  ArcFour rc("unpack");
  for (int i = 0; i < 1000; i++) {
    const quat4 qi = RandomQuaternion(&rc);

    const vec3 ti = vec3(RandDouble(&rc) * 2.0 - 1.0,
                         RandDouble(&rc) * 2.0 - 1.0,
                         RandDouble(&rc) * 2.0 - 1.0);

    const frame3 frame = yocto::translation_frame(ti) *
      yocto::rotation_frame(qi);
    quat4 qo;
    vec3 trans;
    std::tie(qo, trans) = UnpackFrame(frame);
    CHECK_NEAR(trans.x, ti.x);
    CHECK_NEAR(trans.y, ti.y);
    CHECK_NEAR(trans.z, ti.z);

    // the quaternion could either be qi or -qi.
    if (IsNear(qi.x, -qo.x) &&
        IsNear(qi.y, -qo.y) &&
        IsNear(qi.z, -qo.z) &&
        IsNear(qi.w, -qo.w)) {
      qo = quat4{.x = -qo.x, .y = -qo.y, .z = -qo.z, .w = -qo.w};
    }

    CHECK_NEAR(qi.x, qo.x);
    CHECK_NEAR(qi.y, qo.y);
    CHECK_NEAR(qi.z, qo.z);
    CHECK_NEAR(qi.w, qo.w);
  }
}

// Tests a bug with the loss function that assumed the origin was
// in the polyhedron (thx dwrensha).
static void TestLossRegression() {

  const Polyhedron poly = []() {
      auto opoly = PolyhedronFromConvexVertices(
          std::vector<vec3>{
            {-0.7888254090932487,0.1809666339064286,0.5873717318542367},
            {-0.7275239611943676,-0.5176379718006908,-0.45028859194756815},
            {-0.46761652842176743,-8.458653528836922e-05,0.8839314312727501},
            {0.012255643227897352,0.6097001740588195,0.7925373789050683},
            {-0.39543790974206744,-0.460739697734118,0.7945739679039946},
            {-0.6113020439400112,0.2438926374222257,-0.752878604083019}
          });

      CHECK(opoly.has_value());
      return opoly.value();
    }();

  frame3 outer_frame =
    frame3{vec3{-0.28122289782544885,0.8771668061830272,-0.3892198297483371},
           vec3{0.9013115757915298,0.10217934077188853,-0.4209475331482672},
           vec3{-0.3294709776032578,-0.46918842318421455,-0.8193357666226122},
           vec3{0,0,0}};
  frame3 inner_frame =
    frame3{vec3{-0.9900711776109041,0.10560397200855935,0.09277318772238792},
           vec3{-0.10077001875482698,-0.9933693480580881,0.05534204241624576},
           vec3{0.09800238050328079,0.04544380523971492,0.9941480744743338},
           vec3{-0.4725374179836337,-0.20260639570538497,0}};

  double loss = LossFunction(poly, outer_frame, inner_frame);
  CHECK(loss > 0.0);

  std::optional<double> oclearance =
    GetClearance(poly, outer_frame, inner_frame);
  CHECK(!oclearance.has_value()) << oclearance.value();
}

int main(int argc, char **argv) {
  ANSI::Init();
  Print("\n");

  TestHullRegression3("graham", GrahamScan);
  TestHullRegression2("graham", GrahamScan);
  TestHullRegression1("graham", GrahamScan);
  TestHull("graham", GrahamScan);

  // Gift wrapping algorithm has problems with
  // colinear/coincident points :(
  // TestHullRegression3("wrap", OldConvexHull);
  TestHullRegression2("wrap", GiftWrapConvexHull);
  TestHullRegression1("wrap", GiftWrapConvexHull);
  // TestHull("wrap", OldConvexHull);

  TestHullRegression3("quick", QuickHull);
  TestHullRegression2("quick", QuickHull);
  TestHullRegression1("quick", QuickHull);
  TestHull("quick", QuickHull);

  TestCircle();

  TestUnpackRot();
  TestUnpackFull();

  TestLossRegression();

  Print("OK\n");
  return 0;
}
