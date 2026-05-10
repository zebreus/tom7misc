
#include "albrecht.h"

#include <cstdlib>
#include <ctime>
#include <format>
#include <string_view>
#include <utility>
#include <vector>

#include "ansi.h"
#include "arcfour.h"
#include "base/logging.h"
#include "base/print.h"
#include "bit-string.h"
#include "geom/polyhedra.h"
#include "randutil.h"
#include "svg.h"
#include "union-find.h"
#include "util.h"
#include "yocto-math.h"

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

static void CheckAugmentedPoly(const Albrecht::AugmentedPoly &aug,
                               std::string_view name) {
  const Polyhedron &poly = aug.poly;
  const Faces &faces = *poly.faces;

  CHECK(aug.face_edges.size() == faces.NumFaces());
  CHECK(aug.polygons.size() == faces.NumFaces());
  CHECK(aug.edge_transforms.size() == faces.NumEdges());

  for (int f = 0; f < faces.NumFaces(); f++) {
    const std::vector<int> &fv = faces.v[f];
    const auto &face_edges = aug.face_edges[f];
    const auto &poly2d = aug.polygons[f];

    CHECK(face_edges.size() == fv.size());
    CHECK(poly2d.size() == fv.size());

    CHECK_NEAR(poly2d[0].x, 0.0);
    CHECK_NEAR(poly2d[0].y, 0.0);

    // First edge must be vertical.
    CHECK_NEAR(poly2d[1].x, 0.0);
    CHECK(poly2d[1].y > 0.0);

    // Cartesian CW (screen CCW) means area < 0.
    // SignedAreaOfConvexPoly is positive if screen clockwise (cartesian ccw).
    // So for cartesian cw, it should be negative.
    double area = SignedAreaOfConvexPoly(poly2d);
    CHECK(area < 0.0) << "Expected Cartesian CW winding order in " << name;

    // Verify face edges correspond to face vertices
    for (size_t i = 0; i < fv.size(); i++) {
      int v0 = fv[i];
      int v1 = fv[(i + 1) % fv.size()];
      int eidx = face_edges[i];

      CHECK(eidx >= 0 && eidx < faces.NumEdges());
      const Faces::Edge &edge = faces.edges[eidx];

      int minv = v0 < v1 ? v0 : v1;
      int maxv = v0 > v1 ? v0 : v1;
      CHECK(edge.v0 == minv);
      CHECK(edge.v1 == maxv);
      CHECK(edge.f0 == f || edge.f1 == f);
      CHECK(edge.f0 != edge.f1);
    }
  }

  auto FindInFace = [](const std::vector<int> &face, int v) {
      for (int i = 0; i < (int)face.size(); ++i) {
        if (face[i] == v) return i;
      }
      return -1;
    };

  for (int e = 0; e < faces.NumEdges(); e++) {
    const Faces::Edge &edge = faces.edges[e];
    const auto &[f1_to_f0, f0_to_f1] = aug.edge_transforms[e];

    int i0_f0 = FindInFace(faces.v[edge.f0], edge.v0);
    int i1_f0 = FindInFace(faces.v[edge.f0], edge.v1);
    CHECK(i0_f0 >= 0 && i1_f0 >= 0);

    int i0_f1 = FindInFace(faces.v[edge.f1], edge.v0);
    int i1_f1 = FindInFace(faces.v[edge.f1], edge.v1);
    CHECK(i0_f1 >= 0 && i1_f1 >= 0);

    vec2 p0 = aug.polygons[edge.f0][i0_f0];
    vec2 p1 = aug.polygons[edge.f0][i1_f0];

    vec2 q0 = aug.polygons[edge.f1][i0_f1];
    vec2 q1 = aug.polygons[edge.f1][i1_f1];

    // Transform q from f1 to f0's coordinate system
    vec2 tf1_to_f0_q0 = yocto::transform_point(f1_to_f0, q0);
    vec2 tf1_to_f0_q1 = yocto::transform_point(f1_to_f0, q1);

    CHECK_NEAR(tf1_to_f0_q0.x, p0.x);
    CHECK_NEAR(tf1_to_f0_q0.y, p0.y);
    CHECK_NEAR(tf1_to_f0_q1.x, p1.x);
    CHECK_NEAR(tf1_to_f0_q1.y, p1.y);

    // Transform p from f0 to f1's coordinate system
    vec2 tf0_to_f1_p0 = yocto::transform_point(f0_to_f1, p0);
    vec2 tf0_to_f1_p1 = yocto::transform_point(f0_to_f1, p1);

    CHECK_NEAR(tf0_to_f1_p0.x, q0.x);
    CHECK_NEAR(tf0_to_f1_p0.y, q0.y);
    CHECK_NEAR(tf0_to_f1_p1.x, q1.x);
    CHECK_NEAR(tf0_to_f1_p1.y, q1.y);
  }
}

