
#include "construct.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "base/stringprintf.h"
#include "geom/hull-2d.h"
#include "geom/hull-3d.h"
#include "geom/polyhedra.h"
#include "randutil.h"
#include "yocto-math.h"

void PartialPolyhedron::CheckValidity() const {
  CheckIndices();
  CheckHalfSpaces();
  CheckUnfoldings();
  CheckBoundary();
}

void PartialPolyhedron::CheckIndices() const {
  for (int i = 0; i < (int)edges.size(); ++i) {
    const MeshEdge &e = edges[i];
    CHECK(e.v0 >= 0 && e.v0 < (int)vertices.size());
    CHECK(e.v1 >= 0 && e.v1 < (int)vertices.size());
    CHECK(e.left_face >= 0 && e.left_face < (int)faces.size());
    CHECK(e.right_face >= -1 && e.right_face < (int)faces.size());
  }

  for (int i = 0; i < (int)faces.size(); ++i) {
    const MeshFace &f = faces[i];
    CHECK(f.vertices.size() == f.edges.size());
    for (int j = 0; j < (int)f.vertices.size(); ++j) {
      int v_start = f.vertices[j];
      int v_end = f.vertices[(j + 1) % f.vertices.size()];
      int e_idx = f.edges[j];

      CHECK(v_start >= 0 &&v_start < (int)vertices.size());
      CHECK(e_idx >= 0 &&e_idx < (int)edges.size());

      const MeshEdge &e = edges[e_idx];
      if (e.left_face == i) {
        CHECK(e.v0 == v_start && e.v1 == v_end);
      } else if (e.right_face == i) {
        CHECK(e.v1 == v_start && e.v0 == v_end);
      } else {
        LOG(FATAL) << "Face does not claim this edge";
      }
    }
  }
}

void PartialPolyhedron::CheckHalfSpaces() const {
  for (const MeshVertex &v : vertices) {
    for (const HalfSpace &hs : half_spaces) {
      // Allow a small epsilon for floating point limits.
      CHECK(yocto::dot(hs.normal, v.pos) <= hs.d + 1e-4);
    }
  }
}

void PartialPolyhedron::CheckUnfoldings() const {
  for (const Unfolding &unf : unfoldings) {
    CHECK((int)unf.faces.size() == NumFaces());

    // 1. Unfolded faces must have the same shape as 3D faces.
    for (int i = 0; i < NumFaces(); ++i) {
      const MeshFace &f3d = faces[i];
      const UnfoldedFace &f2d = unf.faces[i];
      CHECK(f3d.vertices.size() == f2d.vertices.size());

      for (int j = 0; j < (int)f3d.vertices.size(); ++j) {
        for (int k = j + 1; k < (int)f3d.vertices.size(); ++k) {
          double dist3d = yocto::length(vertices[f3d.vertices[j]].pos -
                                        vertices[f3d.vertices[k]].pos);
          double dist2d = yocto::length(f2d.vertices[j] - f2d.vertices[k]);
          CHECK(std::abs(dist3d - dist2d) < 1e-4);
        }
      }
    }

    if (NumFaces() <= 1) {
      CHECK(IsUnfoldingValid(unf));
      continue;
    }

    // 2. Unfolding must be acyclic and connected (a spanning tree).
    int connected_edges = 0;
    std::vector<std::vector<int>> adj(NumFaces());
    for (int e_idx = 0; e_idx < NumEdges(); ++e_idx) {
      const MeshEdge &edge = edges[e_idx];
      if (edge.left_face != -1 && edge.right_face != -1) {
        int f_left = edge.left_face;
        int f_right = edge.right_face;

        int left_idx = -1, right_idx = -1;
        for (int k = 0; k < (int)faces[f_left].edges.size(); ++k) {
          if (faces[f_left].edges[k] == e_idx) left_idx = k;
        }
        for (int k = 0; k < (int)faces[f_right].edges.size(); ++k) {
          if (faces[f_right].edges[k] == e_idx) right_idx = k;
        }

        CHECK(left_idx != -1 && right_idx != -1);

        vec2 l0 = unf.faces[f_left].vertices[left_idx];
        int l_next = (left_idx + 1) % faces[f_left].vertices.size();
        vec2 l1 = unf.faces[f_left].vertices[l_next];

        vec2 r0 = unf.faces[f_right].vertices[right_idx];
        int r_next = (right_idx + 1) % faces[f_right].vertices.size();
        vec2 r1 = unf.faces[f_right].vertices[r_next];

        if (yocto::length(l0 - r1) < 1e-4 && yocto::length(l1 - r0) < 1e-4) {
          connected_edges++;
          adj[f_left].push_back(f_right);
          adj[f_right].push_back(f_left);
        }
      }
    }

    // A spanning tree on N vertices has exactly N-1 edges.
    CHECK(connected_edges == NumFaces() - 1);

    // DFS to ensure all faces are reachable (connected).
    std::vector<bool> visited(NumFaces(), false);
    std::vector<int> stack = {0};
    visited[0] = true;
    int visited_count = 1;

    while (!stack.empty()) {
      int curr = stack.back();
      stack.pop_back();
      for (int neighbor : adj[curr]) {
        if (!visited[neighbor]) {
          visited[neighbor] = true;
          stack.push_back(neighbor);
          visited_count++;
        }
      }
    }
    CHECK(visited_count == NumFaces());

    // 3. Unfolding must be non-overlapping.
    CHECK(IsUnfoldingValid(unf));
  }
}

void PartialPolyhedron::CheckBoundary() const {
  std::vector<int> b_edges = GetBoundaryEdges();
  if (b_edges.empty()) {
    // Polyhedron might be completely closed.
    return;
  }

  std::vector<int> outgoing(vertices.size(), -1);
  std::vector<int> incoming(vertices.size(), -1);

  for (int e_idx : b_edges) {
    const MeshEdge &e = edges[e_idx];
    CHECK(e.right_face == -1);
    CHECK(e.left_face != -1);

    CHECK(outgoing[e.v0] == -1);
    outgoing[e.v0] = e_idx;

    CHECK(incoming[e.v1] == -1);
    incoming[e.v1] = e_idx;
  }

  int start_v = edges[b_edges[0]].v0;
  int curr_v = start_v;
  int count = 0;

  do {
    int e_idx = outgoing[curr_v];
    CHECK(e_idx != -1); // Path must continue
    curr_v = edges[e_idx].v1;
    count++;
  } while (curr_v != start_v && count <= (int)b_edges.size());

  // Ensures the boundary is exactly one complete loop.
  CHECK(count == (int)b_edges.size());
}

