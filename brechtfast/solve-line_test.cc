
#include "solve-line.h"

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
#include "status-bar.h"
#include "timer.h"

static StatusBar *status = nullptr;

static void TestSampleLine(const Albrecht::AugmentedPoly &aug,
                           std::string_view name) {
  ArcFour rc{name};
  std::optional<BitString> res = SolveLine::SampleLine(&rc, aug);

  if (res.has_value()) {
    Albrecht::DebugResult debug = Albrecht::DebugUnfolding(aug, *res);
    CHECK(debug.cycle_free) << "Sampled unfolding has cycles!";
    CHECK(debug.is_connected) << "Sampled unfolding is not connected!";

    const int num_faces = aug.poly.faces->NumFaces();
    for (int f = 0; f < num_faces; f++) {
      int uncut_count = 0;
      for (int edge_idx : aug.face_edges[f]) {
        if ((*res)[edge_idx]) {
          uncut_count++;
        }
      }
      CHECK_LE(uncut_count, 2) << "Face " << f << " has degree > 2 in sample!";
    }
  }
}

static void CheckOnePoly(const Polyhedron &poly, std::string_view name) {
  Timer timer;
  Albrecht::AugmentedPoly aug(poly);

  TestSampleLine(aug, name);

  status->Status("Checking {}", name);

  std::optional<BitString> res = SolveLine::FindLineUnfolding(aug);

  if (res.has_value()) {
    // Check that the net does indeed have the described property.
    // It should be a valid net...
    Albrecht::DebugResult debug = Albrecht::DebugUnfolding(aug, *res);
    CHECK(debug.is_net) << "Resulting unfolding is not a valid net!";

    // And every face should have degree at most 2.
    const int num_faces = aug.poly.faces->NumFaces();
    for (int f = 0; f < num_faces; f++) {
      int uncut_count = 0;
      for (int edge_idx : aug.face_edges[f]) {
        if ((*res)[edge_idx]) {
          uncut_count++;
        }
      }

      CHECK_LE(uncut_count, 2) << "Face " << f << " has degree > 2!";
    }
  }

  status->Print("{} ok ({}) in {}\n", name,
                res.has_value() ? AGREEN("line") :
                APURPLE("none"),
                ANSI::Time(timer.Seconds()));
}

static void FindAndCheckAll() {
  // New ones first..

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

  CheckOnePoly(DeltoidalHexecontahedron(), "deltoidalhexecontahedron");
  CheckOnePoly(DisdyakisTriacontahedron(), "disdyakistriacontahedron");

  // TOO SLOW
  // CheckOnePoly(Rhombicosidodecahedron(), "rhombicosidodecahedron");
  // CheckOnePoly(TruncatedIcosidodecahedron(), "truncatedicosidodecahedron");
  // CheckOnePoly(SnubDodecahedron(), "snubdodecahedron");
  // CheckOnePoly(Noperthedron(), "nope");
  // CheckOnePoly(Onperthedron(), "onpe");

  for (int i = 1; i <= 92; i++) {
    // Many of these can be fully checked in minutes (up to about an
    // hour) but who wants to wait around for that?
    if (i == 38 || i == 40 || i == 41 || i == 42 || i == 43 ||
        i == 46 || i == 47 || i == 48 ||
        i == 68 || i == 69 || i == 70 || i == 71 ||
        i == 73 || i == 74 || i == 75 || i == 79) {
      status->Print("Skip #{} {}\n", i, JohnsonSolidName(i));
      continue;
    }

    CheckOnePoly(JohnsonSolid(i),
                 std::format("#{} {}", i, JohnsonSolidName(i)));
  }

}

int main(int argc, char **argv) {
  ANSI::Init();

  status = new StatusBar(1);
  FindAndCheckAll();

  Print("OK\n");
  return 0;
}

