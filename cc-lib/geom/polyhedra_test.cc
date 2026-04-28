
#include "geom/polyhedra.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <format>
#include <numbers>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ansi.h"
#include "arcfour.h"
#include "base/logging.h"
#include "base/print.h"
#include "color-util.h"
#include "geom/point-map.h"
#include "image.h"
#include "randutil.h"
#include "timer.h"
#include "yocto-math.h"

using vec2 = yocto::vec<double, 2>;

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

// Red for negative, black for 0, green for positive.
// nominal range [-1, 1].
static constexpr ColorUtil::Gradient DISTANCE{
  GradRGB(-2.0f, 0xFFFF88),
  GradRGB(-1.0f, 0xFFFF00),
  GradRGB(-0.5f, 0xFF0000),
  GradRGB( 0.0f, 0x440044),
  GradRGB( 0.5f, 0x00FF00),
  GradRGB(+1.0f, 0x00FFFF),
  GradRGB(+2.0f, 0x88FFFF),
};

static void TestSignedDistance() {
  constexpr int width = 1920;
  constexpr int height = 1080;

  constexpr double scale = (double)std::min(width, height);

  auto ToWorld = [](int sx, int sy) -> vec2 {
      // Center of screen should be 0,0.
      double cy = sy - height / 2.0;
      double cx = sx - width / 2.0;
      return vec2{.x = cx / scale, .y = cy / scale};
    };

  auto ToScreen = [](const vec2 &pt) -> std::pair<int, int> {
    double cx = pt.x * scale;
    double cy = pt.y * scale;
    return std::make_pair(cx + width / 2.0, cy + height / 2.0);
  };

  // The triangle
  vec2 pt0(0.15, 0.2);
  vec2 pt1(0.35, -0.15);
  vec2 pt2(-0.05, 0.25);

  ImageRGBA img(width, height);
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      vec2 pt = ToWorld(x, y);
      double dist = TriangleSignedDistance(pt0, pt1, pt2, pt);
      img.BlendPixel32(x, y, ColorUtil::LinearGradient32(DISTANCE, dist));
    }
  }

  auto DrawLine = [&](vec2 a, vec2 b) {
      const auto &[x0, y0] = ToScreen(a);
      const auto &[x1, y1] = ToScreen(b);
      img.BlendLineAA32(x0, y0, x1, y1, 0xFFFFFF44);
    };

  DrawLine(pt0, pt1);
  DrawLine(pt1, pt2);
  DrawLine(pt2, pt0);

  img.Save("triangle.png");
  printf("Wrote " AGREEN("triangle.png") "\n");
}

static void TestPointLineDistance() {
  //
  //        *           f
  //    *---+---------* e
  //    0 1 2 3 4 5 6 7 8
  vec2 linea{0,0};
  vec2 lineb{7,0};
  vec2 side_pt{2, 3};
  vec2 e_pt{8.25, 0};
  vec2 f_pt{7 + std::numbers::sqrt2, std::numbers::sqrt2};

  ArcFour rc("test");
  Timer timer;
  static constexpr int64_t ITERS = 100000;
  for (int i = 0; i < ITERS; i++) {
    // Random point like side point, but on the other side.
    double oside_dist = RandDouble(&rc) * 10.0 + 0.125;
    vec2 oside_pt = {
      RandDouble(&rc) * 7.0,
      oside_dist,
    };

    double theta = RandDouble(&rc) * 2.0 * std::numbers::pi;
    frame2 frame = rotation_frame2(theta);
    frame.o.x = RandDouble(&rc) * 5 - 2.5;
    frame.o.y = RandDouble(&rc) * 5 - 2.5;

    vec2 rlinea = transform_point(frame, linea);
    vec2 rlineb = transform_point(frame, lineb);
    vec2 rside_pt = transform_point(frame, side_pt);
    vec2 re_pt = transform_point(frame, e_pt);
    vec2 rf_pt = transform_point(frame, f_pt);

    vec2 roside_pt = transform_point(frame, oside_pt);

    // In both orientations.
    CHECK_NEAR(PointLineDistance(rlinea, rlineb, rside_pt), 3.0);
    CHECK_NEAR(PointLineDistance(rlineb, rlinea, rside_pt), 3.0);

    CHECK_NEAR(PointLineDistance(rlinea, rlineb, re_pt), 1.25);
    CHECK_NEAR(PointLineDistance(rlineb, rlinea, re_pt), 1.25);

    CHECK_NEAR(PointLineDistance(rlinea, rlineb, rf_pt), 2.0);
    CHECK_NEAR(PointLineDistance(rlineb, rlinea, rf_pt), 2.0);

    CHECK_NEAR(PointLineDistance(rlinea, rlineb, rlinea), 0.0);
    CHECK_NEAR(PointLineDistance(rlineb, rlinea, rlinea), 0.0);

    CHECK_NEAR(PointLineDistance(rlinea, rlineb, roside_pt), oside_dist);
    CHECK_NEAR(PointLineDistance(rlineb, rlinea, roside_pt), oside_dist);
  }

  double spi = timer.Seconds() / ITERS;
  printf("PointLineDistance time: %s\n", ANSI::Time(spi).c_str());
}

