
#include "albrecht.h"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "bit-string.h"
#include "geom/polyhedra.h"
#include "yocto-math.h"

using Aug = Albrecht::AugmentedPoly;

Albrecht::DebugResult Albrecht::DebugUnfolding(
    const Aug &aug,
    BitStringConstView unfolding) {
  const Polyhedron &poly = aug.poly;
  const Faces &faces = *poly.faces;
  const int num_faces = faces.NumFaces();
  CHECK(unfolding.Size() == faces.NumEdges());
  DebugResult result;
  result.unfolding = unfolding;

  BitString visited_faces(num_faces, 0);

  result.mesh.polygons.resize(num_faces);

  bool is_valid_tree = true;
  std::vector<int> current_path;

  std::function<void(int, int, frame2)> DFS =
      [&](int face_idx, int parent_face, frame2 global_tf) {
        CHECK(face_idx >= 0 && face_idx < num_faces)
            << face_idx << " vs " << num_faces;

        if (visited_faces.Get(face_idx)) {
          is_valid_tree = false;
          if (!result.cycle.has_value()) {
            std::vector<int> cycle;
            for (size_t i = 0; i < current_path.size(); ++i) {
              if (current_path[i] == face_idx) {
                cycle.assign(current_path.begin() + i, current_path.end());
                break;
              }
            }
            if (!cycle.empty()) {
              result.cycle = std::move(cycle);
            }
          }
          return;
        }

        visited_faces.Set(face_idx, 1);
        current_path.push_back(face_idx);

        PlacedFace pf;
        pf.v.reserve(aug.polygons[face_idx].size());
        for (size_t i = 0; i < aug.polygons[face_idx].size(); ++i) {
          int v_idx = result.mesh.vertices.size();
          result.mesh.vertices.push_back(
              yocto::transform_point(global_tf, aug.polygons[face_idx][i]));
          result.mesh.polyhedron_vertex.push_back(
              poly.faces->v[face_idx][i]);
          pf.v.push_back(v_idx);
        }
        result.mesh.polygons[face_idx] = std::move(pf);

        for (int edge_idx : aug.face_edges[face_idx]) {
          if (unfolding.Get(edge_idx)) {
            const Faces::Edge &edge = faces.edges[edge_idx];
            const auto &[f10, f01] = aug.edge_transforms[edge_idx];

            int next_face = (edge.f0 == face_idx) ? edge.f1 : edge.f0;
            frame2 tf = (edge.f0 == face_idx) ? f10 : f01;

            if (next_face != parent_face) {
              DFS(next_face, face_idx, global_tf * tf);
            }
          }
        }

        current_path.pop_back();
      };

  frame2 identity = {{1.0, 0.0}, {0.0, 1.0}, {0.0, 0.0}};
  if (num_faces > 0) {
    DFS(0, -1, identity);
  }

  std::vector<std::vector<vec2>> face_vertices(num_faces);
  for (int i = 0; i < num_faces; ++i) {
    face_vertices[i].reserve(result.mesh.polygons[i].v.size());
    for (int idx : result.mesh.polygons[i].v) {
      face_vertices[i].push_back(result.mesh.vertices[idx]);
    }
  }

  bool has_overlap = false;
  result.face_overlap.resize(num_faces, -1);
  for (int i = 0; i < num_faces; ++i) {
    if (face_vertices[i].empty()) continue;
    for (int j = i + 1; j < num_faces; ++j) {
      if (face_vertices[j].empty()) continue;
      if (PolygonsOverlap(face_vertices[i], face_vertices[j])) {
        result.face_overlap[i] = j;
        result.face_overlap[j] = i;
        has_overlap = true;
      }
    }
  }

  result.is_planar = !has_overlap;

  int visited_count = 0;
  for (int i = 0; i < num_faces; ++i) {
    if (!result.mesh.polygons[i].v.empty()) {
      visited_count++;
    }
  }

  result.cycle_free = is_valid_tree;
  result.is_connected = (visited_count == num_faces);
  result.is_net = result.cycle_free && result.is_connected &&
    result.is_planar;

  return result;
}


