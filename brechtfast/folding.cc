
#include "folding.h"

#include <cmath>
#include <functional>
#include <numbers>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "base/print.h"
#include "geom/polyhedra.h"
#include "opt/opt.h"
#include "union-find.h"
#include "yocto-math.h"

static constexpr bool VERBOSE = true;

// IMPLEMENTATION PLAN:
// Folding an unfolding (even an overlapping one) into a convex polyhedron
// involves three main phases:
//
// 1. Topology & Gluing: Identify how the boundary edges of the unfolded mesh
//    glue together to form a closed 3D surface.
// 2. Optimization Setup: Parameterize the 3D geometry. Since the 2D polygons
//    must remain rigid, we parameterize the 3D shape by the dihedral angles
//    along the internal edges (which form a face-spanning tree).
// 3. Solver: Optimize the dihedral angles to force the glued boundary edges
//    to coincide in 3D, and reconstruct the polyhedron.

// --- Phase 1: Topology & Gluing ---

// Represents an oriented edge in the 2D unfolding.
struct DirectedEdge {
  // Vertex indices in the 2D mesh.
  int v0 = 0, v1 = 0;
  int face = 0;
};

// Represents an internal cut-free edge shared by two polygons in the
// unfolding.
struct InternalEdge {
  // Polygon indices
  int f0 = 0, f1 = 0;
};

// Partitions all edges into boundary edges (appear exactly once) and
// internal edges (shared by exactly two polygons).
static std::pair<std::vector<DirectedEdge>, std::vector<InternalEdge>>
PartitionEdges(const Folding::UnfoldedMesh &umesh) {
  std::vector<DirectedEdge> all_edges;
  int num_faces = (int)umesh.polygons.size();

  // Get all the directed edges.
  for (int f = 0; f < num_faces; f++) {
    const std::vector<int> &poly = umesh.polygons[f];
    int n = static_cast<int>(poly.size());
    for (int i = 0; i < n; i++) {
      all_edges.push_back({poly[i], poly[(i + 1) % n], f});
    }
  }

  std::vector<DirectedEdge> boundary;
  std::vector<InternalEdge> internal_edges;

  // Find edges that appear exactly once (boundary) or twice (internal).
  for (size_t i = 0; i < all_edges.size(); i++) {
    int count = 0;
    size_t other_idx = 0;
    for (size_t j = 0; j < all_edges.size(); j++) {
      bool same_dir = (all_edges[i].v0 == all_edges[j].v0 &&
                       all_edges[i].v1 == all_edges[j].v1);
      bool opp_dir  = (all_edges[i].v0 == all_edges[j].v1 &&
                       all_edges[i].v1 == all_edges[j].v0);
      if (same_dir || opp_dir) {
        count++;
        if (i != j) {
          other_idx = j;
        }
      }
    }

    if (count == 1) {
      boundary.push_back(all_edges[i]);
    } else if (count == 2 && i < other_idx) {
      int f0 = all_edges[i].face;
      int f1 = all_edges[other_idx].face;
      if (f0 != f1) {
        internal_edges.push_back({f0, f1});
      }
    }
  }

  return {boundary, internal_edges};
}

// A gluing map pairing up boundary edges that will be joined in 3D.
struct Gluing {
  std::vector<std::pair<DirectedEdge, DirectedEdge>> pairs;
};