static void TestPolyTester1() {
  std::vector<vec2> poly = {
    vec2{0.0, 6.0},
    vec2{-2.0, 5.0},
    vec2{-5.0, -4.0},
    vec2{3.5, 2.0},
    vec2{4.0, 7.0},
  };
  CHECK(IsPolyConvex(poly));
  CHECK(SignedAreaOfConvexPoly(poly) > 0.0);
  CHECK(IsConvexAndScreenClockwise(poly));

  PolyTester2D tester(poly);

  CHECK(tester.IsInside(vec2{0.0, 0.0}));
  CHECK(tester.IsInside(vec2{0.0, 0.01}));
  CHECK(tester.IsInside(vec2{0.01, 0.0}));
  CHECK(tester.IsInside(vec2{0.01, 0.01}));
  CHECK(tester.IsInside(vec2{-1.0, 5.5}));

  ArcFour rc("deterministic");

  for (int i = 0; i < 10000; i++) {
    double x = RandDouble(&rc) * 12.0 - 6.0;
    double y = RandDouble(&rc) * 12.0 - 6.0;
    vec2 v{x, y};

    std::optional<double> odist =
      tester.SquaredDistanceOutside(v);

    // Should agree with the standalone functions.
    if (odist.has_value()) {
      CHECK_NEAR(
          SquaredDistanceToPoly(poly, v),
          odist.value());
    } else {
      CHECK(PointInPolygon(v, poly)) << "\n" <<
        std::format("point #{}: ({:.17g}, {:.17g})\n",
                    i, x, y);
    }
  }
}

static void TestPolyTester2() {
  std::vector<vec2> square = {
    vec2{-1, -1},
    vec2{1, -1},
    vec2{1, 1},
    vec2{-1, 1},
  };
  CHECK(IsPolyConvex(square));
  CHECK(IsConvexAndScreenClockwise(square));
  CHECK(SignedAreaOfConvexPoly(square) > 0.0);

  PolyTester2D tester(square);

  CHECK(tester.IsInside(vec2{0.0, 0.0}));
  CHECK(tester.IsInside(vec2{0.0, 0.01}));
  CHECK(tester.IsInside(vec2{0.01, 0.0}));
  CHECK(tester.IsInside(vec2{0.01, 0.01}));
  CHECK(!tester.IsInside(vec2{-3.0, -1}));
  CHECK(!tester.IsInside(vec2{-3.0, 0}));
  CHECK(!tester.IsInside(vec2{-3.0, 1}));

  CHECK_NEAR(
      tester.SquaredDistanceOutside(vec2{-3.0, -1.0}).value_or(999.0),
      2.0 * 2.0);

  CHECK_NEAR(
      tester.SquaredDistanceOutside(vec2{-3.0, -0.8}).value_or(999.0),
      2.0 * 2.0);

  CHECK_NEAR(
      tester.SquaredDistanceOutside(vec2{3.0, 0.0}).value_or(999.0),
      2.0 * 2.0);

  CHECK_NEAR(
      tester.SquaredDistanceOutside(vec2{0.0, 3.0}).value_or(999.0),
      2.0 * 2.0);

  CHECK_NEAR(
      tester.SquaredDistanceOutside(vec2{1.0, 3.0}).value_or(999.0),
      2.0 * 2.0);

  CHECK_NEAR(
      tester.SquaredDistanceOutside(vec2{-1.0, 3.0}).value_or(999.0),
      2.0 * 2.0);

  CHECK_NEAR(
      tester.SquaredDistanceOutside(vec2{1.0, -3.0}).value_or(999.0),
      2.0 * 2.0);

  CHECK_NEAR(
      tester.SquaredDistanceOutside(vec2{-1.0, -3.0}).value_or(999.0),
      2.0 * 2.0);
}