void PartialPolyhedron::CheckSelfIntersections() const {
  // Check that the polyhedron is free of self-intersections in 3D. We
  // do this by ensuring no edge penetrates the interior of any
  // non-adjacent face.
  for (int i = 0; i < NumEdges(); ++i) {
    const MeshEdge &e = edges[i];
    vec3 p0 = vertices[e.v0].pos;
    vec3 p1 = vertices[e.v1].pos;

    for (int j = 0; j < NumFaces(); ++j) {
      if (e.left_face == j || e.right_face == j) continue;

      const MeshFace &f = faces[j];
      bool shares_vertex = false;
      for (int v : f.vertices) {
        if (v == e.v0 || v == e.v1) shares_vertex = true;
      }
      if (shares_vertex) continue;

      double d0 = yocto::dot(f.plane.normal, p0) - f.plane.d;
      double d1 = yocto::dot(f.plane.normal, p1) - f.plane.d;

      // If the edge strictly crosses the plane of the face
      if (d0 * d1 < 0.0) {
        double t = d0 / (d0 - d1);
        vec3 pt = p0 + t * (p1 - p0);

        bool inside = true;
        for (int k = 0; k < (int)f.vertices.size(); ++k) {
          vec3 v0 = vertices[f.vertices[k]].pos;
          vec3 v1 = vertices[f.vertices[(k + 1) % f.vertices.size()]].pos;
          vec3 edge_dir = v1 - v0;
          vec3 pt_dir = pt - v0;
          vec3 cross = yocto::cross(edge_dir, pt_dir);
          // If the point is to the right of the directed edge, it's outside.
          if (yocto::dot(cross, f.plane.normal) < -1e-5) {
            inside = false;
            break;
          }
        }
        CHECK(!inside) << "Self-intersection: Edge " << i
                       << " intersects face " << j;
      }
    }
  }
}

void PartialPolyhedron::CheckPlanarityAndConvexity() const {
  // Check that all 3D faces are planar and strictly convex.
  for (int i = 0; i < NumFaces(); ++i) {
    const MeshFace &f = faces[i];
    CHECK(f.vertices.size() >= 3) << "Face " << i
                                  << " has fewer than 3 vertices.";

    std::vector<vec3> pts;
    pts.reserve(f.vertices.size());
    for (int v_idx : f.vertices) {
      pts.push_back(vertices[v_idx].pos);
    }

    CHECK(PlanarityError(pts) < 1e-4) << "Face " << i << " is not planar.";

    for (int j = 0; j < (int)pts.size(); ++j) {
      vec3 v0 = pts[j];
      vec3 v1 = pts[(j + 1) % pts.size()];
      vec3 v2 = pts[(j + 2) % pts.size()];

      vec3 e1 = v1 - v0;
      vec3 e2 = v2 - v1;
      vec3 cross = yocto::cross(e1, e2);

      // Verify strictly convex (cross product points in the outward
      // normal direction)
      CHECK(yocto::dot(cross, f.plane.normal) > 1e-5)
          << "Face " << i << " is not strictly convex.";
    }
  }
}

void PartialPolyhedron::CheckDihedralAngles() const {
  // Check that dihedral angles between adjacent faces are valid.
  for (int i = 0; i < NumEdges(); ++i) {
    const MeshEdge &e = edges[i];
    if (e.left_face != -1 && e.right_face != -1) {
      const MeshFace &f_left = faces[e.left_face];
      const MeshFace &f_right = faces[e.right_face];

      // Faces should not be coplanar or folded back on each other
      double dot_n = yocto::dot(f_left.plane.normal, f_right.plane.normal);
      CHECK(dot_n > -1.0 + 1e-5 && dot_n < 1.0 - 1e-5)
          << "Invalid dihedral angle between faces " << e.left_face
          << " and " << e.right_face;

      // Verify strict convexity of the dihedral angle
      int test_v = -1;
      for (int v : f_right.vertices) {
        if (v != e.v0 && v != e.v1) {
          test_v = v;
          break;
        }
      }

      if (test_v != -1) {
        vec3 p = vertices[test_v].pos;
        vec3 p0 = vertices[e.v0].pos;
        vec3 p1 = vertices[e.v1].pos;
        double dist = yocto::dot(f_left.plane.normal, p) - f_left.plane.d;
        double dist_to_hinge = yocto::length(
            yocto::cross(p - p0, yocto::normalize(p1 - p0)));
        CHECK(dist < -1e-5 * dist_to_hinge)
            << "Non-convex dihedral angle at edge " << i;
      }
    }
  }
}

void PartialPolyhedron::CheckUnfoldingWindingOrder() const {
  // Check that 2D unfoldings preserve the winding order of 3D faces.
  for (int u_idx = 0; u_idx < (int)unfoldings.size(); ++u_idx) {
    const Unfolding &unf = unfoldings[u_idx];
    for (int i = 0; i < NumFaces(); ++i) {
      const UnfoldedFace &f2d = unf.faces[i];
      double area = SignedAreaOfConvexPoly(f2d.vertices);
      // Area > 0 indicates cartesian CCW order, which matches the expected
      // CCW winding order of 3D faces when viewed from the exterior.
      CHECK(area > 1e-5) << "Unfolding " << u_idx << ", face " << i
                         << " does not preserve CCW winding order.";
    }
  }
}

void PartialPolyhedron::UpdateAABB(const vec3 &v) {
  min_vertex.x = std::min(min_vertex.x, v.x);
  min_vertex.y = std::min(min_vertex.y, v.y);
  min_vertex.z = std::min(min_vertex.z, v.z);

  max_vertex.x = std::max(max_vertex.x, v.x);
  max_vertex.y = std::max(max_vertex.y, v.y);
  max_vertex.z = std::max(max_vertex.z, v.z);
}

