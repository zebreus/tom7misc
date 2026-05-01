
#include "base/stringprintf.h"
#include "construct.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ansi.h"
#include "arcfour.h"
#include "base/print.h"
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

      #if 0
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
        chooser.Generate2DFace(RandDouble(&rc), RandDouble(&rc));

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

      // Diagnostic assertions to isolate IsFeasible failure.
      if (!pp.IsFeasible(edge_idx, new_face_pts)) {
        bool found_edge = false;
        for (int i = 0; i < (int)new_face_pts.size(); ++i) {
          vec3 v_curr = new_face_pts[i];
          vec3 v_next = new_face_pts[(i + 1) % new_face_pts.size()];
          if (yocto::length(v_curr - p1) < 1e-4 &&
              yocto::length(v_next - p0) < 1e-4) {
            found_edge = true;
            break;
          }
        }
        CHECK(found_edge) << "Winding order/edge not preserved.";

        CHECK(PlanarityError(new_face_pts) <= 1e-4)
            << "Planarity error exceeded.";

        vec3 normal = {0.0, 0.0, 0.0};
        for (int i = 0; i < (int)new_face_pts.size(); ++i) {
          vec3 p_curr = new_face_pts[i];
          vec3 p_next = new_face_pts[(i + 1) % new_face_pts.size()];
          normal += yocto::cross(p_curr, p_next);
        }
        double len_n = yocto::length(normal);
        CHECK(len_n >= 1e-5) << "Normal length too small: " << len_n;
        normal /= len_n;

        for (int j = 0; j < (int)new_face_pts.size(); ++j) {
          vec3 p_prev = new_face_pts[j];
          vec3 p_curr = new_face_pts[(j + 1) % new_face_pts.size()];
          vec3 p_next = new_face_pts[(j + 2) % new_face_pts.size()];
          vec3 e1 = p_curr - p_prev;
          vec3 e2 = p_next - p_curr;
          vec3 cross = yocto::cross(e1, e2);
          CHECK(yocto::dot(cross, normal) > 1e-5)
              << "Strict convexity failed at vertex " << j;
        }

        for (const vec3 &p : new_face_pts) {
          for (int k = 0; k < pp.NumFaces(); ++k) {
            const HalfSpace &hs = pp.GetFace(k).plane;
            double d_hs = yocto::dot(hs.normal, p) - hs.d;
            CHECK(d_hs <= 1e-4)
                << "Point violates face " << k << " by " << d_hs;
          }
        }

        HalfSpace new_hs;
        new_hs.normal = normal;
        new_hs.d = yocto::dot(normal, new_face_pts[0]);
        for (int k = 0; k < pp.NumVertices(); ++k) {
          double d_v = yocto::dot(new_hs.normal, pp.GetVertex(k).pos) -
            new_hs.d;
          CHECK(d_v <= 1e-4)
              << "Vertex " << k << " excluded by " << d_v;
        }

        double dot_n = yocto::dot(f_left.plane.normal, normal);
        CHECK(dot_n > -1.0 + 1e-5 && dot_n < 1.0 - 1e-5)
          << "On iteration #" << i << "." << f << "\n"
          << DebugAngles()
          << "State:\n" << pp.DebugString()
          << "\n"
          << "Dihedral dot product out of bounds: " << dot_n;

        int test_v_idx = -1;
        for (int i = 0; i < (int)new_face_pts.size(); ++i) {
          if (yocto::length(new_face_pts[i] - p0) > 1e-4 &&
              yocto::length(new_face_pts[i] - p1) > 1e-4) {
            test_v_idx = i;
            break;
          }
        }
        CHECK(test_v_idx != -1) << "No test vertex for "
          "dihedral check.";
        double dist = yocto::dot(f_left.plane.normal,
                                 new_face_pts[test_v_idx]) - f_left.plane.d;
        CHECK(dist < -1e-5)
            << "Dihedral convexity failed. dist=" << dist
            << " angle=" << angle;

        LOG(FATAL) << "IsFeasible failed for unknown reason!";
      }


      // Sanity check before adding.
      CHECK(pp.IsFeasible(edge_idx, new_face_pts))
          << "Generated face must be feasible";

      // Add the face and validate the partial polyhedron.
      pp.AddFace(edge_idx, new_face_pts);
      pp.CheckValidity();
    }
  }

  Print("AddFace OK\n");
}

static void TestReplenish() {
  ArcFour rc("replenish");
  int skipped = 0;
  StatusBar status(1);
  Periodically status_per(1);
  for (int i = 0; i < 100; i++) {
    PartialPolyhedron pp(&rc, 5 + i, 100);

    status_per.RunIf([&]{
        status.Progress(i, 100, "Replenish");
      });

    for (int f = 0; f < 10; f++) {
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
        chooser.Generate2DFace(RandDouble(&rc), RandDouble(&rc));

      std::vector<vec3> new_face_pts =
        chooser.ConvertTo3D(new_poly);

      // Sanity check before adding.
      CHECK(pp.IsFeasible(edge_idx, new_face_pts))
          << "Generated face must be feasible";

      pp.AddFace(edge_idx, new_face_pts);
      pp.ReplenishUnfoldings();
      pp.CheckValidity();
    }
  }

  Print("Replenish OK (skipped {})\n", skipped);
}

int main(int argc, char **argv) {
  ANSI::Init();

  TestInit();
  TestAddFace();

  TestReplenish();

  Print("OK\n");
  return 0;
}