static bool ConvexPolyhedraAlmostEq(const Polyhedron &a,
                                    const Polyhedron &b) {
  if (a.vertices.size() != b.vertices.size()) {
    return false;
  }

  PointSet3 pts_a;
  for (const vec3 &v : a.vertices) {
    pts_a.Add(v);
  }

  PointSet3 pts_b;
  for (const vec3 &v : b.vertices) {
    pts_b.Add(v);
  }

  for (const vec3 &v : b.vertices) {
    if (!pts_a.Contains(v)) {
      return false;
    }
  }

  for (const vec3 &v : a.vertices) {
    if (!pts_b.Contains(v)) {
      return false;
    }
  }

  return true;
}

static void TestDualize() {
  // Test that dualizing a polyhedron twice gets us back close
  // to the original. Because the order of vertices and edges
  // is not guaranteed, we compare using the function above.
  #define TEST_DUAL(p) do {                           \
      Polyhedron orig = (p);                          \
      Polyhedron d = DualizePoly(orig);               \
      Polyhedron dd = DualizePoly(d);                 \
      CHECK(ConvexPolyhedraAlmostEq(orig, dd)) << #p; \
  } while (false)

  TEST_DUAL(Cube());
  TEST_DUAL(Tetrahedron());
  TEST_DUAL(Octahedron());
  TEST_DUAL(Dodecahedron());
  TEST_DUAL(Icosahedron());
  TEST_DUAL(SnubCube());
  TEST_DUAL(Noperthedron());
  TEST_DUAL(PentagonalIcositetrahedron());
  Print("Dualization OK!\n");
}

static void TestInTriangle() {
  vec2 a{0.0, 0.0};
  vec2 b{10.0, 0.0};
  vec2 c{0.0, 10.0};

  CHECK(InTriangle(a, b, c, vec2{1.0, 1.0}));
  CHECK(InTriangle(a, b, c, vec2{2.0, 5.0}));

  // Outside
  CHECK(!InTriangle(a, b, c, vec2{-1.0, 1.0}));
  CHECK(!InTriangle(a, b, c, vec2{1.0, -1.0}));
  CHECK(!InTriangle(a, b, c, vec2{6.0, 6.0}));

  // Both winding orders
  CHECK(InTriangle(a, c, b, vec2{1.0, 1.0}));
  CHECK(!InTriangle(a, c, b, vec2{-1.0, 1.0}));
}

static void TestPointInPolygon() {
  std::vector<vec2> poly = {
    vec2{0.0, 0.0},
    vec2{10.0, 0.0},
    vec2{10.0, 10.0},
    vec2{5.0, 5.0},
    vec2{0.0, 10.0},
  };

  CHECK(PointInPolygon(vec2{5.0, 2.0}, poly));
  CHECK(!PointInPolygon(vec2{5.0, 8.0}, poly));
  CHECK(!PointInPolygon(vec2{-1.0, 5.0}, poly));
}