static void TestHasSeparatingAxis() {
  const std::vector<vec2> p1 = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
  const std::vector<vec2> p2 = {{2, 0}, {3, 0}, {3, 1}, {2, 1}};

  // Totally separated, axis-aligned.
  CHECK(Albrecht::HasSeparatingAxis(p1, p2));
  CHECK(Albrecht::HasSeparatingAxis(p2, p1));

  // Overlapping.
  const std::vector<vec2> p3 = {{0.5, 0.5}, {1.5, 0.5},
                                {1.5, 1.5}, {0.5, 1.5}};
  CHECK(!Albrecht::HasSeparatingAxis(p1, p3));
  CHECK(!Albrecht::HasSeparatingAxis(p3, p1));

  // One contained in the other.
  const std::vector<vec2> p4 = {{0.1, 0.1}, {0.9, 0.1},
                                {0.9, 0.9}, {0.1, 0.9}};
  CHECK(!Albrecht::HasSeparatingAxis(p1, p4));
  CHECK(!Albrecht::HasSeparatingAxis(p4, p1));

  // Touching at an edge. (These are considered "separating enough.")
  const std::vector<vec2> p5 = {{1, 0}, {2, 0}, {2, 1}, {1, 1}};
  CHECK(Albrecht::HasSeparatingAxis(p1, p5));
  CHECK(Albrecht::HasSeparatingAxis(p5, p1));

  // Touching at a vertex. (Also considered "separating enough.")
  const std::vector<vec2> p6 = {{-1, -1}, {0, -1}, {0, 0}, {-1, 0}};
  CHECK(Albrecht::HasSeparatingAxis(p1, p6));
  CHECK(Albrecht::HasSeparatingAxis(p6, p1));

  // Asymmetric case; HasSeparatingAxis checks only the
  // axes from p1.
  const std::vector<vec2> p7 = {{0.9, 1.2}, {1.2, 0.9}, {2.0, 2.0}};
  CHECK(!Albrecht::HasSeparatingAxis(p1, p7));
  CHECK(Albrecht::HasSeparatingAxis(p7, p1));
}

static void Unfold(Polyhedron poly_in, std::string_view name) {
  static ArcFour *rc = new ArcFour(std::format("{}", time(nullptr)));

  Albrecht::AugmentedPoly aug(std::move(poly_in));
  CheckAugmentedPoly(aug, name);

  const Faces &faces = *aug.poly.faces;

  const int num_faces = faces.NumFaces();
  const int num_edges = faces.NumEdges();
  BitString unfolding(num_edges, false);

  // Greedily connect them, but in some random order.
  std::vector<int> edges;
  for (int i = 0; i < num_edges; i++)
    edges.push_back(i);
  Shuffle(rc, &edges);

  UnionFind uf(num_faces);
  for (int i : edges) {
    const Faces::Edge &edge = faces.edges[i];
    if (uf.Find(edge.f0) != uf.Find(edge.f1)) {
      uf.Union(edge.f0, edge.f1);
      unfolding.Set(i, true);
    }
  }

  Albrecht::DebugResult dr =
    Albrecht::DebugUnfolding(aug, unfolding);

  Print("For " ABLUE("{}") ":\n", name);

  Print("Cycle free? {}\n",
        dr.cycle_free ? AGREEN("true") : ARED("false"));
  if (dr.cycle.has_value()) {
    Print("Cycle:" ANSI_RED);
    for (int v : dr.cycle.value()) {
      Print(" {}", v);
    }
    Print(ANSI_RESET "\n");
  }
  Print("Connected? {}\n",
        dr.is_connected ? AGREEN("true") : ARED("false"));
  Print("Planar? {}\n",
        dr.is_planar ? AGREEN("true") : ARED("false"));
  for (int f0 = 0; f0 < num_faces; f0++) {
    int f1 = dr.face_overlap[f0];
    if (f0 != -1) {
      Print("Overlapping: " ARED("{} {}") "\n", f0, f1);
      // Just print one.
      break;
    }
  }
  Print("Is net: {}\n", dr.is_net ? AGREEN("true") : ARED("false"));

  SVG::Doc svg = Albrecht::MakeSVG(aug, dr, true, true);

  Util::WriteFile(std::format("test-{}.svg", name),
                  SVG::ToSVG(svg));

  CHECK(dr.cycle_free) << "By construction!";
  CHECK(dr.is_connected) << "By construction!";
  CHECK(dr.is_net == Albrecht::IsNet(aug, unfolding));
}

