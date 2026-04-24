
#ifndef _ALBRECHTFAST_ALBRECHT_H
#define _ALBRECHTFAST_ALBRECHT_H

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "bit-string.h"
#include "bounds.h"
#include "geom/polyhedra.h"
#include "hashing.h"
#include "inline-vector.h"
#include "svg.h"
#include "yocto-math.h"

// Some code for investigating Albrecht Durer's conjecture that every
// convex polyhedron has a net.
//
// Given a polyhedron (which has its faces and vertices numbered), we
// can represent a net as a tree. Each node corresponds to a face.
// For a face with n edges, its node has n children; each child is
// either empty (the edge is not unfolded) or a node containing
// that adjacent face, unfolded.
//
// Since the order of the children indicates the identity of the
// faces (an edge connects the current face to one other face), the
// children can be represented as simply a bit string. And since
// every face appears in the net, we can also canonically represent
// the entire net as a bit string: The 0th face is first, containing
// a single bit for each of its children (in order). Then we have a
// depth-first expansion of each child, in order, that is 1.
//
// An even simpler way to think about this is that we order all
// of the edges in the polyhedron, and then a net can just be
// represented as the set of all edges that are unfolded.

struct Albrecht {

  // Precomputations for a polyhedron that may make testing nets,
  // (or searching for them, etc.) more efficient.
  struct AugmentedPoly {
    Polyhedron poly;

    // For each face (in the same order as in the poly), all of
    // its edge indices. The edges appear in the same order that
    // they do in faces->v (i.e., the first edge is from v[0] to v[1])
    // but note that since the same edge appears in two faces with
    // opposite directions, Edge::v0 is not necessarily this face's
    // starting vertex.
    std::vector<InlineVector<int>> face_edges;

    // For each face (in the same order as in the poly), its 2D
    // polygon on the XY plane. A canonical pose:
    //   - Vertices are the same order as they appear in the face
    //   - The first vertex is at 0,0.
    //   - The first edge is vertical.
    // The polygon will have Cartesian CW (screen CCW) winding order.
    std::vector<std::vector<vec2>> polygons;

    // For each edge, two transformations. The first maps face1's
    // canonical 2D pose to face0's, such that the edge overlaps.
    // The second maps face0's to face1's.
    std::vector<std::pair<frame2, frame2>> edge_transforms;


