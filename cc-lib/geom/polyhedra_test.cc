
#include "geom/polyhedra.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <optional>
#include <utility>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "geom/point-map.h"
#include "yocto-math.h"

using vec2 = yocto::vec<double, 2>;

#define CHECK_NEAR(f, g) do {                                           \
  const double fv = (f);                                                \
  const double gv = (g);                                                \
  const double e = std::abs(fv - gv);                                   \
  CHECK(e < 0.0000001) << "Expected " << #f << " and " << #g <<         \
    " to be close, but got: " <<                                        \
    std::format("{:.17g} and {:.17g}, with err {:.17g}", fv, gv, e);   \
  } while (0)

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

static void TestGetByName() {
  auto po = PolyhedronByName("cube");
  CHECK(po.has_value());
  CHECK(po.value().faces->NumFaces() == 8);

  CHECK(!PolyhedronByName("").has_value());
  CHECK(!PolyhedronByName("fakeohedron").has_value());
}

int main(int argc, char **argv) {
  ANSI::Init();
  Print("\n");

  TestDualize();

  TestPolyhedronTransformations();
  TestPlanarityError();
  TestHullDistances();

  TestStructure();

  TestGetByName();

  Print("OK\n");
  return 0;
}