static void TestStretchFactor() {
  // A BFS tree on a Cube's faces always yields the exact same
  // topology regardless of face order: a root (Top) connected to 4
  // side faces, and one side face connected to the Bottom face. The
  // max stretch is exactly 3 (between the Bottom face and a side face
  // not connected to it). Starting at face 0 works generically
  // because the cube is face-transitive.

  Albrecht::AugmentedPoly aug(Cube());
  const Faces &faces = *aug.poly.faces;
  int num_faces = faces.NumFaces();
  int num_edges = faces.NumEdges();

  BitString unfolding(num_edges, false);
  std::vector<int> q;
  std::vector<bool> visited(num_faces, false);

  q.push_back(0);
  visited[0] = true;
  size_t head = 0;

  while (head < q.size()) {
    int curr = q[head++];
    for (int e : aug.face_edges[curr]) {
      const Faces::Edge &edge = faces.edges[e];
      int nxt = (edge.f0 == curr) ? edge.f1 : edge.f0;
      if (!visited[nxt]) {
        visited[nxt] = true;
        unfolding.Set(e, true);
        q.push_back(nxt);
      }
    }
  }

  Albrecht::Stretch stretch = Albrecht::StretchFactor(aug, unfolding);
  CHECK(stretch.f0 < stretch.f1);
  CHECK(stretch.distance_3d == 1);
  CHECK(stretch.unfolded_distance == 3);
}


int main(int argc, char **argv) {
  ANSI::Init();

  TestHasSeparatingAxis();

  TestStretchFactor();

  Unfold(Icosahedron(), "icos");
  Unfold(Dodecahedron(), "dodec");
  Unfold(Noperthedron(), "nope");
  Unfold(Onperthedron(), "onpe");

  Unfold(TruncatedTetrahedron(), "truncatedtetrahedron");
  Unfold(Cuboctahedron(), "cuboctahedron");
  Unfold(TruncatedCube(), "truncatedcube");
  Unfold(TruncatedOctahedron(), "truncatedoctahedron");
  Unfold(Rhombicuboctahedron(), "rhombicuboctahedron");
  Unfold(TruncatedCuboctahedron(), "truncatedcuboctahedron");
  Unfold(SnubCube(), "snubcube");
  Unfold(Icosidodecahedron(), "icosidodecahedron");
  Unfold(TruncatedDodecahedron(), "truncateddodecahedron");
  Unfold(TruncatedIcosahedron(), "truncatedicosahedron");
  Unfold(Rhombicosidodecahedron(), "rhombicosidodecahedron");
  Unfold(TruncatedIcosidodecahedron(), "truncatedicosidodecahedron");
  Unfold(SnubDodecahedron(), "snubdodecahedron");

  Unfold(TriakisTetrahedron(), "triakistetrahedron");
  Unfold(RhombicDodecahedron(), "rhombicdodecahedron");
  Unfold(TriakisOctahedron(), "triakisoctahedron");
  Unfold(TetrakisHexahedron(), "tetrakishexahedron");
  Unfold(DeltoidalIcositetrahedron(), "deltoidalicositetrahedron");
  Unfold(DisdyakisDodecahedron(), "disdyakisdodecahedron");
  Unfold(DeltoidalHexecontahedron(), "deltoidalhexecontahedron");
  Unfold(PentagonalIcositetrahedron(), "pentagonalicositetrahedron");
  Unfold(RhombicTriacontahedron(), "rhombictriacontahedron");
  Unfold(TriakisIcosahedron(), "triakisicosahedron");
  Unfold(PentakisDodecahedron(), "pentakisdodecahedron");
  Unfold(DisdyakisTriacontahedron(), "disdyakistriacontahedron");
  Unfold(PentagonalHexecontahedron(), "pentagonalhexecontahedron");

  Print("OK\n");
  return 0;
}