    // Compute the augmented information for the polyhedron up front.
    AugmentedPoly(Polyhedron poly_in) : poly(std::move(poly_in)) {

      polygons.reserve(poly.faces->v.size());
      for (const std::vector<int> &face : poly.faces->v) {
        std::vector<vec2> poly2d;
        poly2d.reserve(face.size());

        CHECK(face.size() >= 3) << "Invalid face.";
        const vec3 &v0 = poly.vertices[face[0]];
        const vec3 &v1 = poly.vertices[face[1]];
        const vec3 &v2 = poly.vertices[face[2]];

        // 2D coordinate system for the face, placing the first
        // edge vertical.
        const vec3 ey = normalize(v1 - v0);
        const vec3 n = normalize(cross(ey, v2 - v0));
        const vec3 ex = cross(n, ey);

        for (int idx : face) {
          vec3 p = poly.vertices[idx] - v0;
          poly2d.push_back(vec2{dot(p, ex), dot(p, ey)});
        }

        polygons.push_back(std::move(poly2d));
      }

      // Compute face_edges.
      std::unordered_map<std::pair<int, int>, int,
                         Hashing<std::pair<int, int>>> edge_map;
      for (int e = 0; e < poly.faces->NumEdges(); ++e) {
        auto k1 = std::make_pair(poly.faces->edges[e].v0,
                                 poly.faces->edges[e].v1);
        auto k2 = std::make_pair(poly.faces->edges[e].v1,
                                 poly.faces->edges[e].v0);
        edge_map[k1] = e;
        edge_map[k2] = e;
      }

      face_edges.resize(poly.faces->NumFaces());
      for (int f = 0; f < poly.faces->NumFaces(); ++f) {
        const std::vector<int> &face = poly.faces->v[f];
        for (size_t i = 0; i < face.size(); ++i) {
          int v0 = face[i];
          int v1 = face[(i + 1) % face.size()];
          auto it = edge_map.find(std::make_pair(v0, v1));
          CHECK(it != edge_map.end()) << "Edge not found!";
          face_edges[f].push_back(it->second);
        }
      }

      // Compute edge transforms.
      edge_transforms.reserve(poly.faces->edges.size());
      for (const Faces::Edge &edge : poly.faces->edges) {
        // Get the index of the vertex within the face.
        auto FindInFace = [](std::span<const int> face, int v) {
          for (int i = 0; i < (int)face.size(); ++i) {
            if (face[i] == v) return i;
          }
          LOG(FATAL) << "Invalid poly: " << v << " not in face!";
          return -1;
        };

        // The 2d points in face 0.
        vec2 p0 = polygons[edge.f0][
            FindInFace(poly.faces->v[edge.f0], edge.v0)];
        vec2 p1 = polygons[edge.f0][
            FindInFace(poly.faces->v[edge.f0], edge.v1)];

        vec2 q0 = polygons[edge.f1][
            FindInFace(poly.faces->v[edge.f1], edge.v0)];
        vec2 q1 = polygons[edge.f1][
            FindInFace(poly.faces->v[edge.f1], edge.v1)];

        // Compute rigid transform mapping (src0, src1) to (dst0, dst1)
        auto Transform = [](vec2 src0, vec2 src1, vec2 dst0, vec2 dst1) {
          vec2 v_src = src1 - src0;
          vec2 v_dst = dst1 - dst0;
          double angle = std::atan2(v_dst.y, v_dst.x) -
            std::atan2(v_src.y, v_src.x);
          frame2 f = rotation_frame2(angle);
          f.o = dst0 - yocto::transform_point(f, src0);
          return f;
        };

        frame2 f1_to_f0 = Transform(q0, q1, p0, p1);
        frame2 f0_to_f1 = Transform(p0, p1, q0, q1);

        edge_transforms.push_back({f1_to_f0, f0_to_f1});
      }
    }
  };

  struct DebugResult {
    // True if the input is free from cycles.
    bool cycle_free = false;
    // If it has a cycle, an example cycle given as face indices.
    std::optional<std::vector<int>> cycle;
    // True if every face is involved and connected.
    bool is_connected = false;
    // True if the graph can be embedded in the plane without
    // overlap.
    bool is_planar = false;
    // If non-planar, an example of overlapping faces, given
    // as two face indices with f0 < f1.
    std::optional<std::pair<int, int>> overlapping_faces;

    // True if the graph is acyclic (a tree), completely connected,
    // and planar.
    bool is_net = false;
    // An SVG displaying the unfolded polyhedron.
    SVG::Doc svg;
  };

