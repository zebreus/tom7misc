
#include "base/stringprintf.h"
#include "folding.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "albrecht.h"
#include "ansi.h"
#include "arcfour.h"
#include "base/logging.h"
#include "base/print.h"
#include "examples.h"
#include "geom/polyhedra.h"
#include "status-bar.h"
#include "timer.h"
#include "yocto-math.h"

static StatusBar *status = nullptr;

static void CheckDistances(const Polyhedron &poly,
                           std::vector<double> expected_distances) {
  std::vector<double> actual_distances;
  int n = poly.vertices.size();
  for (int i = 0; i < n; i++) {
    for (int j = i + 1; j < n; j++) {
      actual_distances.push_back(
          yocto::length(poly.vertices[i] - poly.vertices[j]));
    }
  }
  std::sort(actual_distances.begin(), actual_distances.end());
  std::sort(expected_distances.begin(), expected_distances.end());
  CHECK(actual_distances.size() == expected_distances.size())
      << "Expected " << expected_distances.size() << " distances, got "
      << actual_distances.size();
  for (size_t i = 0; i < actual_distances.size(); i++) {
    CHECK(std::abs(actual_distances[i] - expected_distances[i]) < 1e-5)
        << "Distance mismatch at index " << i << ": expected "
        << expected_distances[i] << ", got " << actual_distances[i];
  }
}

static void CheckOnePoly(const Polyhedron &poly,
                         std::string_view name) {
  Timer timer;
  status->Status("Round trip for " ACYAN("{}"), name);

  Albrecht::AugmentedPoly aug(poly);
  ArcFour rc(name);

  // Find an unfolding (net or non-net)
  Examples ex = GetSomeExamples(&rc, aug, NoConstraint{},
                                std::nullopt, 1, 1, false);
  CHECK(!ex.nets.empty() || !ex.non_nets.empty())
      << "No examples found for " << name;

  // We can use non-nets here because we now use the polyhedron's
  // connectivity (via polyhedron_vertex) to disambiguate overlaps,
  // preventing unrelated vertices from being incorrectly merged.
  const Albrecht::DebugResult &res =
      !ex.nets.empty() ? ex.nets.front() : ex.non_nets.front();

  // Convert Albrecht::UnfoldedMesh to Folding::UnfoldedMesh
  Folding::UnfoldedMesh umesh;
  std::vector<int> umesh_poly_vertex;
  std::vector<int> index_map(res.mesh.vertices.size());
  for (size_t i = 0; i < res.mesh.vertices.size(); i++) {
    int found = -1;
    for (size_t j = 0; j < umesh.vertices.size(); j++) {
      if (res.mesh.polyhedron_vertex[i] == umesh_poly_vertex[j] &&
          yocto::length(res.mesh.vertices[i] - umesh.vertices[j]) < 1e-5) {
        found = static_cast<int>(j);
        break;
      }
    }
    if (found == -1) {
      index_map[i] = static_cast<int>(umesh.vertices.size());
      umesh.vertices.push_back(res.mesh.vertices[i]);
      umesh_poly_vertex.push_back(res.mesh.polyhedron_vertex[i]);
    } else {
      index_map[i] = found;
    }
  }

  for (const Albrecht::PlacedFace &pf : res.mesh.polygons) {
    if (!pf.v.empty()) {
      std::vector<int> poly;
      poly.reserve(pf.v.size());
      for (int v : pf.v) {
        poly.push_back(index_map[v]);
      }
      umesh.polygons.push_back(poly);
    }
  }

  #if 1
  status->Print("--- umesh for {} ---\n", name);
  for (size_t i = 0; i < umesh.vertices.size(); i++) {
    status->Print("  v[{}]: ({}, {})\n", i,
                  umesh.vertices[i].x, umesh.vertices[i].y);
  }
  for (size_t i = 0; i < umesh.polygons.size(); i++) {
    std::string pp;
    for (int v : umesh.polygons[i]) {
      AppendFormat(&pp, " {}", v);
    }
    status->Print("  poly[{}]:{}\n", i, pp);
  }
  status->Print("----------------------\n");
  #endif

  // Refold the 2D mesh into a polyhedron
  status->Status("Refolding " ACYAN("{}"), name);
  std::optional<Polyhedron> folded = Folding::Fold(umesh);
  CHECK(folded.has_value()) << "Failed to fold " << name;

  // Generate expected pairwise distances from the original polyhedron
  std::vector<double> expected;
  int n = poly.vertices.size();
  expected.reserve(n * (n - 1) / 2);
  for (int i = 0; i < n; i++) {
    for (int j = i + 1; j < n; j++) {
      expected.push_back(yocto::length(poly.vertices[i] - poly.vertices[j]));
    }
  }

  // Verify that the folded geometry matches the original
  status->Status("Check distances for " ACYAN("{}"), name);
  CheckDistances(*folded, expected);
  status->Print("{} ok in {}\n", name, ANSI::Time(timer.Seconds()));
}