// Creates the initial partial polyhedron. This consists of
// two joined faces so that we can maintain the invariant that
// the object has a non-degenerate boundary and volume.
void PartialPolyhedron::Initialize(int face1_max_verts,
                                   int face2_max_verts) {
  CHECK(face1_max_verts >= 3 && face2_max_verts >= 3) << "precondition";

  // Generate random coordinates for the remaining vertices to avoid
  // degenerate shapes and ensure a valid dihedral angle.
  auto GenerateConvexPoly = [&](int max_verts, bool positive_y) {
    std::vector<vec2> pts;
    pts.push_back(vec2{0.0, 0.0});
    pts.push_back(vec2{1.0, 0.0});
    for (int i = 0; i < max_verts - 2; ++i) {
      double x = 0.2 + 0.6 * RandDouble(rc);
      double y = 0.2 + 0.6 * RandDouble(rc);
      if (!positive_y) y = -y;
      pts.push_back(vec2{x, y});
    }

    std::vector<int> hull_idx = Hull2D::GrahamScan(pts);
    std::vector<vec2> hull;

    // Shift the hull so that (0,0) is the first vertex.
    int zero_idx = 0;
    for (int i = 0; i < (int)hull_idx.size(); ++i) {
      if (hull_idx[i] == 0) {
        zero_idx = i;
        break;
      }
    }
    for (int i = 0; i < (int)hull_idx.size(); ++i) {
      hull.push_back(pts[hull_idx[(zero_idx + i) % hull_idx.size()]]);
    }
    return hull;
  };

  std::vector<vec2> U0 = GenerateConvexPoly(face1_max_verts, false);
  std::vector<vec2> U1 = GenerateConvexPoly(face2_max_verts, true);

  // We always insert the original, so this is
  // a safe initial value.
  min_vertex = max_vertex = vec3{0, 0, 0};

  auto AddVertex = [this](vec3 v) {
    vertices.push_back(MeshVertex{v});
    UpdateAABB(v);
  };

  AddVertex(vec3{0.0, 0.0, 0.0});
  AddVertex(vec3{1.0, 0.0, 0.0});

  // Face 0 vertices (in the z=0 plane, mirrored to preserve CCW exterior)
  for (int i = 1; i < (int)U0.size() - 1; ++i) {
    AddVertex(vec3{U0[i].x, -U0[i].y, 0.0});
  }

  int offset = (int)vertices.size();

  // Face 1 bending upwards into z > 0 for convexity
  double theta = std::numbers::pi * 0.75; // 135 degrees
  double cos_t = std::cos(theta);
  double sin_t = std::sin(theta);
  for (int i = 2; i < (int)U1.size(); ++i) {
    AddVertex(vec3{U1[i].x, U1[i].y * cos_t, U1[i].y * sin_t});
  }

  auto AddEdge = [&](int start, int end, int left_face, int right_face) {
    int idx = (int)edges.size();
    edges.push_back(MeshEdge{start, end, left_face, right_face});
    return idx;
  };

  // The shared edge connects v0 and v1. Face 1 sees 0->1, Face 0 sees 1->0.
  int shared_edge = AddEdge(0, 1, 1, 0);

  std::vector<int> f0_verts;
  f0_verts.push_back(0);
  for (int i = 1; i < (int)U0.size() - 1; ++i) {
    f0_verts.push_back(2 + i - 1);
  }
  f0_verts.push_back(1);

  std::vector<int> f0_edges;
  for (int j = 0; j < (int)f0_verts.size(); ++j) {
    int start = f0_verts[j];
    int end = f0_verts[(j + 1) % f0_verts.size()];
    if (start == 1 && end == 0) {
      f0_edges.push_back(shared_edge);
    } else {
      f0_edges.push_back(AddEdge(start, end, 0, -1));
    }
  }

  std::vector<int> f1_verts;
  f1_verts.push_back(0);
  f1_verts.push_back(1);
  for (int i = 2; i < (int)U1.size(); ++i) {
    f1_verts.push_back(offset + i - 2);
  }

  std::vector<int> f1_edges;
  for (int j = 0; j < (int)f1_verts.size(); ++j) {
    int start = f1_verts[j];
    int end = f1_verts[(j + 1) % f1_verts.size()];
    if (start == 0 && end == 1) {
      f1_edges.push_back(shared_edge);
    } else {
      f1_edges.push_back(AddEdge(start, end, 1, -1));
    }
  }

  auto ComputePlane = [&](const std::vector<int>& face_verts) {
    vec3 p0 = vertices[face_verts[0]].pos;
    vec3 p1 = vertices[face_verts[1]].pos;
    vec3 p2 = vertices[face_verts[2]].pos;
    vec3 n = yocto::normalize(yocto::cross(p1 - p0, p2 - p1));
    HalfSpace hs;
    hs.normal = n;
    hs.d = yocto::dot(n, p0);
    return hs;
  };

  // Set up Face 0
  MeshFace f0;
  f0.vertices = f0_verts;
  f0.edges = f0_edges;
  f0.plane = ComputePlane(f0.vertices);
  faces.push_back(f0);

  // Set up Face 1
  MeshFace f1;
  f1.vertices = f1_verts;
  f1.edges = f1_edges;
  f1.plane = ComputePlane(f1.vertices);
  faces.push_back(f1);

  half_spaces.push_back(f0.plane);
  half_spaces.push_back(f1.plane);

  // Initialize the single starting 2D unfolding.
  Unfolding unf;
  UnfoldedFace uf0, uf1;

  uf0.vertices = U0;
  uf1.vertices = U1;

  unf.faces.push_back(uf0);
  unf.faces.push_back(uf1);

  unfoldings.push_back(unf);
}

int PartialPolyhedron::NumFaces() const {
  return (int)faces.size();
}

int PartialPolyhedron::NumEdges() const {
  return (int)edges.size();
}

int PartialPolyhedron::NumVertices() const {
  return (int)vertices.size();
}

std::vector<int> PartialPolyhedron::GetBoundaryEdges() const {
  std::vector<int> b_edges;
  for (int i = 0; i < (int)edges.size(); i++) {
    if (edges[i].right_face == -1) {
      b_edges.push_back(i);
    }
  }
  return b_edges;
}

