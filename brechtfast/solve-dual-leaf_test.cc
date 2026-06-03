
#include "solve-dual-leaf.h"

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

static void TestSampleDualLeaf(const Albrecht::AugmentedPoly &aug,
                               std::string_view name) {
  ArcFour rc{name};
  const int num_edges = aug.poly.faces->NumEdges();

  for (int i = 0; i < 100; i++) {
    int e = i % num_edges;
    BitString res = SolveDualLeaf::SampleDualLeaf(&rc, aug, e);

    Albrecht::DebugResult debug = Albrecht::DebugUnfolding(aug, res);
    CHECK(debug.cycle_free) << "Sampled unfolding has cycles!";
    CHECK(debug.is_connected) << "Sampled unfolding is not connected!";

    CHECK(!res[e]) << "Edge " << e << " is not cut in sample!";

    int f0 = aug.poly.faces->edges[e].f0;
    int f1 = aug.poly.faces->edges[e].f1;

    int f0_uncut = 0;
    for (int edge_idx : aug.face_edges[f0]) {
      if (res[edge_idx]) {
        f0_uncut++;
      }
    }
    int f1_uncut = 0;
    for (int edge_idx : aug.face_edges[f1]) {
      if (res[edge_idx]) {
        f1_uncut++;
      }
    }

    CHECK_EQ(f0_uncut, 1) << "Face " << f0 << " is not a leaf in sample!";
    CHECK_EQ(f1_uncut, 1) << "Face " << f1 << " is not a leaf in sample!";
  }
}

static void CheckOnePoly(StatusBar *status,
                         const Polyhedron &poly, std::string_view name) {
  Timer timer;
  Albrecht::AugmentedPoly aug(poly);

  Periodically status_per(1);

  status->Status("Sample {}", poly.name);
  TestSampleDualLeaf(aug, name);

  const int num_edges = poly.faces->NumEdges();

  // Loop over all edges and run the dual-leaf solver.
  for (int e = 0; e < num_edges; e++) {
    status_per.RunIf([&]{
        status->Progress(e, num_edges, "Checking {}", name);
      });

    std::optional<BitString> res = SolveDualLeaf::FindDualLeafUnfolding(aug, e);

    // If we don't get a result, just abort so we can investigate!
    if (!res.has_value()) {
      LOG(FATAL) << "No solution found for " << name << " with edge = " << e;
    }

    // Check that the net does indeed have the described property.
    // It should be a valid net...
    Albrecht::DebugResult debug = Albrecht::DebugUnfolding(aug, *res);
    CHECK(debug.is_net) << "Resulting unfolding is not a valid net!";

    // And the faces should be leaves, with the edge cut.
    CHECK(!res.value()[e]) << "Edge " << e << " is not cut in the unfolding!";

    int f0 = poly.faces->edges[e].f0;
    int f1 = poly.faces->edges[e].f1;

    int f0_uncut = 0;
    for (int edge_idx : aug.face_edges[f0]) {
      if ((*res)[edge_idx]) {
        f0_uncut++;
      }
    }

    int f1_uncut = 0;
    for (int edge_idx : aug.face_edges[f1]) {
      if ((*res)[edge_idx]) {
        f1_uncut++;
      }
    }

    CHECK_EQ(f0_uncut, 1) << "Face " << f0 << " is not a leaf!";
    CHECK_EQ(f1_uncut, 1) << "Face " << f1 << " is not a leaf!";
  }

  status->Print("{} ok in {}\n", name, ANSI::Time(timer.Seconds()));
}