// Finds a valid gluing of the boundary edges.
// For convex polyhedra unfoldings, we can iteratively "zip" adjacent boundary
// edges that meet at a cut vertex and have the same length. Alexandrov's
// theorem requires that the sum of 2D angles at any resulting glued 3D vertex
// is strictly less than 2*pi. Returns nullopt if no valid gluing exists.
std::optional<Gluing> ZipBoundary(const Folding::UnfoldedMesh &umesh,
                                  const std::vector<DirectedEdge> &boundary) {
  if (boundary.empty()) {
    Gluing empty_gluing;
    return {empty_gluing};
  }

  // Chain the boundary edges into a contiguous cycle.
  std::vector<DirectedEdge> cycle;
  std::vector<DirectedEdge> remaining = boundary;
  cycle.push_back(remaining[0]);
  remaining.erase(remaining.begin());

  while (!remaining.empty()) {
    bool found = false;
    for (size_t i = 0; i < remaining.size(); i++) {
      if (remaining[i].v0 == cycle.back().v1) {
        cycle.push_back(remaining[i]);
        remaining.erase(remaining.begin() + i);
        found = true;
        break;
      }
    }
    if (!found) {
      Print("ZipBoundary: Failed to chain boundary edges into a cycle.\n");
      return std::nullopt;
    }
  }

  int num_vertices = (int)umesh.vertices.size();
  std::vector<double> angle_sums(num_vertices, 0.0);

  // Compute the initial 2D interior angle sum at each vertex.
  for (const std::vector<int> &poly : umesh.polygons) {
    int n = static_cast<int>(poly.size());
    for (int i = 0; i < n; i++) {
      int v_prev = poly[(i + n - 1) % n];
      int v_curr = poly[i];
      int v_next = poly[(i + 1) % n];

      auto d1 = normalize(umesh.vertices[v_prev] - umesh.vertices[v_curr]);
      auto d2 = normalize(umesh.vertices[v_next] - umesh.vertices[v_curr]);

      double dot_val = (double)dot(d1, d2);
      if (dot_val < -1.0) dot_val = -1.0;
      if (dot_val > 1.0) dot_val = 1.0;

      angle_sums[v_curr] += std::acos(dot_val);
    }
  }

  UnionFind dsu(num_vertices);
  Gluing result;

  // Iteratively find and zip adjacent boundary edges of equal length.
  // We prioritize zipping edges that meet at a vertex with the largest
  // current angle sum, which avoids greedy premature closure of flat points.
  while (cycle.size() >= 2) {
    int best_i = -1;
    int best_j = -1;
    double max_cut_angle = -1.0;

    for (size_t i = 0; i < cycle.size(); i++) {
      size_t j = (i + 1) % cycle.size();
      const DirectedEdge &e1 = cycle[i];
      const DirectedEdge &e2 = cycle[j];

      // Verify that adjacent edges meet at the same glued vertex.
      CHECK(dsu.Find(e1.v1) == dsu.Find(e2.v0))
          << "Boundary is not a continuous cycle.";

      auto p1_0 = umesh.vertices[e1.v0];
      auto p1_1 = umesh.vertices[e1.v1];
      double l1 = (double)length(p1_0 - p1_1);

      auto p2_0 = umesh.vertices[e2.v0];
      auto p2_1 = umesh.vertices[e2.v1];
      double l2 = (double)length(p2_0 - p2_1);

      double diff = l1 - l2;
      if (diff < 0.0) diff = -diff;

      if (diff < 1e-5) {
        int root1 = dsu.Find(e1.v0);
        int root2 = dsu.Find(e2.v1);
        double merged_angle = (root1 == root2) ?
            angle_sums[root1] : (angle_sums[root1] + angle_sums[root2]);

        double cut_angle = angle_sums[dsu.Find(e1.v1)];

        if (merged_angle < 2.0 * std::numbers::pi + 1e-5 &&
            cut_angle < 2.0 * std::numbers::pi + 1e-5) {
          if (cut_angle > max_cut_angle) {
            max_cut_angle = cut_angle;
            best_i = static_cast<int>(i);
            best_j = static_cast<int>(j);
          }
        }
      }
    }

    if (best_i == -1) break;

    const DirectedEdge &e1 = cycle[best_i];
    const DirectedEdge &e2 = cycle[best_j];
    int root1 = dsu.Find(e1.v0);
    int root2 = dsu.Find(e2.v1);

    result.pairs.push_back({e1, e2});
    if (dsu.Union(e1.v0, e2.v1)) {
      angle_sums[root2] += angle_sums[root1];
      // In case dsu.Union makes root1 the new parent, keep them synced.
      angle_sums[root1] = angle_sums[root2];
    }

    if (best_i < best_j) {
      cycle.erase(cycle.begin() + best_j);
      cycle.erase(cycle.begin() + best_i);
    } else {
      cycle.erase(cycle.begin() + best_i);
      cycle.erase(cycle.begin() + best_j);
    }
  }

  if (!cycle.empty()) {
    Print("ZipBoundary: Boundary cycle not fully zipped. Remaining cycle:\n");
    for (const DirectedEdge &e : cycle) {
      double l = (double)length(umesh.vertices[e.v0] - umesh.vertices[e.v1]);
      Print("  v{} -> v{}, len = {:.6f}\n", e.v0, e.v1, l);
    }
    return std::nullopt;
  }

  return result;
}