static void TestPolyhedronTransformations() {
  {
    Polyhedron cube = Cube();
    // Cube is 2x2x2 centered at origin.
    CHECK_NEAR(Diameter(cube), std::sqrt(12.0));

    Polyhedron scaled = Scale(cube, 2.0);
    CHECK_NEAR(Diameter(scaled), std::sqrt(12.0) * 2.0);
  }

  {
    Polyhedron cube = Cube();
    // Normalizing radius should make all points distance 1 from origin.
    Polyhedron normalized = NormalizeRadius(cube);
    for (const vec3 &v : normalized.vertices) {
      CHECK_NEAR(yocto::length(v), 1.0);
    }
  }

  {
    Polyhedron cube = Cube();
    Polyhedron moved = cube;
    for (vec3 &v : moved.vertices) {
      v += vec3{1.0, 2.0, 3.0};
    }
    Polyhedron recentered = Recenter(moved);
    for (int i = 0; i < (int)cube.vertices.size(); i++) {
      CHECK_NEAR(cube.vertices[i].x, recentered.vertices[i].x);
      CHECK_NEAR(cube.vertices[i].y, recentered.vertices[i].y);
      CHECK_NEAR(cube.vertices[i].z, recentered.vertices[i].z);
    }
  }
}

static void TestPlanarityError() {
  {
    Polyhedron cube = Cube();
    CHECK_NEAR(PlanarityError(cube), 0.0);
  }

  {
    // Perturb a vertex to make it non-planar.
    Polyhedron bad_cube = Cube();
    bad_cube.vertices[0].z += 1.0;
    CHECK(PlanarityError(bad_cube) > 0.1);
  }
}

static void TestHullDistances() {
  std::vector<vec2> pts = {
    vec2{0.0, 0.0},
    vec2{10.0, 0.0},
    vec2{10.0, 10.0},
    vec2{0.0, 10.0},
  };
  std::vector<int> hull = {0, 1, 2, 3};

  CHECK_NEAR(DistanceToHull(pts, hull, vec2{12.0, 5.0}), 2.0);

  auto [closest, dist] = ClosestPointOnHull(pts, hull, vec2{12.0, 5.0});
  CHECK_NEAR(dist, 2.0);
  CHECK_NEAR(closest.x, 10.0);
  CHECK_NEAR(closest.y, 5.0);
}

static void TestSignedDistanceToEdgeEndpoints() {
  vec2 v0{0.0, 0.0};
  vec2 v1{10.0, 0.0};

  // The distance to the infinite line is 4.0.
  // The distance to the segment is the distance to v0, which is 5.0.
  double dist = SignedDistanceToEdge(v0, v1, vec2{-3.0, 4.0});

  CHECK_NEAR(dist, 5.0);
}

