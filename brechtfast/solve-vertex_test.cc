
#include "solve-vertex.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <optional>
#include <string_view>

#include "albrecht.h"
#include "ansi.h"
#include "arcfour.h"
#include "base/logging.h"
#include "base/print.h"
#include "bit-string.h"
#include "geom/johnson-solids.h"
#include "geom/polyhedra.h"
#include "nasty.h"
#include "periodically.h"
#include "status-bar.h"
#include "timer.h"

static StatusBar *status = nullptr;

static void TestSampleVertex(const Albrecht::AugmentedPoly &aug,
                             std::string_view name) {
  ArcFour rc{name};
  const int num_edges = aug.poly.faces->NumEdges();
  int num_vertices = 0;
  for (int e = 0; e < num_edges; e++) {
    num_vertices = std::max(num_vertices, aug.poly.faces->edges[e].v0 + 1);
    num_vertices = std::max(num_vertices, aug.poly.faces->edges[e].v1 + 1);
  }

  for (int i = 0; i < 100; i++) {
    int v = i % num_vertices;
    BitString res = SolveVertex::SampleVertex(&rc, aug, v);

    Albrecht::DebugResult debug = Albrecht::DebugUnfolding(aug, res);
    CHECK(debug.cycle_free) << "Sampled unfolding has cycles!";
    CHECK(debug.is_connected) << "Sampled unfolding is not connected!";

    for (int e = 0; e < num_edges; e++) {
      const Faces::Edge &edge = aug.poly.faces->edges[e];
      if (edge.v0 == v || edge.v1 == v) {
        CHECK(!res[e]) << "Edge " << e << " connected to vertex " << v
                       << " is not cut in sample!";
      }
    }
  }
}

static void CheckOnePoly(const Polyhedron &poly, std::string_view name) {
  Timer timer;
  Albrecht::AugmentedPoly aug(poly);

  Periodically status_per(1);

  status->Status("Sample {}", poly.name);
  TestSampleVertex(aug, name);

  const int num_edges = poly.faces->NumEdges();
  int num_vertices = 0;
  for (int e = 0; e < num_edges; e++) {
    num_vertices = std::max(num_vertices, poly.faces->edges[e].v0 + 1);
    num_vertices = std::max(num_vertices, poly.faces->edges[e].v1 + 1);
  }

  // Loop over all vertices and run the vertex solver.
  for (int v = 0; v < num_vertices; v++) {
    status_per.RunIf([&]{
        status->Progress(v, num_vertices, "Checking {}", name);
      });

    std::optional<BitString> res = SolveVertex::FindVertexUnfolding(aug, v);

    // If we don't get a result, just abort so we can investigate!
    if (!res.has_value()) {
      LOG(FATAL) << "No solution found for " << name
                 << " with vertex = " << v;
    }

    // Check that the net does indeed have the described property.
    // It should be a valid net...
    Albrecht::DebugResult debug = Albrecht::DebugUnfolding(aug, *res);
    CHECK(debug.is_net) << "Resulting unfolding is not a valid net!";

    // And all edges connected to the vertex should be cut.
    for (int e = 0; e < num_edges; e++) {
      const Faces::Edge &edge = poly.faces->edges[e];
      if (edge.v0 == v || edge.v1 == v) {
        CHECK(!(*res)[e]) << "Edge " << e << " connected to vertex " << v
                          << " is not cut in the unfolding!";
      }
    }
  }

  status->Print("{} ok in {}\n", name, ANSI::Time(timer.Seconds()));
}

static void FindAndCheckAll() {
  StatusBar status(1);

  CheckOnePoly(Nasty::TiltedDecagonPyramid(), "tilteddecagonpyramid");
  CheckOnePoly(Nasty::SquatSnail(), "squatsnail");
  CheckOnePoly(Nasty::FlattenedIcosahedron(), "flattenedicosahedron");
  CheckOnePoly(Nasty::LongTaperedPrism(), "longtaperedprism");
  CheckOnePoly(Nasty::LongTaperedAntiprism(), "longtaperedantiprism");
  // too big!
  // CheckOnePoly(Nasty::Lens(), "lens");
  CheckOnePoly(Nasty::LowPolyLens(), "lowpolylens");
  CheckOnePoly(Nasty::Coin(), "coin");
  CheckOnePoly(Nasty::Sawblade(), "sawblade");
  CheckOnePoly(Nasty::Dome(), "dome");
  CheckOnePoly(Nasty::Chisel(), "chisel");

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
  CheckOnePoly(DeltoidalIcositetrahedron(),
               "deltoidalicositetrahedron");
  CheckOnePoly(DisdyakisDodecahedron(), "disdyakisdodecahedron");
  CheckOnePoly(PentagonalIcositetrahedron(),
               "pentagonalicositetrahedron");
  CheckOnePoly(RhombicTriacontahedron(), "rhombictriacontahedron");
  CheckOnePoly(TriakisIcosahedron(), "triakisicosahedron");
  CheckOnePoly(PentakisDodecahedron(), "pentakisdodecahedron");
  CheckOnePoly(PentagonalHexecontahedron(),
               "pentagonalhexecontahedron");

  // Big, slow
  CheckOnePoly(Rhombicosidodecahedron(), "rhombicosidodecahedron");
  CheckOnePoly(TruncatedIcosidodecahedron(),
               "truncatedicosidodecahedron");
  CheckOnePoly(SnubDodecahedron(), "snubdodecahedron");

  CheckOnePoly(DeltoidalHexecontahedron(), "deltoidalhexecontahedron");
  CheckOnePoly(DisdyakisTriacontahedron(), "disdyakistriacontahedron");

  CheckOnePoly(Noperthedron(), "nope");
  CheckOnePoly(Onperthedron(), "onpe");

  for (int i = 1; i <= 92; i++) {
    CheckOnePoly(JohnsonSolid(i), JohnsonSolidName(i));
  }
}

int main(int argc, char **argv) {
  ANSI::Init();

  status = new StatusBar(1);

  FindAndCheckAll();

  status->Remove();

  Print("OK\n");
  return 0;
}

