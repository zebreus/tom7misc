
#include "albrecht.h"
#include "bit-string.h"
#include "bounds.h"
#include "geom/polyhedra.h"
#include "svg.h"
#include "yocto-math.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <functional>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using Aug = Albrecht::AugmentedPoly;

Albrecht::DebugResult Albrecht::DebugUnfolding(
    const Aug &aug,
    BitStringConstView unfolding) {
  const Polyhedron &poly = aug.poly;
  const Faces &faces = *poly.faces;
  const int num_faces = faces.NumFaces();
  CHECK(unfolding.Size() == faces.NumEdges());
  DebugResult result;

  BitString visited_faces(num_faces, 0);

  result.placed_faces.reserve(num_faces);

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
        result.placed_faces.push_back(std::move(pf));

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
  for (size_t i = 0; i < result.placed_faces.size(); ++i) {
    for (size_t j = i + 1; j < result.placed_faces.size(); ++j) {
      if (PolygonsOverlap(result.placed_faces[i].vertices,
                           result.placed_faces[j].vertices)) {
        int f0 = result.placed_faces[i].face_idx;
        int f1 = result.placed_faces[j].face_idx;
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
  result.is_connected = (result.placed_faces.size() == (size_t)num_faces);
  result.is_net = result.cycle_free && result.is_connected &&
    result.is_planar;

  Bounds bounds;

  SVG::G group;
  group.style.stroke_color = 0x000000FF;
  group.style.fill_color = 0xCCCCFFFF;
  group.style.fill_opacity = 0.25;
  group.style.stroke_width = 2.0;

  for (const PlacedFace &pf : result.placed_faces)
    for (const vec2 &v : pf.vertices)
      bounds.Bound(v);

  Bounds::Scaler scaler = bounds.ScaleToFitWithMargin(
      1024, 1024, 32, true);


  for (const PlacedFace &pf : result.placed_faces) {
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

  for (const PlacedFace &pf : result.placed_faces) {
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

bool Albrecht::IsNet(const AugmentedPoly &aug,
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
