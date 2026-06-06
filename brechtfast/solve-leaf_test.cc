
#include "solve-leaf.h"

#include <cmath>
#include <format>
#include <optional>
#include <string_view>

#include "albrecht.h"
#include "ansi.h"
#include "arcfour.h"
#include "base/print.h"
#include "bit-string.h"
#include "geom/johnson-solids.h"
#include "geom/polyhedra.h"
#include "nasty.h"
#include "periodically.h"
#include "status-bar.h"

static void TestSampleFace(const Albrecht::AugmentedPoly &aug,
                           std::string_view name) {
  ArcFour rc{name};
  const int num_faces = aug.poly.faces->NumFaces();

  for (int i = 0; i < 100; i++) {
    int f = i % num_faces;
    BitString res = SolveLeaf::SampleFace(&rc, aug, f);

    Albrecht::DebugResult debug = Albrecht::DebugUnfolding(aug, res);
    CHECK(debug.cycle_free) << "Sampled unfolding has cycles!";
    CHECK(debug.is_connected) << "Sampled unfolding is not connected!";

    int uncut_count = 0;
    for (int edge_idx : aug.face_edges[f]) {
      if (res[edge_idx]) {
        uncut_count++;
      }
    }

    CHECK_EQ(uncut_count, 1) << "Face " << f << " is not a leaf in sample!";
  }
}

static StatusBar *status = nullptr;

static void CheckOnePoly(const Polyhedron &poly, std::string_view name) {
  Albrecht::AugmentedPoly aug(poly);

  TestSampleFace(aug, name);

  Periodically status_per(1);

  [[maybe_unused]] const int num_edges = poly.faces->NumEdges();
  [[maybe_unused]] const int num_faces = poly.faces->NumFaces();

  // Loop over all edges and run the leaf solver.
  for (int e = 0; e < num_edges; e++) {
    const Faces::Edge &edge = poly.faces->edges[e];
    status_per.RunIf([&]{
        status->Progress(e, num_edges, "Checking {}", name);
      });

    for (int f : {edge.f0, edge.f1}) {

      std::optional<BitString> res =
        SolveLeaf::FindLeafUnfolding(aug, f, e);

      // If we don't get a result, just abort so we can investigate!
      if (!res.has_value()) {
        LOG(FATAL) << "No solution found for " << name << " with "
                   << " face = " << f << " and edge = " << e;
      }

      // Check that the net does indeed have the described property.
      // It should be a valid net...
      Albrecht::DebugResult debug = Albrecht::DebugUnfolding(aug, *res);
      CHECK(debug.is_net) << "Resulting unfolding is not a valid net!";

      // And the face should be a leaf, with the edge not cut.

      int uncut_count = 0;
      for (int edge_idx : aug.face_edges[f]) {
        if ((*res)[edge_idx]) {
          uncut_count++;
        }
      }

      CHECK(res.value()[e]) << "Edge " << e << " is cut in the unfolding!";
      CHECK_EQ(uncut_count, 1) << "Face " << f << " is not a leaf!";
    }
  }

  status->Print("{} ok\n", name);
}

static constexpr bool VERY_SLOW = false;

static void FindAndCheckAll() {

  // This one does succeed for the leaf IH, but it takes a long time
  // (lots of faces).
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
  CheckOnePoly(DeltoidalIcositetrahedron(), "deltoidalicositetrahedron");
  CheckOnePoly(DisdyakisDodecahedron(), "disdyakisdodecahedron");
  CheckOnePoly(PentagonalIcositetrahedron(), "pentagonalicositetrahedron");
  CheckOnePoly(RhombicTriacontahedron(), "rhombictriacontahedron");
  CheckOnePoly(TriakisIcosahedron(), "triakisicosahedron");
  CheckOnePoly(PentakisDodecahedron(), "pentakisdodecahedron");
  CheckOnePoly(PentagonalHexecontahedron(), "pentagonalhexecontahedron");

  // Big, slow
  CheckOnePoly(Rhombicosidodecahedron(), "rhombicosidodecahedron");
  CheckOnePoly(TruncatedIcosidodecahedron(), "truncatedicosidodecahedron");
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

  Print("OK");
  return 0;
}