bool PartialPolyhedron::IsUnfoldingValid(const Unfolding &unfolding) const {
  for (int i = 0; i < (int)unfolding.faces.size(); i++) {
    for (int j = i + 1; j < (int)unfolding.faces.size(); j++) {
      const std::vector<vec2> &f1 = unfolding.faces[i].vertices;
      const std::vector<vec2> &f2 = unfolding.faces[j].vertices;

      // If neither polygon provides a separating axis, their
      // interiors overlap.
      if (!HasSeparatingAxis(f1, f2) && !HasSeparatingAxis(f2, f1)) {
        return false;
      }
    }
  }
  return true;
}

void PartialPolyhedron::ReplenishUnfoldings() {
  if (NumFaces() <= 1) return;

  // Precompute local 2D shapes for each face.
  std::vector<std::vector<vec2>> local_shapes(NumFaces());
  for (int i = 0; i < NumFaces(); ++i) {
    const MeshFace &f = faces[i];
    if (f.vertices.empty()) continue;
    vec3 v0 = vertices[f.vertices[0]].pos;
    vec3 v1 = vertices[f.vertices[1]].pos;
    vec3 N = f.plane.normal;
    vec3 X = yocto::normalize(v1 - v0);
    vec3 Y = yocto::cross(N, X);
    for (int v_idx : f.vertices) {
      vec3 v = vertices[v_idx].pos;
      vec3 d = v - v0;
      local_shapes[i].push_back(vec2{yocto::dot(d, X), yocto::dot(d, Y)});
    }
  }

  // Find all dual edges to build spanning trees over the faces.
  struct DualEdge {
    int e_idx, f1, f2;
  };
  std::vector<DualEdge> dual_edges;
  for (int i = 0; i < NumEdges(); ++i) {
    if (edges[i].left_face != -1 && edges[i].right_face != -1) {
      dual_edges.push_back({i, edges[i].left_face, edges[i].right_face});
    }
  }

  // We uniquely identify an unfolding by its sorted set of tree edges.
  std::vector<std::vector<int>> existing_trees;
  for (const Unfolding &unf : unfoldings) {
    std::vector<int> tree_edges;
    for (int e_idx = 0; e_idx < NumEdges(); ++e_idx) {
      const MeshEdge &edge = edges[e_idx];
      if (edge.left_face != -1 && edge.right_face != -1) {
        int f_left = edge.left_face;
        int f_right = edge.right_face;

        int left_idx = -1, right_idx = -1;
        for (int k = 0; k < (int)faces[f_left].edges.size(); ++k) {
          if (faces[f_left].edges[k] == e_idx) left_idx = k;
        }
        for (int k = 0; k < (int)faces[f_right].edges.size(); ++k) {
          if (faces[f_right].edges[k] == e_idx) right_idx = k;
        }

        if (left_idx != -1 && right_idx != -1) {
          vec2 l0 = unf.faces[f_left].vertices[left_idx];
          int l_next = (left_idx + 1) % faces[f_left].vertices.size();
          vec2 l1 = unf.faces[f_left].vertices[l_next];

          vec2 r0 = unf.faces[f_right].vertices[right_idx];
          int r_next = (right_idx + 1) % faces[f_right].vertices.size();
          vec2 r1 = unf.faces[f_right].vertices[r_next];

          if (yocto::length(l0 - r1) < 1e-4 &&
              yocto::length(l1 - r0) < 1e-4) {
            tree_edges.push_back(e_idx);
          }
        }
      }
    }
    std::sort(tree_edges.begin(), tree_edges.end());
    existing_trees.push_back(tree_edges);
  }

  int consecutive_failures = 0;
  int max_failures = 1000;

  while ((int)unfoldings.size() < target_unfoldings &&
         consecutive_failures < max_failures) {
    // Generate a random spanning tree by assigning random weights.
    std::vector<std::pair<double, int>> edge_weights;
    for (int i = 0; i < (int)dual_edges.size(); ++i) {
      edge_weights.push_back({RandDouble(rc), i});
    }
    std::sort(edge_weights.begin(), edge_weights.end());

    std::vector<int> parent(NumFaces());
    for (int i = 0; i < NumFaces(); ++i) parent[i] = i;

    auto Find = [&](int i) {
      int root = i;
      while (root != parent[root]) root = parent[root];
      int curr = i;
      while (curr != root) {
        int nxt = parent[curr];
        parent[curr] = root;
        curr = nxt;
      }
      return root;
    };

    auto Union = [&](int i, int j) {
      int root_i = Find(i);
      int root_j = Find(j);
      if (root_i != root_j) {
        parent[root_i] = root_j;
        return true;
      }
      return false;
    };

    std::vector<int> tree_edges;
    std::vector<std::vector<int>> adj(NumFaces());
    for (const auto &ew : edge_weights) {
      const DualEdge &de = dual_edges[ew.second];
      if (Union(de.f1, de.f2)) {
        tree_edges.push_back(de.e_idx);
        adj[de.f1].push_back(de.e_idx);
        adj[de.f2].push_back(de.e_idx);
      }
    }
    std::sort(tree_edges.begin(), tree_edges.end());

    bool found = false;
    for (const auto &ext : existing_trees) {
      if (ext == tree_edges) {
        found = true;
        break;
      }
    }
    if (found) {
      consecutive_failures++;
      continue;
    }

    // Unfold in 2D using the tree topology.
    Unfolding unf;
    unf.faces.resize(NumFaces());
    std::vector<bool> placed(NumFaces(), false);

    unf.faces[0].vertices = local_shapes[0];
    placed[0] = true;

    std::vector<int> q = {0};
    while (!q.empty()) {
      int curr = q.back();
      q.pop_back();

      for (int e_idx : adj[curr]) {
        const MeshEdge &e = edges[e_idx];
        int neighbor = (e.left_face == curr) ? e.right_face : e.left_face;
        if (!placed[neighbor]) {
          int idx_a = -1;
          for (int k = 0; k < (int)faces[curr].edges.size(); ++k) {
            if (faces[curr].edges[k] == e_idx) idx_a = k;
          }
          int idx_b = -1;
          for (int k = 0; k < (int)faces[neighbor].edges.size(); ++k) {
            if (faces[neighbor].edges[k] == e_idx) idx_b = k;
          }

          vec2 p_start = unf.faces[curr].vertices[idx_a];
          vec2 p_end = unf.faces[curr].vertices[(idx_a + 1) %
                                                faces[curr].vertices.size()];

          const auto &poly_b = local_shapes[neighbor];
          vec2 q_start = poly_b[idx_b];
          vec2 q_end = poly_b[(idx_b + 1) % poly_b.size()];

          vec2 v_q = q_end - q_start;
          vec2 v_p = p_start - p_end;

          double length_sq = yocto::length_squared(v_q);
          double cos_theta = 1.0;
          double sin_theta = 0.0;
          if (length_sq > 1e-12) {
            cos_theta = yocto::dot(v_q, v_p) / length_sq;
            sin_theta = (v_q.x * v_p.y - v_q.y * v_p.x) / length_sq;
          }

          for (const vec2 &q_pt : poly_b) {
            vec2 d = q_pt - q_start;
            vec2 rot_d;
            rot_d.x = d.x * cos_theta - d.y * sin_theta;
            rot_d.y = d.x * sin_theta + d.y * cos_theta;
            unf.faces[neighbor].vertices.push_back(p_end + rot_d);
          }

          placed[neighbor] = true;
          q.push_back(neighbor);
        }
      }
    }

    if (IsUnfoldingValid(unf)) {
      unfoldings.push_back(unf);
      existing_trees.push_back(tree_edges);
      consecutive_failures = 0;
    } else {
      existing_trees.push_back(tree_edges);
      consecutive_failures++;
    }
  }
}