// --- Phase 2: Optimization Setup ---

// Computes the 3D positions of all 2D vertices given a set of internal
// dihedral angles. It starts by placing the root face in the XY plane, then
// recursively traverses the spanning tree of faces, rotating each child face
// into 3D using the corresponding dihedral angle.
std::vector<vec3> Compute3DPositions(const Folding::UnfoldedMesh &umesh,
                                     const std::vector<InternalEdge> &tree,
                                     std::span<const double> dihedral_angles) {
  int num_faces = static_cast<int>(umesh.polygons.size());
  int num_vertices = static_cast<int>(umesh.vertices.size());
  if (num_faces == 0) return {};

  // Build adjacency list for the face tree.
  struct EdgeInfo {
    int nxt_face;
    int edge_idx;
  };
  std::vector<std::vector<EdgeInfo>> adj(num_faces);
  for (int i = 0; i < static_cast<int>(tree.size()); i++) {
    adj[tree[i].f0].push_back({tree[i].f1, i});
    adj[tree[i].f1].push_back({tree[i].f0, i});
  }

  // BFS to build parent pointers and locate shared edges (hinges).
  struct ParentInfo {
    int parent_face;
    int edge_idx;
    int v0;
    int v1;
  };
  std::vector<ParentInfo> parent_info(num_faces, {-1, -1, -1, -1});

  std::vector<int> q;
  q.reserve(num_faces);
  q.push_back(0); // Arbitrarily use face 0 as the fixed root.
  int q_idx = 0;

  while (q_idx < static_cast<int>(q.size())) {
    int curr = q[q_idx++];
    for (const EdgeInfo &ei : adj[curr]) {
      if (ei.nxt_face != parent_info[curr].parent_face) {
        int hinge_v0 = -1, hinge_v1 = -1;
        const std::vector<int> &p_poly = umesh.polygons[curr];
        const std::vector<int> &c_poly = umesh.polygons[ei.nxt_face];
        int p_n = static_cast<int>(p_poly.size());
        int c_n = static_cast<int>(c_poly.size());

        // Find the shared edge, favoring opposing directed edges.
        for (int i = 0; i < p_n; i++) {
          int p_v0 = p_poly[i];
          int p_v1 = p_poly[(i + 1) % p_n];
          for (int j = 0; j < c_n; j++) {
            if (c_poly[j] == p_v1 && c_poly[(j + 1) % c_n] == p_v0) {
              hinge_v0 = p_v0;
              hinge_v1 = p_v1;
              break;
            }
          }
          if (hinge_v0 != -1) break;
        }

        // Fallback: check for same-direction edge.
        if (hinge_v0 == -1) {
          for (int i = 0; i < p_n; i++) {
            int p_v0 = p_poly[i];
            int p_v1 = p_poly[(i + 1) % p_n];
            for (int j = 0; j < c_n; j++) {
              if (c_poly[j] == p_v0 && c_poly[(j + 1) % c_n] == p_v1) {
                hinge_v0 = p_v0;
                hinge_v1 = p_v1;
                break;
              }
            }
            if (hinge_v0 != -1) break;
          }
        }

        parent_info[ei.nxt_face] = {curr, ei.edge_idx, hinge_v0, hinge_v1};
        q.push_back(ei.nxt_face);
      }
    }
  }

  // Assign each vertex to an arbitrary face that contains it.
  std::vector<int> vertex_to_face(num_vertices, 0);
  for (int f = 0; f < num_faces; f++) {
    for (int v : umesh.polygons[f]) {
      vertex_to_face[v] = f;
    }
  }

  // Helper: rotates a 3D point around a directed axis using Rodrigues' formula.
  auto rotate_point = [](vec3 p, vec3 a, vec3 b, double angle) -> vec3 {
    vec3 axis = {.x = b.x - a.x, .y = b.y - a.y, .z = b.z - a.z};
    double len = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
    if (len < 1e-12) return p;
    axis.x /= len; axis.y /= len; axis.z /= len;

    vec3 v = {.x = p.x - a.x, .y = p.y - a.y, .z = p.z - a.z};
    double c = std::cos(angle);
    double s = std::sin(angle);

    vec3 cross_v = {
      .x = axis.y * v.z - axis.z * v.y,
      .y = axis.z * v.x - axis.x * v.z,
      .z = axis.x * v.y - axis.y * v.x,
    };

    double dot_v = axis.x * v.x + axis.y * v.y + axis.z * v.z;

    vec3 rotated_v = {
      .x = v.x * c + cross_v.x * s + axis.x * dot_v * (1.0 - c),
      .y = v.y * c + cross_v.y * s + axis.y * dot_v * (1.0 - c),
      .z = v.z * c + cross_v.z * s + axis.z * dot_v * (1.0 - c),
    };

    return vec3{
      .x = a.x + rotated_v.x,
      .y = a.y + rotated_v.y,
      .z = a.z + rotated_v.z,
    };
  };

  std::vector<vec3> positions(num_vertices);
  for (int i = 0; i < num_vertices; i++) {
    // Start with the 2D layout coordinates embedded in 3D (z=0)
    vec3 p = {.x = umesh.vertices[i].x, .y = umesh.vertices[i].y, .z = 0.0};
    int curr_face = vertex_to_face[i];

    // Walk up the spanning tree from the vertex's local face to the root,
    // applying the hinge rotations sequentially.
    while (curr_face != 0 && curr_face != -1) {
      const ParentInfo &pi = parent_info[curr_face];
      if (pi.v0 == -1 || pi.v1 == -1) {
        curr_face = pi.parent_face;
        continue;
      }
      vec3 hinge_a = {
        .x = umesh.vertices[pi.v0].x,
        .y = umesh.vertices[pi.v0].y,
        .z = 0.0,
      };
      vec3 hinge_b = {
        .x = umesh.vertices[pi.v1].x,
        .y = umesh.vertices[pi.v1].y,
        .z = 0.0,
      };

      p = rotate_point(p, hinge_a, hinge_b, dihedral_angles[pi.edge_idx]);
      curr_face = pi.parent_face;
    }
    positions[i] = p;
  }

  return positions;
}