static void TestStructure() {
  auto CheckPoly = [](const Polyhedron &p,
                      // expected vertices, edges, faces
                      int vs, int es, int fs) {
      CHECK(p.faces.get() != nullptr);
      const Faces &faces = *p.faces;

      CHECK((int)p.vertices.size() == vs);
      CHECK(faces.NumVertices() == vs);
      CHECK((int)faces.v.size() == fs);
      CHECK((int)faces.edges.size() == es);

      // Euler characteristic: V - E + F = 2
      CHECK(vs - es + fs == 2);

      for (const std::vector<int> &nbs : faces.neighbors) {
        CHECK(std::is_sorted(nbs.begin(), nbs.end()));
        CHECK(nbs.size() >= 3);
      }

      std::vector<int> edge_counts(vs, 0);

      auto Contains = [](const std::vector<int> &vec, int val) {
          return std::find(vec.begin(), vec.end(), val) != vec.end();
        };

      for (const Faces::Edge &edge : faces.edges) {
        // Edge invariants.
        CHECK(edge.v0 >= 0);
        CHECK(edge.v0 < edge.v1) << p.name;
        CHECK(edge.v1 < vs);

        CHECK(edge.f0 >= 0);
        CHECK(edge.f0 < edge.f1);
        CHECK(edge.f1 < fs);

        edge_counts[edge.v0]++;
        edge_counts[edge.v1]++;

        // Implied invariants:
        // The faces f0 and f1 must both contain v0 and v1.
        CHECK(Contains(faces.v[edge.f0], edge.v0));
        CHECK(Contains(faces.v[edge.f0], edge.v1));
        CHECK(Contains(faces.v[edge.f1], edge.v0));
        CHECK(Contains(faces.v[edge.f1], edge.v1));

        // v0 and v1 should be listed as neighbors of each other.
        CHECK(Contains(faces.neighbors[edge.v0], edge.v1));
        CHECK(Contains(faces.neighbors[edge.v1], edge.v0));
      }

      for (int i = 0; i < vs; i++) {
        // The number of edges touching a vertex should equal its degree.
        CHECK(edge_counts[i] == (int)faces.neighbors[i].size());
      }

      for (int f = 0; f < fs; f++) {
        const std::vector<int> &fv = faces.v[f];
        CHECK(fv.size() >= 3);
        for (int v : fv) {
          CHECK(v >= 0 && v < vs);
        }
      }

      // Check for consistent orientation (winding order) across all edges.
      // Each edge should be traversed in opposite directions by the two
      // faces that share it.
      for (const Faces::Edge &edge : faces.edges) {
        auto GetDir = [&](int f) {
            const std::vector<int> &fv = faces.v[f];
            for (int i = 0; i < (int)fv.size(); i++) {
              int a = fv[i];
              int b = fv[(i + 1) % fv.size()];
              if (a == edge.v0 && b == edge.v1) return 1;
              if (a == edge.v1 && b == edge.v0) return -1;
            }
            return 0;
          };

        int d0 = GetDir(edge.f0);
        int d1 = GetDir(edge.f1);

        CHECK(d0 != 0) << "Edge missing from face " << edge.f0;
        CHECK(d1 != 0) << "Edge missing from face " << edge.f1;
        CHECK(d0 == -d1) << "Inconsistent winding order on edge "
                         << edge.v0 << "-" << edge.v1 << " between faces "
                         << edge.f0 << " and " << edge.f1;
      }

      // This is mostly redundant with the checks above, so we
      // check it last for better error messages.
      CHECK(IsManifold(p));
      CHECK(IsWellConditioned(p.vertices));
    };

  CheckPoly(Tetrahedron(), 4, 6, 4);
  CheckPoly(Cube(), 8, 12, 6);
  CheckPoly(Octahedron(), 6, 12, 8);
  CheckPoly(Dodecahedron(), 20, 30, 12);
  CheckPoly(Icosahedron(), 12, 30, 20);

  CheckPoly(TruncatedTetrahedron(), 12, 18, 8);
  CheckPoly(Cuboctahedron(), 12, 24, 14);
  CheckPoly(TruncatedCube(), 24, 36, 14);
  CheckPoly(TruncatedOctahedron(), 24, 36, 14);
  CheckPoly(Rhombicuboctahedron(), 24, 48, 26);
  CheckPoly(SnubCube(), 24, 60, 38);
  CheckPoly(Icosidodecahedron(), 30, 60, 32);
  CheckPoly(TruncatedIcosahedron(), 60, 90, 32);
  CheckPoly(SnubDodecahedron(), 60, 150, 92);

  CheckPoly(TriakisTetrahedron(), 8, 18, 12);
  CheckPoly(RhombicDodecahedron(), 14, 24, 12);
  CheckPoly(TriakisOctahedron(), 14, 36, 24);
  CheckPoly(PentagonalIcositetrahedron(), 38, 60, 24);
  CheckPoly(RhombicTriacontahedron(), 32, 60, 30);
  CheckPoly(PentagonalHexecontahedron(), 92, 150, 60);

  CheckPoly(NPrism(3, 1.0), 6, 9, 5);
  CheckPoly(NPrism(5, 1.0), 10, 15, 7);
  CheckPoly(NAntiPrism(4, 1.0), 8, 16, 10);

  CheckPoly(Noperthedron(), 90, 240, 152);
}

int main(int argc, char **argv) {
  ANSI::Init();
  Print("\n");

  TestSignedDistance();
  TestPointLineDistance();

  TestPolyTester1();
  TestPolyTester2();

  TestDualize();

  TestInTriangle();
  TestPointInPolygon();
  TestPolyhedronTransformations();
  TestPlanarityError();
  TestHullDistances();
  TestSignedDistanceToEdgeEndpoints();

  TestStructure();

  Print("OK\n");
  return 0;
}