std::pair<double, double> PartialPolyhedron::ComputeFeasibleAngles(
    int edge_idx) const {
  const MeshEdge &e = edges[edge_idx];
  CHECK(e.left_face != -1);
  CHECK(e.right_face == -1);

  vec3 p0 = vertices[e.v0].pos;
  vec3 p1 = vertices[e.v1].pos;
  vec3 edge_dir = yocto::normalize(p1 - p0);

  const MeshFace &f_left = faces[e.left_face];
  vec3 normal_left = f_left.plane.normal;
  vec3 outward_dir = yocto::cross(edge_dir, normal_left);

  // Don't allow really shallow angles; the dot product scales
  // quadratically for small angles and it's easy to get precision
  // problems.
  double min_angle = 5e-3;
  double max_angle = std::numbers::pi - 5e-3;

  for (int i = 0; i < (int)vertices.size(); i++) {
    if (i == e.v0 || i == e.v1) {
      continue;
    }

    vec3 offset = vertices[i].pos - p0;
    double u = yocto::dot(offset, normal_left);
    double w = yocto::dot(offset, outward_dir);

    // All existing vertices should be inside the left_face half-space.
    // Clamp to avoid floating-point issues causing u > 0.
    u = std::min(0.0, u);

    // Ignore vertices that are functionally on the hinge.
    if (u > -1e-7 && std::abs(w) < 1e-7) {
      continue;
    }

    // Here, abs(u) instead of -u to avoid atan2 returning -pi
    // on -0! Devilish!
    double max_theta = std::atan2(std::abs(u), w);
    if (max_theta < max_angle) {
      max_angle = max_theta;
    }
  }

  if (max_angle < min_angle) {
    max_angle = min_angle;
  }

  return {min_angle, max_angle};
}

std::vector<vec2>
PartialPolyhedron::ComputeFeasibleRegion(int edge_idx,
                                         double dihedral_angle) const {
  const MeshEdge &e = edges[edge_idx];
  CHECK(e.left_face != -1);
  CHECK(e.right_face == -1);

  vec3 p0 = vertices[e.v0].pos;
  vec3 p1 = vertices[e.v1].pos;
  vec3 edge_dir = yocto::normalize(p1 - p0);

  const MeshFace &f_left = faces[e.left_face];
  vec3 normal_left = f_left.plane.normal;
  vec3 outward_dir = yocto::cross(edge_dir, normal_left);

  double theta = dihedral_angle;
  vec3 n_new = std::cos(theta) * normal_left +
               std::sin(theta) * outward_dir;

  // Local 2D coordinate system for the new face.
  // To preserve CCW winding from the exterior, the edge must traverse
  // from v1 to v0 in the new face.
  vec3 origin = p1;
  vec3 x = yocto::normalize(p0 - p1);
  vec3 y = yocto::cross(n_new, x);

  // Start with a large bounding polygon
  double r = 1.0e5;
  std::vector<vec2> poly = {
      {-r, -r}, {r, -r}, {r, r}, {-r, r}};

  // Intersect with all existing half-spaces
  for (const HalfSpace &hs : half_spaces) {
    // 2D half-space: a*x + b*y <= c
    double a = yocto::dot(hs.normal, x);
    double b = yocto::dot(hs.normal, y);
    double c = hs.d - yocto::dot(hs.normal, origin);

    std::vector<vec2> next_poly;
    if (poly.empty()) break;

    for (int i = 0; i < (int)poly.size(); ++i) {
      vec2 v_curr = poly[i];
      vec2 v_next = poly[(i + 1) % poly.size()];

      double d_curr = a * v_curr.x + b * v_curr.y - c - 1e-7;
      double d_next = a * v_next.x + b * v_next.y - c - 1e-7;

      bool in_curr = (d_curr <= 0.0);
      bool in_next = (d_next <= 0.0);

      if (in_curr) {
        next_poly.push_back(v_curr);
      }

      if (in_curr != in_next) {
        double t = d_curr / (d_curr - d_next);
        next_poly.push_back(v_curr + t * (v_next - v_curr));
      }
    }
    poly = next_poly;
  }

  return poly;
}