  static DebugResult DebugUnfolding(const AugmentedPoly &aug,
                                    BitStringConstView unfolding) {
    const Polyhedron &poly = aug.poly;
    const Faces &faces = *poly.faces;
    const int num_faces = faces.NumFaces();
    CHECK(unfolding.Size() == faces.NumEdges());
    DebugResult result;

    BitString visited_faces(num_faces, 0);

    struct PlacedFace {
      int face_idx = 0;
      std::vector<vec2> vertices;
    };
    std::vector<PlacedFace> laid_out_faces;
    laid_out_faces.reserve(num_faces);

    bool is_valid_tree = true;
    // We maintain a stack of the current path to extract a cycle
    // if we found one.
    std::vector<int> current_path;

    std::function<void(int, int, frame2)> DFS =
        [&](int face_idx, int parent_face, frame2 global_tf) {
          CHECK(face_idx >= 0 && face_idx < num_faces)
              << face_idx << " vs " << num_faces;

          if (visited_faces.Get(face_idx)) {
            is_valid_tree = false;
            // If we haven't recorded an example cycle yet, we can do so now.
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
          pf.face_idx = face_idx;
          pf.vertices.reserve(aug.polygons[face_idx].size());
          for (const vec2 &v : aug.polygons[face_idx]) {
            pf.vertices.push_back(yocto::transform_point(global_tf, v));
          }
          laid_out_faces.push_back(std::move(pf));

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

    BitString face_overlaps(num_faces, false);
    for (size_t i = 0; i < laid_out_faces.size(); ++i) {
      for (size_t j = i + 1; j < laid_out_faces.size(); ++j) {
        if (PolygonsOverlap(laid_out_faces[i].vertices,
                             laid_out_faces[j].vertices)) {
          int f0 = laid_out_faces[i].face_idx;
          int f1 = laid_out_faces[j].face_idx;
          if (f0 > f1) std::swap(f0, f1);
          result.overlapping_faces = std::make_pair(f0, f1);
          face_overlaps.Set(f0, true);
          face_overlaps.Set(f1, true);
          break;
        }
      }
    }

    result.is_planar = face_overlaps.Zero();
    if (!result.is_planar) {
      CHECK(result.overlapping_faces.has_value());
    }

    result.cycle_free = is_valid_tree;
    result.is_connected = (laid_out_faces.size() == (size_t)num_faces);
    result.is_net = result.cycle_free && result.is_connected &&
      result.is_planar;

    Bounds bounds;

    SVG::G group;
    group.style.stroke_color = 0x000000FF;
    group.style.fill_color = 0xCCCCFFFF;
    group.style.fill_opacity = 0.25;
    group.style.stroke_width = 2.0;

    for (const PlacedFace &pf : laid_out_faces)
      for (const vec2 &v : pf.vertices)
        bounds.Bound(v);

    Bounds::Scaler scaler = bounds.ScaleToFitWithMargin(
        1024, 1024, 32, true);


    for (const PlacedFace &pf : laid_out_faces) {
      SVG::Path path;
      for (size_t i = 0; i < pf.vertices.size(); ++i) {
        const auto &[sx, sy] = scaler.Scale(pf.vertices[i]);

        if (i == 0) {
          path.data.push_back(SVG::MoveTo{sx, sy});
        } else {
          path.data.push_back(SVG::LineTo{sx, sy});
        }
      }
      path.data.push_back(SVG::ClosePath{});

      if (face_overlaps[pf.face_idx]) {
        SVG::G error_group;
        error_group.style.fill_color = 0xFF0000FF;
        error_group.children = {SVG::Node{std::move(path)}};
        group.children.push_back(SVG::Node{std::move(error_group)});
      } else {
        group.children.push_back(SVG::Node{std::move(path)});
      }
    }

    double local_dim = std::max(bounds.Width(), bounds.Height());

    auto AddText = [&scaler](SVG::G *g,
                             // baseline (nominally the center)
                             double cx, double cy,
                             std::string_view text) {

        const auto &[scx, scy] = scaler.Scale(cx, cy);

        SVG::G e_node;
        e_node.style.transform =
          std::array<double, 6>{1.0, 0.0, 0.0, 1.0, scx, scy};
        e_node.children.push_back(SVG::Node{SVG::Text{std::string(text)}});
        g->children.push_back(SVG::Node{std::move(e_node)});
      };

    // face font size, edge font size
    double fs = local_dim * 0.025;
    double e_fs = local_dim * 0.01;

    SVG::G text_group;
    text_group.style.fill_color = 0x000000FF;
    text_group.style.fill_opacity = 1.0;
    text_group.style.stroke_color = SVG::COLOR_NONE;
    text_group.style.font_family = {"sans-serif"};
    text_group.style.font_size = scaler.SizeX() * fs;

    SVG::G edge_text_group;
    edge_text_group.style.fill_color = 0x000000FF;
    edge_text_group.style.fill_opacity = 1.0;
    edge_text_group.style.stroke_color = SVG::COLOR_NONE;
    edge_text_group.style.font_family = {"sans-serif"};
    edge_text_group.style.font_size = scaler.SizeX() * e_fs;

    for (const PlacedFace &pf : laid_out_faces) {
      int f_idx = pf.face_idx;

      vec2 center = {0.0, 0.0};
      for (const vec2 &v : pf.vertices) {
        center.x += v.x;
        center.y += v.y;
      }
      if (!pf.vertices.empty()) {
        center.x /= pf.vertices.size();
        center.y /= pf.vertices.size();
      }

      AddText(&text_group, center.x, center.y, std::format("{}", f_idx));

      for (size_t i = 0; i < pf.vertices.size(); ++i) {
        const int edge_idx = aug.face_edges[f_idx][i];
        vec2 p0 = pf.vertices[i];
        vec2 p1 = pf.vertices[(i + 1) % pf.vertices.size()];
        vec2 mid = {(p0.x + p1.x) * 0.5, (p0.y + p1.y) * 0.5};

        vec2 dir = {center.x - mid.x, center.y - mid.y};
        double len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
        if (len > 1e-6) {
          dir.x /= len;
          dir.y /= len;
        }

        // Push the edge number slightly towards the center of the face
        double offset = e_fs * 0.8;
        double ex = mid.x + dir.x * offset;
        double ey = mid.y + dir.y * offset;

        AddText(&edge_text_group, ex, ey, std::format("{}", edge_idx));
      }
    }

    group.children.push_back(SVG::Node{std::move(edge_text_group)});
    group.children.push_back(SVG::Node{std::move(text_group)});

    result.svg.root = SVG::Node{std::move(group)};

    result.svg.view_box = std::array<double, 4>{0, 0, 1024, 1024};

    return result;
  }

  static bool PolygonsOverlap(std::span<const vec2> a,
                              std::span<const vec2> b) {
    return !HasSeparatingAxis(a, b) && !HasSeparatingAxis(b, a);
  }

  // Checks if there's a separating axis parallel to edge normals of p1.
  // You need to check both (a, b) and (b, a) if you want to prove
  // two polyhedra are non-overlapping.
  static bool HasSeparatingAxis(std::span<const vec2> p1,
                                std::span<const vec2> p2) {
    for (size_t i = 0; i < p1.size(); ++i) {
      vec2 p = p1[i];
      vec2 q = p1[(i + 1) % p1.size()];
      vec2 edge = {q.x - p.x, q.y - p.y};
      double len = std::sqrt(edge.x * edge.x + edge.y * edge.y);
      if (len < 1e-9)
        continue;
      vec2 normal = {-edge.y / len, edge.x / len};

      double min1 = std::numeric_limits<double>::infinity();
      double max1 = -std::numeric_limits<double>::infinity();
      for (vec2 v : p1) {
        double d = v.x * normal.x + v.y * normal.y;
        if (d < min1) min1 = d;
        if (d > max1) max1 = d;
      }

      double min2 = std::numeric_limits<double>::infinity();
      double max2 = -std::numeric_limits<double>::infinity();
      for (vec2 v : p2) {
        double d = v.x * normal.x + v.y * normal.y;
        if (d < min2) min2 = d;
        if (d > max2) max2 = d;
      }

      if (max1 <= min2 + 1e-7 || max2 <= min1 + 1e-7) {
        return true;
      }
    }
    return false;
  }

  // Determines whether the unfolding is a valid net without producing
  // any debug information. This can assume that the unfolding is a
  // proper face-spanning tree (no cycles, no disconnected components).
  static bool IsNet(const AugmentedPoly &aug,
                    BitStringConstView unfolding) {
    const Polyhedron &poly = aug.poly;
    const Faces &faces = *poly.faces;
    const int num_faces = faces.NumFaces();
    if (num_faces <= 4) return true;

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

};


#endif
