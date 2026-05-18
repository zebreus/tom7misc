
#include "construct.h"

#include <algorithm>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ansi.h"
#include "arcfour.h"
#include "base/print.h"
#include "base/stringprintf.h"
#include "geom/mesh.h"
#include "geom/polyhedra.h"
#include "periodically.h"
#include "randutil.h"
#include "status-bar.h"
#include "yocto-math.h"

static void TestInit() {
  ArcFour rc("init");

  for (int i = 0; i < 10; i++) {
    PartialPolyhedron pp(&rc, 5 + i * 5, 100);
    pp.CheckValidity();
  }
}

static std::string DebugPoly(std::span<const vec2> poly) {
  std::string ret = "[";
  for (const auto &v : poly) {
    AppendFormat(&ret, "({:.17g}, {:.17g}), ", v.x, v.y);
  }
  ret.push_back(']');
  return ret;
}

static void TestAddFace() {
  ArcFour rc("add-face");
  StatusBar status(1);
  Periodically status_per(1);
  for (int i = 0; i < 1000; i++) {
    PartialPolyhedron pp(&rc, 5 + i, 100);

    for (int f = 0; f < 3; f++) {
      // Print("Iter {}.{}:\n", i, f);
      status_per.RunIf([&]{
          status.Progress(i, 1000, "AddFace");
        });

      std::vector<int> b_edges = pp.GetBoundaryEdges();
      CHECK(b_edges.size() >= 3) << "Must have proper boundary.";

      // Pick an edge uniformly at random.
      int edge_idx = b_edges[RandTo(&rc, (int)b_edges.size())];

      auto DebugIter = [&]{
          return std::format("Iter #{}.#{}. Chose edge {}.\n",
                             i, f, edge_idx);
        };

      auto DebugDump = [&]{
          Mesh3D mesh;
          std::vector<uint32_t> vertex_colors;

          // Convert pp to a mesh (see geom/mesh.h)
          mesh.vertices.reserve(pp.NumVertices());
          for (int v = 0; v < pp.NumVertices(); ++v) {
            mesh.vertices.push_back(pp.GetVertex(v).pos);
          }

          mesh.faces.reserve(pp.NumFaces());
          for (int f_idx = 0; f_idx < pp.NumFaces(); ++f_idx) {
            mesh.faces.push_back(pp.GetFace(f_idx).vertices);
          }

          // Highlight the indicated edge by coloring its endpoints
          // red (all other vertices black).
          vertex_colors.assign(pp.NumVertices(), 0x000000FF);
          const MeshEdge &e = pp.GetEdge(edge_idx);
          vertex_colors[e.v0] = 0xFF0000FF;
          vertex_colors[e.v1] = 0xFF0000FF;

          SaveAsOBJ(mesh, {vertex_colors}, "construct_test.obj");
          return std::format("PP: {}\n", pp.DebugString());
        };

      // Compute and sample a feasible angle.
      const auto &[min_angle, max_angle] = pp.ComputeFeasibleAngles(edge_idx);

      double subtended = max_angle - min_angle;

      #if 1
      // If there's not enough angular volume, skip this edge.
      if (subtended <= 1.0e-3)
        continue;
      #endif

      static constexpr double ANGLE_MARGIN = 0.1;

      double angle_frac = ANGLE_MARGIN +
        (1.0 - (ANGLE_MARGIN * 2.0)) * RandDouble(&rc);
      double angle = min_angle + angle_frac * subtended;

      auto DebugAngles = [&]() {
          return std::format("Min θ: {:.17g}\n"
                             "Max θ: {:.17g}\n"
                             "Subtended: {:.17g}\n"
                             "Chosen θ: {:.17g}\n",
                             min_angle, max_angle, subtended, angle);
        };

      CHECK(subtended > 1.0e-5) <<
        DebugIter() <<
        DebugDump() <<
        DebugAngles();

      // Prevent the new face from becoming wildly larger than the overall
      // partial polyhedron. We compute the current bounding box diameter.
      auto [aabb_min, aabb_max] = pp.AABB();
      const double diameter = yocto::length(aabb_max - aabb_min);


      // Get the feasible region for the new face.
      std::vector<vec2> poly = pp.ComputeFeasibleRegion(edge_idx, angle);
      CHECK(poly.size() >= 3) << "Feasible region must be a valid polygon";

      // Reconstruct the local coordinate frame to convert 2D to 3D.
      const MeshEdge &e = pp.GetEdge(edge_idx);
      vec3 p0 = pp.GetVertex(e.v0).pos;
      vec3 p1 = pp.GetVertex(e.v1).pos;

      const MeshFace &f_left = pp.GetFace(e.left_face);
      vec3 normal_left = f_left.plane.normal;

      FaceChooser chooser(poly, p0, p1, normal_left, angle, diameter);

      std::vector<vec2> new_poly =
        chooser.Triangular2DFace(RandDouble(&rc), RandDouble(&rc));

      CHECK(new_poly.size() >= 3) << "Face must have at least 3 vertices.";

      // The first two vertices must match the 3D edge exactly.
      CHECK(yocto::length(new_poly[0]) < 1e-4)
          << "First vertex must be origin.";
      vec2 expected_v1 = {chooser.edge_len, 0.0};
      CHECK(yocto::length(new_poly[1] - expected_v1) < 1e-4)
          << "Second vertex must be on the x-axis at edge_len.";

      // Face must be convex and have the right winding order (Cartesian CCW).
      CHECK(IsConvexAndScreenClockwise(new_poly))
          << "Face must be convex and have correct winding order.";

      // All points must be within the feasible polygon.
      for (int j = 0; j < (int)new_poly.size(); ++j) {
        bool is_inside = PointInPolygon(new_poly[j], poly) ||
          SquaredDistanceToPoly(poly, new_poly[j]) < 1e-6;
        CHECK(is_inside) <<
          DebugIter() <<
          DebugDump() <<
          DebugAngles() <<
          "\nFeasible Poly: " << DebugPoly(poly) <<
          "\nNew poly: " << DebugPoly(new_poly) <<
          "\nGenerated point " << j << " not in feasible region.";
      }

      std::vector<vec3> new_face_pts =
        chooser.ConvertTo3D(new_poly);

      const char *problem = pp.FeasibilityProblem(edge_idx, new_face_pts);
      CHECK(problem == nullptr) << "Generated face must be feasible. "
        "Problem: " << problem;

      // Add the face and validate the partial polyhedron.
      pp.AddFace(edge_idx, new_face_pts);
      pp.CheckValidity();
    }
  }

  Print("AddFace OK\n");
}

