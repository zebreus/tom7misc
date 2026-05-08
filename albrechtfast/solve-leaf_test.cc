
#include "solve-leaf.h"

#include <optional>
#include <string_view>

#include "albrecht.h"
#include "ansi.h"
#include "base/print.h"
#include "bit-string.h"
#include "geom/johnson-solids.h"
#include "geom/polyhedra.h"
#include "nasty.h"
#include "periodically.h"
#include "status-bar.h"

static void CheckOnePoly(const Polyhedron &poly, std::string_view name) {
  Albrecht::AugmentedPoly aug(poly);

  StatusBar status(1);
  Periodically status_per(1);

  const int num_edges = poly.faces->NumEdges();

  // Loop over all edges and run the strong solver.
  for (int e = 0; e < num_edges; e++) {
    const Faces::Edge &edge = poly.faces->edges[e];
    status_per.RunIf([&]{
        status.Progress(e, num_edges, "Checking {}", name);
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

  status.Remove();
  Print("{} ok\n", name);
}

static void FindAndCheckAll() {
  // New ones first..

  CheckOnePoly(Nasty::TiltedDecagonPyramid(), "tilteddecagonpyramid");
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

  FindAndCheckAll();

  Print("OK");
  return 0;
}