const char *PartialPolyhedron::FeasibilityProblem(
    int boundary_edge_idx, const std::vector<vec3> &new_face_pts) const {
  if (new_face_pts.size() < 3)
    return "face has no area";

  if (boundary_edge_idx < 0 || boundary_edge_idx >= (int)edges.size()) {
    return "boundary out of range";
  }

  const MeshEdge &e = edges[boundary_edge_idx];
  if (e.left_face == -1 || e.right_face != -1)
    return "edge not on boundary";

  vec3 p0 = vertices[e.v0].pos;
  vec3 p1 = vertices[e.v1].pos;

  // Check if p1 -> p0 is an edge in the new face (to preserve CCW winding).
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
  if (!found_edge) return "edge not found";

  // Check planarity.
  if (PlanarityError(new_face_pts) > 1e-4) return "new face not planar";

  // Compute normal robustly using the cross product of sequential vertices.
  vec3 normal = {0.0, 0.0, 0.0};
  for (int i = 0; i < (int)new_face_pts.size(); ++i) {
    vec3 p_curr = new_face_pts[i];
    vec3 p_next = new_face_pts[(i + 1) % new_face_pts.size()];
    normal += yocto::cross(p_curr, p_next);
  }
  double len = yocto::length(normal);
  if (len < 1e-5) return "degenerate normal";
  normal /= len;

  // Explicitly check that the winding order of the new face is correct.
  // The convexity check below does NOT imply this: if the face is wound
  // clockwise, both its Newell normal and its edge cross products will
  // flip to point inward, keeping their dot product positive! We verify
  // that the normal points "outward" relative to the shared edge.
  vec3 edge_dir = yocto::normalize(p1 - p0);
  vec3 outward_dir = yocto::cross(edge_dir, faces[e.left_face].plane.normal);
  if (yocto::dot(normal, outward_dir) < -1e-5) {
    return "wrong winding order";
  }


  // Check strict convexity of the new face.
  for (int j = 0; j < (int)new_face_pts.size(); ++j) {
    vec3 p_prev = new_face_pts[j];
    vec3 p_curr = new_face_pts[(j + 1) % new_face_pts.size()];
    vec3 p_next = new_face_pts[(j + 2) % new_face_pts.size()];

    vec3 e1 = p_curr - p_prev;
    vec3 e2 = p_next - p_curr;
    vec3 cross = yocto::cross(e1, e2);

    if (yocto::dot(cross, normal) <= 1e-5) {
      return "new face not convex";
    }
  }

  // All new points must satisfy existing half-spaces.
  for (const vec3 &p : new_face_pts) {
    for (const HalfSpace &hs : half_spaces) {
      if (yocto::dot(hs.normal, p) > hs.d + 1e-4) {
        return "points outside half-spaces";
      }
    }
  }

  // The new face's half-space must contain all existing vertices.
  HalfSpace new_hs;
  new_hs.normal = normal;
  new_hs.d = yocto::dot(normal, new_face_pts[0]);

  for (const MeshVertex &v : vertices) {
    if (yocto::dot(new_hs.normal, v.pos) > new_hs.d + 1e-4) {
      return "existing vertex outside new half-space";
    }
  }

  // Dihedral angle checks against the adjacent face.
  const MeshFace &f_left = faces[e.left_face];
  double dot_n = yocto::dot(f_left.plane.normal, normal);
  if (dot_n <= -1.0 + 1e-5) {
    return "bad dihedral angle: faces folded back on each other";
  }
  if (dot_n >= 1.0 - 1e-5) {
    return "bad dihedral angle: faces are coplanar";
  }

  // Ensure strict convexity of the dihedral angle (it must fold
  // "inward".
  int test_v_idx = -1;
  for (int i = 0; i < (int)new_face_pts.size(); ++i) {
    // Pick a vertex from the new face that isn't on the shared edge.
    if (yocto::length(new_face_pts[i] - p0) > 1e-4 &&
        yocto::length(new_face_pts[i] - p1) > 1e-4) {
      test_v_idx = i;
      break;
    }
  }

  if (test_v_idx != -1) {
    vec3 pt = new_face_pts[test_v_idx];
    double dist = yocto::dot(f_left.plane.normal, pt) - f_left.plane.d;
    double dist_to_hinge = yocto::length(
        yocto::cross(pt - p0, yocto::normalize(p1 - p0)));

    if (dist >= -1e-5 * dist_to_hinge) {
      return "dihedral angle is not strictly convex";
    }
  }

  // Simulate vertex merging to check for edge collisions.
  std::vector<int> face_v_indices;
  for (const vec3 &pt : new_face_pts) {
    int match_idx = -1;
    for (int j = 0; j < (int)vertices.size(); j++) {
      if (yocto::length(vertices[j].pos - pt) < 1e-4) {
        match_idx = j;
        break;
      }
    }
    face_v_indices.push_back(match_idx);
  }

  for (int i = 0; i < (int)face_v_indices.size(); i++) {
    int v0 = face_v_indices[i];
    int v1 = face_v_indices[(i + 1) % face_v_indices.size()];
    if (v0 != -1 && v1 != -1) {
      if (v0 == v1) {
        return "degenerate edge after vertex merging";
      }
      for (const MeshEdge &edge : edges) {
        if (edge.v0 == v1 && edge.v1 == v0 && edge.right_face != -1) {
          return "edge is already shared";
        }
        if (edge.v0 == v0 && edge.v1 == v1) {
          return "edge already exists in same direction";
        }
      }
    }
  }

  // OK.
  return nullptr;
}

