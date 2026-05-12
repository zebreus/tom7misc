
#include "solve-strong.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string_view>

#include "albrecht.h"
#include "ansi.h"
#include "base/print.h"
#include "bit-string.h"
#include "geom/polyhedra.h"
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
        SolveStrong::FindStrongUnfolding(aug, f, e);

      // If we don't get a result, just abort so we can investigate!
      if (!res.has_value()) {
        LOG(FATAL) << "No solution found for " << name << " with "
                   << " face = " << f << " and edge = " << e;
      }

      // Check that the net does indeed have the described property.
      // It should be a valid net...
      Albrecht::DebugResult debug = Albrecht::DebugUnfolding(aug, *res);
      CHECK(debug.is_net) << "Resulting unfolding is not a valid net!";

      // ...and the indicated edge should be on the convex hull.
      bool on_hull = false;
      const auto &pf = debug.mesh.polygons[f];
      for (size_t i = 0; i < pf.v.size(); i++) {
        if (aug.face_edges[f][i] == e) {
          vec2 p0 = debug.mesh.vertices[pf.v[i]];
          vec2 p1 = debug.mesh.vertices[pf.v[(i + 1) % pf.v.size()]];

          vec2 edge_dir = {p1.x - p0.x, p1.y - p0.y};
          double len = std::sqrt(edge_dir.x * edge_dir.x +
                                 edge_dir.y * edge_dir.y);
          if (len < 1e-9) continue;
          edge_dir.x /= len;
          edge_dir.y /= len;

          double min_cross = 0.0;
          double max_cross = 0.0;
          for (size_t other_f = 0;
               other_f < debug.mesh.polygons.size();
               other_f++) {
            if (other_f != static_cast<size_t>(f)) {
              for (int v_idx : debug.mesh.polygons[other_f].v) {
                const vec2 &pt = debug.mesh.vertices[v_idx];
                double cross = edge_dir.x * (pt.y - p0.y) -
                  edge_dir.y * (pt.x - p0.x);
                min_cross = std::min(min_cross, cross);
                max_cross = std::max(max_cross, cross);
              }
            }
          }

          // If all points are on one side (or exactly on the
          // line), the line is a supporting line and the edge
          // sits on the convex hull.
          if (min_cross >= -1e-6 || max_cross <= 1e-6) {
            on_hull = true;
            break;
          }
        }
      }

      if (on_hull) {
        status.Print("{} f={} e={} ok\n", name, f, e);
      }
      CHECK(on_hull) << "Edge " << e << " is not on the convex hull for "
                     << name;

    }
  }

  status.Remove();
  Print("{} ok\n", name);
}

static void FindAndCheckAll() {
  CheckOnePoly(Icosahedron(), "icos");
  CheckOnePoly(Dodecahedron(), "dodec");
  CheckOnePoly(Cube(), "cube");
  CheckOnePoly(Octahedron(), "octahedron");

  // These might not pass?
  CheckOnePoly(TruncatedTetrahedron(), "truncatedtetrahedron");
  CheckOnePoly(Cuboctahedron(), "cuboctahedron");

  CheckOnePoly(TruncatedOctahedron(), "truncatedoctahedron");
  CheckOnePoly(Rhombicuboctahedron(), "rhombicuboctahedron");
  CheckOnePoly(TruncatedCuboctahedron(), "truncatedcuboctahedron");
  CheckOnePoly(SnubCube(), "snubcube");
  CheckOnePoly(Icosidodecahedron(), "icosidodecahedron");
  CheckOnePoly(TruncatedDodecahedron(), "truncateddodecahedron");
  CheckOnePoly(TruncatedIcosahedron(), "truncatedicosahedron");
  CheckOnePoly(Rhombicosidodecahedron(), "rhombicosidodecahedron");
  CheckOnePoly(TruncatedIcosidodecahedron(), "truncatedicosidodecahedron");
  CheckOnePoly(SnubDodecahedron(), "snubdodecahedron");

  CheckOnePoly(TriakisTetrahedron(), "triakistetrahedron");
  CheckOnePoly(RhombicDodecahedron(), "rhombicdodecahedron");
  CheckOnePoly(TriakisOctahedron(), "triakisoctahedron");
  CheckOnePoly(TetrakisHexahedron(), "tetrakishexahedron");
  CheckOnePoly(DeltoidalIcositetrahedron(), "deltoidalicositetrahedron");
  CheckOnePoly(DisdyakisDodecahedron(), "disdyakisdodecahedron");
  CheckOnePoly(DeltoidalHexecontahedron(), "deltoidalhexecontahedron");
  CheckOnePoly(PentagonalIcositetrahedron(), "pentagonalicositetrahedron");
  CheckOnePoly(RhombicTriacontahedron(), "rhombictriacontahedron");
  CheckOnePoly(TriakisIcosahedron(), "triakisicosahedron");
  CheckOnePoly(PentakisDodecahedron(), "pentakisdodecahedron");
  CheckOnePoly(DisdyakisTriacontahedron(), "disdyakistriacontahedron");
  CheckOnePoly(PentagonalHexecontahedron(), "pentagonalhexecontahedron");

  CheckOnePoly(Noperthedron(), "nope");
  CheckOnePoly(Onperthedron(), "onpe");


  // Impossible!
  // The triangle edges cannot be on the hull!
  CheckOnePoly(TruncatedCube(), "truncatedcube");
}


int main(int argc, char **argv) {
  ANSI::Init();

  FindAndCheckAll();

  Print("OK");
  return 0;
}