// The optimization goal is to close the shape, meaning vertices that are glued
// together on the boundary must end up at the exact same 3D position.
struct GluingConstraint {
  int v0 = 0, v1 = 0;
};

// Extracts pairs of 2D vertices from the boundary that should be collocated.
std::vector<GluingConstraint> GetGluingConstraints(const Gluing &gluing) {
  std::vector<GluingConstraint> constraints;
  size_t num_pairs = gluing.pairs.size();
  for (size_t i = 0; i < num_pairs; i++) {
    const std::pair<DirectedEdge, DirectedEdge> &p = gluing.pairs[i];
    // The zipped edges are adjacent in the boundary cycle, so first.v1 and
    // second.v0 are already the same 2D vertex. We only need to constrain
    // the opposite ends.
    constraints.push_back({p.first.v0, p.second.v1});
  }
  return constraints;
}

// --- Phase 3: Solver ---

// Optimizes the internal dihedral angles to satisfy the gluing constraints.
// The objective minimizes the sum of squared 3D distances between vertices
// that are supposed to be glued:
//   E = sum_{c in constraints} || V[c.v0] - V[c.v1] ||^2
// This can be solved with gradient descent or a Levenberg-Marquardt solver.
// (A volume-maximizing term can be added if it tends to flatten out, but
// for valid unfoldings, E=0 is a unique convex minimum by Cauchy's Theorem).
std::optional<std::vector<double>> OptimizeDihedralAngles(
    const Folding::UnfoldedMesh &umesh,
    const std::vector<InternalEdge> &tree,
    const std::vector<GluingConstraint> &constraints) {
  int num_angles = (int)tree.size();
  if (num_angles == 0) {
    return std::vector<double>{};
  }

  std::function<double(std::span<const double>)> objective =
      [&umesh, &tree, &constraints](std::span<const double> angles) -> double {
    std::vector<vec3> positions = Compute3DPositions(umesh, tree, angles);
    double error = 0.0;
    for (const GluingConstraint &c : constraints) {
      auto diff = positions[c.v0] - positions[c.v1];
      error += (double)dot(diff, diff);
    }
    return error;
  };

  // Restrict bounds slightly to prevent degenerate flat foldings
  // (angles of exactly 0 or pi), guiding the solver to the convex state.
  std::vector<double> lower_bound(num_angles, 0.01);
  std::vector<double> upper_bound(num_angles, std::numbers::pi - 0.01);

  const int iters = 3000 * num_angles;
  auto [best_angles, best_error] = Opt::Minimize(
      num_angles, objective, lower_bound, upper_bound, iters, 2);

  if (VERBOSE) {
    Print("OptimizeDihedralAngles: best_error = {:.11g}\n", best_error);
  }

  if (best_error < 1e-5) {
    return best_angles;
  }
  Print("OptimizeDihedralAngles: Optimization failed to converge.\n");
  return std::nullopt;
}