void PartialPolyhedron::AddFace(int boundary_edge_idx,
                                const std::vector<vec3> &new_face_pts) {
  int n = (int)new_face_pts.size();

  // Find or insert vertices.
  std::vector<int> face_v_indices;
  for (int i = 0; i < n; i++) {
    const vec3 &pt = new_face_pts[i];
    int match_idx = -1;
    for (int j = 0; j < (int)vertices.size(); j++) {
      if (yocto::length(vertices[j].pos - pt) < 1e-4) {
        match_idx = j;
        break;
      }
    }
    if (match_idx == -1) {
      match_idx = (int)vertices.size();
      vertices.push_back(MeshVertex{pt});
      UpdateAABB(pt);
    }
    face_v_indices.push_back(match_idx);
  }

  int new_face_idx = (int)faces.size();

  // Find or create edges.
  std::vector<int> face_e_indices;
  for (int i = 0; i < n; i++) {
    int v0 = face_v_indices[i];
    int v1 = face_v_indices[(i + 1) % n];

    int match_e = -1;
    for (int j = 0; j < (int)edges.size(); j++) {
      if (edges[j].v0 == v1 && edges[j].v1 == v0) {
        match_e = j;
        break;
      }
    }

    if (match_e != -1) {
      CHECK(edges[match_e].right_face == -1) << "Edge is already shared.";
      edges[match_e].right_face = new_face_idx;
      face_e_indices.push_back(match_e);
    } else {
      MeshEdge new_e;
      new_e.v0 = v0;
      new_e.v1 = v1;
      new_e.left_face = new_face_idx;
      new_e.right_face = -1;
      face_e_indices.push_back((int)edges.size());
      edges.push_back(new_e);
    }
  }

  // Set up the new face.
  MeshFace f;
  f.vertices = face_v_indices;
  f.edges = face_e_indices;

  vec3 normal = {0.0, 0.0, 0.0};
  for (int i = 0; i < n; i++) {
    vec3 p_curr = new_face_pts[i];
    vec3 p_next = new_face_pts[(i + 1) % n];
    normal += yocto::cross(p_curr, p_next);
  }
  normal = yocto::normalize(normal);

  f.plane.normal = normal;
  f.plane.d = yocto::dot(normal, new_face_pts[0]);

  faces.push_back(f);
  half_spaces.push_back(f.plane);

  num_faces_left--;

  // Construct local 2D shape for the new face.
  vec3 v0_3d = new_face_pts[0];
  vec3 v1_3d = new_face_pts[1];
  vec3 x_axis = yocto::normalize(v1_3d - v0_3d);
  vec3 y_axis = yocto::cross(normal, x_axis);

  std::vector<vec2> local_shape;
  for (int i = 0; i < n; i++) {
    vec3 d = new_face_pts[i] - v0_3d;
    local_shape.push_back(vec2{yocto::dot(d, x_axis), yocto::dot(d, y_axis)});
  }

  // Find the boundary edge indices in both adjacent face and new face.
  int adj_face = edges[boundary_edge_idx].left_face;
  int idx_a = -1;
  for (int k = 0; k < (int)faces[adj_face].edges.size(); k++) {
    if (faces[adj_face].edges[k] == boundary_edge_idx) {
      idx_a = k;
      break;
    }
  }

  int idx_b = -1;
  for (int k = 0; k < (int)f.edges.size(); k++) {
    if (f.edges[k] == boundary_edge_idx) {
      idx_b = k;
      break;
    }
  }

  CHECK(idx_a != -1 && idx_b != -1) << "Boundary edge not found.";

  // Update unfoldings.
  std::vector<Unfolding> next_unfoldings;
  for (int i = 0; i < (int)unfoldings.size(); i++) {
    Unfolding unf = unfoldings[i];
    vec2 p_start = unf.faces[adj_face].vertices[idx_a];
    vec2 p_end = unf.faces[adj_face].vertices[(idx_a + 1) %
                                              faces[adj_face].vertices.size()];

    vec2 q_start = local_shape[idx_b];
    vec2 q_end = local_shape[(idx_b + 1) % local_shape.size()];

    vec2 v_q = q_end - q_start;
    vec2 v_p = p_start - p_end;

    double length_sq = yocto::length_squared(v_q);
    double cos_theta = 1.0;
    double sin_theta = 0.0;
    if (length_sq > 1e-12) {
      cos_theta = yocto::dot(v_q, v_p) / length_sq;
      sin_theta = (v_q.x * v_p.y - v_q.y * v_p.x) / length_sq;
    }

    UnfoldedFace uf_new;
    for (int j = 0; j < (int)local_shape.size(); j++) {
      vec2 d = local_shape[j] - q_start;
      vec2 rot_d;
      rot_d.x = d.x * cos_theta - d.y * sin_theta;
      rot_d.y = d.x * sin_theta + d.y * cos_theta;
      uf_new.vertices.push_back(p_end + rot_d);
    }

    unf.faces.push_back(uf_new);

    // Keep valid unfoldings without overlapping.
    if (IsUnfoldingValid(unf)) {
      next_unfoldings.push_back(std::move(unf));
    }
  }

  unfoldings = std::move(next_unfoldings);
}

std::optional<Polyhedron> PartialPolyhedron::Close() const {
  std::vector<vec3> pts;
  pts.reserve(vertices.size());
  for (const MeshVertex &v : vertices) {
    pts.push_back(v.pos);
  }
  if (!IsWellConditioned(pts)) {
    return std::nullopt;
  }

  return PolyhedronFromConvexVertices(Hull3D::ReduceToHull(pts));
}

bool PartialPolyhedron::HasSeparatingAxis(
    const std::vector<vec2> &poly1,
    const std::vector<vec2> &poly2) const {
  for (int i = 0; i < (int)poly1.size(); i++) {
    vec2 a = poly1[i];
    vec2 b = poly1[(i + 1) % poly1.size()];
    vec2 edge = b - a;
    double len = yocto::length(edge);
    if (len < 1e-7) {
      continue;
    }

    // Determine which side of this edge is the interior of poly1
    double interior_sign = 0.0;
    for (int k = 0; k < (int)poly1.size(); k++) {
      double dist = yocto::cross(edge, poly1[k] - a) / len;
      if (std::abs(dist) > 1e-5) {
        interior_sign = dist;
        break;
      }
    }

    if (interior_sign == 0.0) {
      continue;
    }

    // Check if poly2 is entirely on the outside (or on the boundary)
    bool all_outside_or_on = true;
    for (int k = 0; k < (int)poly2.size(); k++) {
      vec2 v = poly2[k];
      double dist = yocto::cross(edge, v - a) / len;
      if (interior_sign > 0.0 && dist > 1e-5) {
        all_outside_or_on = false;
        break;
      } else if (interior_sign < 0.0 && dist < -1e-5) {
        all_outside_or_on = false;
        break;
      }
    }

    if (all_outside_or_on) {
      return true;
    }
  }
  return false;
}