static void FindAndCheckAll() {
  StatusBar status(1);

  CheckOnePoly(&status, Nasty::TiltedDecagonPyramid(), "tilteddecagonpyramid");
  CheckOnePoly(&status, Nasty::SquatSnail(), "squatsnail");
  CheckOnePoly(&status, Nasty::FlattenedIcosahedron(), "flattenedicosahedron");
  CheckOnePoly(&status, Nasty::LongTaperedPrism(), "longtaperedprism");
  CheckOnePoly(&status, Nasty::LongTaperedAntiprism(), "longtaperedantiprism");
  // too big!
  // CheckOnePoly(&status, Nasty::Lens(), "lens");
  CheckOnePoly(&status, Nasty::LowPolyLens(), "lowpolylens");
  CheckOnePoly(&status, Nasty::Coin(), "coin");
  CheckOnePoly(&status, Nasty::Sawblade(), "sawblade");
  CheckOnePoly(&status, Nasty::Dome(), "dome");
  CheckOnePoly(&status, Nasty::Chisel(), "chisel");

  CheckOnePoly(&status, Icosahedron(), "icos");
  CheckOnePoly(&status, Dodecahedron(), "dodec");
  CheckOnePoly(&status, Cube(), "cube");
  CheckOnePoly(&status, Octahedron(), "octahedron");

  CheckOnePoly(&status, TruncatedCube(), "truncatedcube");
  CheckOnePoly(&status, TruncatedTetrahedron(), "truncatedtetrahedron");
  CheckOnePoly(&status, Cuboctahedron(), "cuboctahedron");
  CheckOnePoly(&status, TruncatedOctahedron(), "truncatedoctahedron");
  CheckOnePoly(&status, Rhombicuboctahedron(), "rhombicuboctahedron");
  CheckOnePoly(&status, TruncatedCuboctahedron(), "truncatedcuboctahedron");
  CheckOnePoly(&status, SnubCube(), "snubcube");
  CheckOnePoly(&status, Icosidodecahedron(), "icosidodecahedron");
  CheckOnePoly(&status, TruncatedDodecahedron(), "truncateddodecahedron");
  CheckOnePoly(&status, TruncatedIcosahedron(), "truncatedicosahedron");

  CheckOnePoly(&status, TriakisTetrahedron(), "triakistetrahedron");
  CheckOnePoly(&status, RhombicDodecahedron(), "rhombicdodecahedron");
  CheckOnePoly(&status, TriakisOctahedron(), "triakisoctahedron");
  CheckOnePoly(&status, TetrakisHexahedron(), "tetrakishexahedron");
  CheckOnePoly(&status, DeltoidalIcositetrahedron(),
               "deltoidalicositetrahedron");
  CheckOnePoly(&status, DisdyakisDodecahedron(), "disdyakisdodecahedron");
  CheckOnePoly(&status, PentagonalIcositetrahedron(),
               "pentagonalicositetrahedron");
  CheckOnePoly(&status, RhombicTriacontahedron(), "rhombictriacontahedron");
  CheckOnePoly(&status, TriakisIcosahedron(), "triakisicosahedron");
  CheckOnePoly(&status, PentakisDodecahedron(), "pentakisdodecahedron");
  CheckOnePoly(&status, PentagonalHexecontahedron(),
               "pentagonalhexecontahedron");

  // Big, slow
  CheckOnePoly(&status, Rhombicosidodecahedron(), "rhombicosidodecahedron");
  CheckOnePoly(&status, TruncatedIcosidodecahedron(),
               "truncatedicosidodecahedron");
  CheckOnePoly(&status, SnubDodecahedron(), "snubdodecahedron");

  CheckOnePoly(&status, DeltoidalHexecontahedron(), "deltoidalhexecontahedron");
  CheckOnePoly(&status, DisdyakisTriacontahedron(), "disdyakistriacontahedron");

  CheckOnePoly(&status, Noperthedron(), "nope");
  CheckOnePoly(&status, Onperthedron(), "onpe");

  for (int i = 1; i <= 92; i++) {
    CheckOnePoly(&status, JohnsonSolid(i), JohnsonSolidName(i));
  }
}

int main(int argc, char **argv) {
  ANSI::Init();

  FindAndCheckAll();

  Print("OK\n");
  return 0;
}