static void TestReplenish(bool leaf_ih) {
  ArcFour rc(leaf_ih ? "replenish-any" : "replenish-leaf_ih");
  int skipped = 0;
  StatusBar status(1);
  Periodically status_per(1);

  int invalid_faces = 0;
  for (int i = 0; i < 100; i++) {
    PartialPolyhedron pp(&rc, 5 + i, 100);

    status_per.RunIf([&]{
        status.Progress(i, 100, "Replenish ({})",
                        leaf_ih ? "leaf ih" : "any");
      });

    while (pp.NumFaces() < 12) {

      // Once we have a few faces, set a constraint.
      if (leaf_ih && pp.NumFaces() == 5 &&
          // (might hit this twice if the next face is invalid)
          !pp.GetLeafConstraint().has_value()) {
        const auto &unfoldings = pp.GetUnfoldings();
        CHECK(!unfoldings.empty());
        const Unfolding &unf = unfoldings[RandTo(&rc, unfoldings.size())];
        std::vector<std::pair<int, int>> leaves;
        for (int face_idx = 0; face_idx < pp.NumFaces(); ++face_idx) {
          int connected_edges = 0;
          int connected_edge = -1;
          for (int e : pp.GetFace(face_idx).edges) {
            if (std::find(unf.tree_edges.begin(), unf.tree_edges.end(), e) !=
                unf.tree_edges.end()) {
              connected_edges++;
              connected_edge = e;
            }
          }
          if (connected_edges == 1) {
            leaves.push_back({face_idx, connected_edge});
          }
        }
        CHECK(!leaves.empty());
        auto [face_idx, constr_edge_idx] = leaves[RandTo(&rc, leaves.size())];
        pp.SetLeafConstraint(face_idx, constr_edge_idx);
      }

      std::vector<int> b_edges = pp.GetBoundaryEdges();

      if (leaf_ih) {
        // Currently, AddFace always adds a face as a leaf. So we cannot
        // extend along boundary edges that are forbidden by the leaf
        // constraint, as we would then be unable to make the target
        // face be a leaf.
        std::vector<int> filtered;
        auto lc = pp.GetLeafConstraint();
        for (int e : b_edges) {
          if (!lc.has_value() || pp.GetEdge(e).left_face != lc.value().first) {
            filtered.push_back(e);
          }
        }
        if (!filtered.empty()) {
          b_edges = filtered;
        }

        CHECK(b_edges.size() >= 1) << "There must be at least one edge "
          "that CAN be expanded, right?";
      } else {
        CHECK(b_edges.size() >= 3) << "Must have proper boundary.";
      }

      // Pick an edge uniformly at random.
      int edge_idx = b_edges[RandTo(&rc, (int)b_edges.size())];

      auto DebugIter = [&]{
          return std::format("Iter #{}.#{}. Chose edge {}.\n",
                             i, pp.NumFaces(), edge_idx);
        };

      auto DebugDump = [&]{
          Mesh3D mesh;
          std::vector<uint32_t> vertex_colors;

          // Convert pp to a mesh (see geom/mesh.h)
          mesh.vertices.reserve(pp.NumVertices());
          for (int v = 0; v < pp.NumVertices(); ++v) {
            mesh.vertices.push_back(pp.GetVertex(v).pos);
          }

          mesh.faces.reserve(pp.NumFaces());
          for (int f_idx = 0; f_idx < pp.NumFaces(); ++f_idx) {
            mesh.faces.push_back(pp.GetFace(f_idx).vertices);
          }

          // Highlight the indicated edge by coloring its endpoints
          // red (all other vertices black).
          vertex_colors.assign(pp.NumVertices(), 0x000000FF);
          const MeshEdge &e = pp.GetEdge(edge_idx);
          vertex_colors[e.v0] = 0xFF0000FF;
          vertex_colors[e.v1] = 0xFF0000FF;

          SaveAsOBJ(mesh, {vertex_colors}, "construct_test.obj");
          return std::format("PP: {}\n", pp.DebugString());
        };

      // Compute and sample a feasible angle.
      const auto &[min_angle, max_angle] = pp.ComputeFeasibleAngles(edge_idx);

      double subtended = max_angle - min_angle;

      #if 0
      // If there's not enough angular volume, skip this edge.
      if (subtended <= 1.0e-3)
        continue;
      #endif

      static constexpr double ANGLE_MARGIN = 0.1;

      double angle_frac = ANGLE_MARGIN +
        (1.0 - (ANGLE_MARGIN * 2.0)) * RandDouble(&rc);
      double angle = min_angle + angle_frac * subtended;

      // If there's not enough angular volume, skip this edge.
      if (subtended <= 1.0e-3) {
        skipped++;
        continue;
      }


      auto DebugAngles = [&]() {
          return std::format("Min θ: {:.17g}\n"
                             "Max θ: {:.17g}\n"
                             "Subtended: {:.17g}\n"
                             "Chosen θ: {:.17g}\n",
                             min_angle, max_angle, subtended, angle);
        };

      CHECK(subtended > 1.0e-5) <<
        DebugIter() <<
        DebugDump() <<
        DebugAngles();

      // Prevent the new face from becoming wildly larger than the overall
      // partial polyhedron. We compute the current bounding box diameter.
      auto [aabb_min, aabb_max] = pp.AABB();
      const double diameter = yocto::length(aabb_max - aabb_min);

      // Print("ComputeFeasible {}.{}\n", i, f);

      // Get the feasible region for the new face.
      std::vector<vec2> poly = pp.ComputeFeasibleRegion(edge_idx, angle);
      CHECK(poly.size() >= 3) << "Feasible region must be a valid polygon";

      // Reconstruct the local coordinate frame to convert 2D to 3D.
      const MeshEdge &e = pp.GetEdge(edge_idx);
      vec3 p0 = pp.GetVertex(e.v0).pos;
      vec3 p1 = pp.GetVertex(e.v1).pos;

      const MeshFace &f_left = pp.GetFace(e.left_face);
      vec3 normal_left = f_left.plane.normal;

      FaceChooser chooser(poly, p0, p1, normal_left, angle, diameter);

      std::vector<vec2> new_poly =
        chooser.Triangular2DFace(RandDouble(&rc), RandDouble(&rc));

      std::vector<vec3> new_face_pts =
        chooser.ConvertTo3D(new_poly);

      const char *problem = pp.FeasibilityProblem(edge_idx, new_face_pts);

      if (problem != nullptr) {
        status.Print("Invalid face: {}\n", problem);
        invalid_faces++;
        CHECK(invalid_faces < 10) << "Too many invalid faces!";
      } else {
        pp.AddFace(edge_idx, new_face_pts);
        pp.ReplenishUnfoldings();
        pp.CheckValidity();
      }
    }
  }

  Print("Replenish[{}] OK (skipped {})\n",
        leaf_ih ? "any" : "leaf_ih",
        skipped);
}

int main(int argc, char **argv) {
  ANSI::Init();

  TestInit();
  TestAddFace();

  TestReplenish(false);
  TestReplenish(true);

  Print("OK\n");
  return 0;
}