bool Albrecht::IsNet(const AugmentedPoly &aug,
                     BitStringConstView unfolding) {
  const Polyhedron &poly = aug.poly;
  const Faces &faces = *poly.faces;
  const int num_faces = faces.NumFaces();
  if (num_faces <= 3) return true;

  int total_vertices = 0;
  for (const auto &poly2d : aug.polygons) total_vertices += poly2d.size();

  struct PlacedFace {
    int offset = 0;
    int count = 0;
    vec2 min_b{}, max_b{};
  };

  std::vector<PlacedFace> placed;
  placed.reserve(num_faces);

  std::vector<vec2> all_vertices;
  all_vertices.reserve(total_vertices);

  struct StackNode {
    int face_idx = 0;
    int parent_face = 0;
    frame2 tf{};
  };

  std::vector<StackNode> stack;
  stack.reserve(num_faces);

  frame2 identity = {{1.0, 0.0}, {0.0, 1.0}, {0.0, 0.0}};
  stack.push_back({0, -1, identity});

  while (!stack.empty()) {
    StackNode node = stack.back();
    stack.pop_back();

    int face_idx = node.face_idx;
    const std::vector<vec2> &poly2d = aug.polygons[face_idx];

    int offset = all_vertices.size();
    int count = poly2d.size();

    vec2 min_b = {std::numeric_limits<double>::infinity(),
                  std::numeric_limits<double>::infinity()};
    vec2 max_b = {-std::numeric_limits<double>::infinity(),
                  -std::numeric_limits<double>::infinity()};

    for (const vec2 &v : poly2d) {
      vec2 tv = yocto::transform_point(node.tf, v);
      all_vertices.push_back(tv);
      if (tv.x < min_b.x) min_b.x = tv.x;
      if (tv.y < min_b.y) min_b.y = tv.y;
      if (tv.x > max_b.x) max_b.x = tv.x;
      if (tv.y > max_b.y) max_b.y = tv.y;
    }

    std::span<const vec2> current_poly(&all_vertices[offset], count);

    for (const PlacedFace &pf : placed) {
      // First, quick AABB test.
      if (max_b.x <= pf.min_b.x + 1e-7 || min_b.x >= pf.max_b.x - 1e-7 ||
          max_b.y <= pf.min_b.y + 1e-7 || min_b.y >= pf.max_b.y - 1e-7) {
        continue;
      }

      std::span<const vec2> other_poly(&all_vertices[pf.offset], pf.count);

      // Precise overlap test.
      if (PolygonsOverlap(current_poly, other_poly)) {
        return false;
      }
    }

    placed.push_back({offset, count, min_b, max_b});

    for (int edge_idx : aug.face_edges[face_idx]) {
      if (unfolding.Get(edge_idx)) {
        const Faces::Edge &edge = faces.edges[edge_idx];
        int next_face = (edge.f0 == face_idx) ? edge.f1 : edge.f0;

        if (next_face != node.parent_face) {
          const auto &[f10, f01] = aug.edge_transforms[edge_idx];
          frame2 edge_tf = (edge.f0 == face_idx) ? f10 : f01;
          stack.push_back({next_face, face_idx, node.tf * edge_tf});
        }
      }
    }
  }

  return true;
}


Albrecht::Stretch Albrecht::StretchFactor(const AugmentedPoly &aug,
                                          BitStringConstView unfolding) {
  const Polyhedron &poly = aug.poly;
  const Faces &faces = *poly.faces;
  const int num_faces = faces.NumFaces();

  std::vector<std::vector<int>> adj_3d(num_faces);
  std::vector<std::vector<int>> adj_2d(num_faces);

  for (int i = 0; i < num_faces; ++i) {
    for (int edge_idx : aug.face_edges[i]) {
      const Faces::Edge &edge = faces.edges[edge_idx];
      int next_face = (edge.f0 == i) ? edge.f1 : edge.f0;
      adj_3d[i].push_back(next_face);
      if (unfolding.Get(edge_idx)) {
        adj_2d[i].push_back(next_face);
      }
    }
  }

  Stretch max_stretch;
  double max_ratio = -1.0;

  std::vector<int> dist_3d(num_faces);
  std::vector<int> dist_2d(num_faces);
  std::vector<int> q;
  q.reserve(num_faces);

  for (int start = 0; start < num_faces; ++start) {
    std::fill(dist_3d.begin(), dist_3d.end(), -1);
    std::fill(dist_2d.begin(), dist_2d.end(), -1);

    // BFS 3D
    q.clear();
    q.push_back(start);
    dist_3d[start] = 0;
    size_t head = 0;
    while (head < q.size()) {
      int curr = q[head++];
      for (int nxt : adj_3d[curr]) {
        if (dist_3d[nxt] == -1) {
          dist_3d[nxt] = dist_3d[curr] + 1;
          q.push_back(nxt);
        }
      }
    }

    // BFS 2D
    q.clear();
    q.push_back(start);
    dist_2d[start] = 0;
    head = 0;
    while (head < q.size()) {
      int curr = q[head++];
      for (int nxt : adj_2d[curr]) {
        if (dist_2d[nxt] == -1) {
          dist_2d[nxt] = dist_2d[curr] + 1;
          q.push_back(nxt);
        }
      }
    }

    // Check stretch against max for all pairs (f0 < f1)
    for (int i = start + 1; i < num_faces; ++i) {
      if (dist_3d[i] > 0 && dist_2d[i] > 0) {
        double ratio = static_cast<double>(dist_2d[i]) / dist_3d[i];
        if (ratio > max_ratio) {
          max_ratio = ratio;
          max_stretch.f0 = start;
          max_stretch.f1 = i;
          max_stretch.distance_3d = dist_3d[i];
          max_stretch.unfolded_distance = dist_2d[i];
        }
      }
    }
  }

  return max_stretch;
}


double Albrecht::ShapePenalty(const Albrecht::AugmentedPoly &aug) {
  double penalty = 0.0;
  for (const std::vector<vec2> &poly2d : aug.polygons) {
    double area = std::abs(SignedAreaOfConvexPoly(poly2d));
    double perimeter = 0.0;
    for (size_t i = 0; i < poly2d.size(); i++) {
      vec2 p_prev = poly2d[(i + poly2d.size() - 1) % poly2d.size()];
      vec2 p0 = poly2d[i];
      vec2 p1 = poly2d[(i + 1) % poly2d.size()];

      vec2 e1 = p0 - p_prev;
      vec2 e2 = p1 - p0;
      double len1 = yocto::length(e1);
      double len2 = yocto::length(e2);

      perimeter += len2;

      if (len1 > 1e-9 && len2 > 1e-9) {
        double cross = (e1.x * e2.y - e1.y * e2.x) / (len1 * len2);
        double abs_sin = std::abs(cross);
        // Cubic penalty as the angle approaches 0 or 180 degrees.
        penalty += 1.0 / std::max(abs_sin * abs_sin * abs_sin, 1e-9);
      } else {
        penalty += 1e9;
      }
    }

    if (area > 1e-9) {
      penalty += (perimeter * perimeter) / area;
    } else {
      penalty += 1e9;
    }
  }

  return penalty;
}