double PartialPolyhedron::MeasureOverlapFraction(
    int boundary_edge_idx,
    const std::vector<vec2> &poly) const {
  if (unfoldings.empty()) return 0.0;

  const MeshEdge &e = edges[boundary_edge_idx];
  int adj_face = e.left_face;
  CHECK(adj_face != -1);

  int idx_a = -1;
  for (int k = 0; k < (int)faces[adj_face].edges.size(); k++) {
    if (faces[adj_face].edges[k] == boundary_edge_idx) {
      idx_a = k;
      break;
    }
  }
  CHECK(idx_a != -1);

  int overlap_count = 0;
  for (const Unfolding &unf : unfoldings) {
    vec2 p_start = unf.faces[adj_face].vertices[idx_a];
    vec2 p_end = unf.faces[adj_face].vertices[(idx_a + 1) %
                                              faces[adj_face].vertices.size()];

    vec2 q_start = poly[0];
    vec2 q_end = poly[1];

    vec2 v_q = q_end - q_start;
    vec2 v_p = p_start - p_end;

    double length_sq = yocto::length_squared(v_q);
    double cos_theta = 1.0;
    double sin_theta = 0.0;
    if (length_sq > 1e-12) {
      cos_theta = yocto::dot(v_q, v_p) / length_sq;
      sin_theta = (v_q.x * v_p.y - v_q.y * v_p.x) / length_sq;
    }

    std::vector<vec2> transformed_poly;
    transformed_poly.reserve(poly.size());
    for (int j = 0; j < (int)poly.size(); j++) {
      vec2 d = poly[j] - q_start;
      vec2 rot_d;
      rot_d.x = d.x * cos_theta - d.y * sin_theta;
      rot_d.y = d.x * sin_theta + d.y * cos_theta;
      transformed_poly.push_back(p_end + rot_d);
    }

    bool overlap = false;
    for (int i = 0; i < (int)unf.faces.size(); i++) {
      const std::vector<vec2> &f = unf.faces[i].vertices;
      // If neither polygon provides a separating axis, their interiors overlap.
      if (!HasSeparatingAxis(transformed_poly, f) &&
          !HasSeparatingAxis(f, transformed_poly)) {
        overlap = true;
        break;
      }
    }

    if (overlap) {
      overlap_count++;
    }
  }

  return (double)overlap_count / unfoldings.size();
}


std::string PartialPolyhedron::DebugString() const {
  std::string s;

  AppendFormat(&s, "PartialPolyhedron: {} faces left, target {} unfoldings\n",
               num_faces_left, target_unfoldings);

  AppendFormat(&s,
               "AABB: min=({:.17g}, {:.17g}, {:.17g})\n"
               "      max=({:.17g}, {:.17g}, {:.17g})\n",
               min_vertex.x, min_vertex.y, min_vertex.z,
               max_vertex.x, max_vertex.y, max_vertex.z);

  AppendFormat(&s, "Vertices ({}):\n", vertices.size());
  for (int i = 0; i < (int)vertices.size(); i++) {
    const vec3 &v = vertices[i].pos;
    AppendFormat(&s, "  v{}: ({:.17g}, {:.17g}, {:.17g})\n",
                 i, v.x, v.y, v.z);
  }

  AppendFormat(&s, "Edges ({}):\n", edges.size());
  for (int i = 0; i < (int)edges.size(); i++) {
    const MeshEdge &e = edges[i];
    AppendFormat(&s, "  e{}: v{} -> v{} (left: {}, right: {})\n",
                 i, e.v0, e.v1, e.left_face, e.right_face);
  }

  AppendFormat(&s, "Faces ({}):\n", faces.size());
  for (int i = 0; i < (int)faces.size(); i++) {
    const MeshFace &f = faces[i];
    AppendFormat(&s, "  f{}: verts [", i);
    for (int v : f.vertices) {
      AppendFormat(&s, " {}", v);
    }
    AppendFormat(&s, " ] edges [");
    for (int e : f.edges) {
      AppendFormat(&s, " {}", e);
    }
    AppendFormat(&s, " ] plane n=({:.17g}, {:.17g}, {:.17g}) d={:.17g}\n",
                 f.plane.normal.x, f.plane.normal.y, f.plane.normal.z,
                 f.plane.d);
  }

  AppendFormat(&s, "HalfSpaces ({}):\n", half_spaces.size());
  for (int i = 0; i < (int)half_spaces.size(); i++) {
    const HalfSpace &hs = half_spaces[i];
    AppendFormat(&s, "  hs{}: n=({:.17g}, {:.17g}, {:.17g}) d={:.17g}\n",
                 i, hs.normal.x, hs.normal.y, hs.normal.z, hs.d);
  }

  AppendFormat(&s, "Unfoldings ({}):\n", unfoldings.size());
  for (int i = 0; i < (int)unfoldings.size(); i++) {
    AppendFormat(&s, "  u{}:\n", i);
    const Unfolding &unf = unfoldings[i];
    for (int j = 0; j < (int)unf.faces.size(); j++) {
      AppendFormat(&s, "    f{}:", j);
      for (const vec2 &p : unf.faces[j].vertices) {
        AppendFormat(&s, " ({:.17g}, {:.17g})", p.x, p.y);
      }
      AppendFormat(&s, "\n");
    }
  }

  return s;
}


FaceChooser::FaceChooser(
    // The feasible region. This will be on the 2D
    // segment (0, 0)-(edge_len, 0) where edge_len
    // is the length of the 3D edge.
    const std::vector<vec2> &feasible_poly,
    // The edge being extended.
    const vec3 &p0, const vec3 &p1,
    // The normal of the existing face that's being extended.
    // (the "left face" in a partial polyhedron). The dihedral
    // angle is measured from this.
    const vec3 &normal_left,
    // The dihedral angle.
    double angle,
    // The diameter of the current polyhedron,
    // which we use to limit the sampled face's size.
    double diameter) : p0(p0), p1(p1) {
  vec3 edge_dir = yocto::normalize(p1 - p0);
  vec3 outward_dir = yocto::cross(edge_dir, normal_left);

  // Calculate the normal of the plane containing the new face.
  vec3 n_new =
    std::cos(angle) * normal_left +
    std::sin(angle) * outward_dir;

  // Define a 2D local coordinate system for the new face.
  origin = p1;
  x_dir = yocto::normalize(p0 - p1);
  y_dir = yocto::cross(n_new, x_dir);

  edge_len = yocto::length(p0 - p1);

  v_top = {0.0, 0.0};
  for (int j = 0; j < (int)feasible_poly.size(); j++) {
    if (feasible_poly[j].y > v_top.y) {
      v_top = feasible_poly[j];
    }
  }
  CHECK(v_top.y > 1e-5) << "Polygon must have area in +y";

  max_dist = std::max(edge_len, diameter * MAX_DIAMETER_RATIO);
}