// --- Main Wrapper ---

std::optional<Polyhedron> Folding::Fold(const UnfoldedMesh &umesh) {
  auto [boundary, internal] = PartitionEdges(umesh);

  // Find and glue the boundary.
  std::optional<Gluing> gluing_opt = ZipBoundary(umesh, boundary);
  if (!gluing_opt.has_value()) {
    Print("Failed to find gluing.\n");
    return std::nullopt;
  }

  // Set up parameterization and constraints.
  std::vector<GluingConstraint> constraints =
      GetGluingConstraints(gluing_opt.value());

  // Optimize dihedral angles.
  std::optional<std::vector<double>> solved_angles =
      OptimizeDihedralAngles(umesh, internal, constraints);
  if (!solved_angles.has_value()) {
    Print("Failed to find solved angles.\n");
    return std::nullopt;
  }

  // Compute final 3D coordinates.
  std::vector<vec3> vertices_3d =
      Compute3DPositions(umesh, internal, solved_angles.value());

  // Snap glued vertices together to ensure exact collocation, and
  // extract only the unique 3D vertices.
  // This avoids precision issues where the solver's small residual error
  // leaves glued vertices slightly separated. Furthermore, the convex hull
  // algorithm expects distinct points and may fail or create degenerate
  // geometry if exact duplicates are passed.
  UnionFind dsu((int)umesh.vertices.size());
  for (const GluingConstraint &c : constraints) {
    dsu.Union(c.v0, c.v1);
  }

  std::vector<vec3> fused(umesh.vertices.size(), vec3{0.0, 0.0, 0.0});
  std::vector<int> counts(umesh.vertices.size(), 0);
  for (size_t i = 0; i < vertices_3d.size(); i++) {
    int root = dsu.Find(static_cast<int>(i));
    fused[root].x += vertices_3d[i].x;
    fused[root].y += vertices_3d[i].y;
    fused[root].z += vertices_3d[i].z;
    counts[root]++;
  }

  std::vector<vec3> unique_vertices;
  for (size_t i = 0; i < vertices_3d.size(); i++) {
    if (dsu.Find(static_cast<int>(i)) == static_cast<int>(i)) {
      unique_vertices.push_back(vec3{
        fused[i].x / counts[i],
        fused[i].y / counts[i],
        fused[i].z / counts[i]
      });
    }
  }

  // Construct the Polyhedron.
  // PolyhedronFromVertices (from geom/polyhedra.h) robustly computes the
  // convex hull of a set of 3D points and generates the planar faces. Since
  // we know our vertices are extreme points of a convex polyhedron, this builds
  // a perfectly formed mesh without manual reconstruction.
  if (VERBOSE) {
    Print("{} vertices:\n", unique_vertices.size());
    for (const vec3 &v : unique_vertices) {
      Print("  {:.11g}, {:.11g}, {:.11g}\n", v.x, v.y, v.z);
    }
  }
  return PolyhedronFromVertices(unique_vertices);
}
