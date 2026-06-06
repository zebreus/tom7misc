
#include "solve-dual-leaf.h"

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

static StatusBar *status = nullptr;

static void CheckOnePoly(const Polyhedron &poly, std::string_view name) {
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

static constexpr bool VERY_SLOW = false;

static void FindAndCheckAll() {
  StatusBar status(1);

  // This one does succeed for the dual leaf IH, but it takes a
  // long time (lots of faces).
  if (VERY_SLOW) {
    CheckOnePoly(Nasty::DrillBit(), "drillbit");
  }

  CheckOnePoly(Nasty::GrunbaumTetra(), "grunbaumtetra");

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