static void TestTetrahedron() {
  Folding::UnfoldedMesh mesh;
  double h = std::sqrt(3.0) / 2.0;
  mesh.vertices = {
    vec2{0.0, 0.0},
    vec2{1.0, 0.0},
    vec2{0.5, h},
    vec2{0.5, -h},
    vec2{-0.5, h},
    vec2{1.5, h}
  };

  mesh.polygons = {
    {0, 1, 2},
    {1, 0, 3},
    {0, 2, 4},
    {2, 1, 5}
  };

  std::optional<Polyhedron> poly = Folding::Fold(mesh);
  CHECK(poly.has_value()) << "Tetrahedron failed to fold.";
  CHECK(poly->faces != nullptr);
  CHECK(poly->vertices.size() == 4);
  CHECK(poly->faces->NumVertices() == 4);
  CHECK(poly->faces->NumFaces() == 4);
  CHECK(poly->faces->NumEdges() == 6);

  std::vector<double> expected(6, 1.0);
  CheckDistances(*poly, expected);
}

static void TestCube() {
  Folding::UnfoldedMesh mesh;
  mesh.vertices = {
    vec2{1.0, 2.0},
    vec2{1.0, 1.0},
    vec2{2.0, 1.0},
    vec2{2.0, 2.0},
    vec2{0.0, 1.0},
    vec2{0.0, 2.0},
    vec2{3.0, 2.0},
    vec2{3.0, 1.0},
    vec2{1.0, 3.0},
    vec2{2.0, 3.0},
    vec2{2.0, 0.0},
    vec2{1.0, 0.0},
    vec2{1.0, -1.0},
    vec2{2.0, -1.0}
  };

  mesh.polygons = {
    {1, 2, 3, 0},
    {0, 3, 9, 8},
    {1, 0, 5, 4},
    {3, 2, 7, 6},
    {2, 1, 11, 10},
    {10, 11, 12, 13}
  };

  std::optional<Polyhedron> poly = Folding::Fold(mesh);
  CHECK(poly.has_value()) << "Cube failed to fold.";
  CHECK(poly->faces != nullptr);
  CHECK(poly->vertices.size() == 8);
  CHECK(poly->faces->NumVertices() == 8);
  CHECK(poly->faces->NumFaces() == 6);
  CHECK(poly->faces->NumEdges() == 12);

  std::vector<double> expected;
  expected.reserve(28);
  for (int i = 0; i < 12; i++) expected.push_back(1.0);
  for (int i = 0; i < 12; i++) expected.push_back(std::sqrt(2.0));
  for (int i = 0; i < 4; i++) expected.push_back(std::sqrt(3.0));
  CheckDistances(*poly, expected);
}

static void TestRoundTrip() {

  // ok!
  CheckOnePoly(Icosahedron(), "icos");
  CheckOnePoly(Dodecahedron(), "dodec");
  CheckOnePoly(Cube(), "cube");
  CheckOnePoly(Octahedron(), "octahedron");

  CheckOnePoly(TruncatedCube(), "truncatedcube");
  CheckOnePoly(TruncatedTetrahedron(), "truncatedtetrahedron");
  CheckOnePoly(Cuboctahedron(), "cuboctahedron");
  CheckOnePoly(TruncatedOctahedron(), "truncatedoctahedron");
  CheckOnePoly(Rhombicuboctahedron(), "rhombicuboctahedron");
  CheckOnePoly(TruncatedCuboctahedron(), "truncatedcuboctahedron");
  CheckOnePoly(SnubCube(), "snubcube");
  CheckOnePoly(Icosidodecahedron(), "icosidodecahedron");
  CheckOnePoly(TruncatedDodecahedron(), "truncateddodecahedron");
  CheckOnePoly(TruncatedIcosahedron(), "truncatedicosahedron");

  CheckOnePoly(TriakisTetrahedron(), "triakistetrahedron");
  CheckOnePoly(RhombicDodecahedron(), "rhombicdodecahedron");
  CheckOnePoly(TriakisOctahedron(), "triakisoctahedron");
  CheckOnePoly(TetrakisHexahedron(), "tetrakishexahedron");
  CheckOnePoly(DeltoidalIcositetrahedron(), "deltoidalicositetrahedron");
  CheckOnePoly(DisdyakisDodecahedron(), "disdyakisdodecahedron");
  CheckOnePoly(PentagonalIcositetrahedron(), "pentagonalicositetrahedron");
  CheckOnePoly(RhombicTriacontahedron(), "rhombictriacontahedron");
  CheckOnePoly(TriakisIcosahedron(), "triakisicosahedron");
  CheckOnePoly(PentakisDodecahedron(), "pentakisdodecahedron");
  CheckOnePoly(PentagonalHexecontahedron(), "pentagonalhexecontahedron");
};

int main(int argc, char **argv) {
  ANSI::Init();

  status = new StatusBar(1);

  TestTetrahedron();
  TestCube();

  TestRoundTrip();

  Print("OK\n");
  return 0;
}

