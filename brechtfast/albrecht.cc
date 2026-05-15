
#include "albrecht.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/print.h"
#include "bit-string.h"
#include "bounds.h"
#include "dirty.h"
#include "geom/polyhedra.h"
#include "image.h"
#include "svg.h"
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

static constexpr int WIDTH = 1024, HEIGHT = 1024;

struct Insert {
  vec2 poly_min, poly_max;
  vec2 screen_min, screen_max;
};

struct Label {
  int id = -1;
  vec2 pos;
  // Size is in native polygon space.
  double size = 0.0;
  // If empty, the label is drawn at normal scale in the main drawing.
  // If non-empty, it should be skipped from the main drawing, and
  // instead rendered in the inserts (given by their indices).
  std::vector<int> inserts;
};

struct LayoutPlan {
  Bounds::Scaler scaler;
  std::vector<Insert> inserts;
  std::vector<Label> poly_labels;
  std::vector<Label> edge_labels;
};

static constexpr int CELL_SIZE = 128;
static_assert(WIDTH % CELL_SIZE == 0 && HEIGHT % CELL_SIZE == 0);
static constexpr int CELLSW = WIDTH / CELL_SIZE;
static constexpr int CELLSH = HEIGHT / CELL_SIZE;

// Constants controlling complexity thresholds and insert placement
static constexpr double COMPLEXITY_THRESHOLD = 15.0;
static constexpr double INSERT_ZOOM_FACTOR = 3.0;
static constexpr double OUTSIDE_PENALTY = 1000.0;
static constexpr double NEARNESS_PENALTY = 500.0;


static LayoutPlan MakePlan(const Albrecht::AugmentedPoly &aug,
                           const Albrecht::DebugResult &dr,
                           bool include_inserts,
                           bool include_labels) {
  LayoutPlan plan;

  Bounds bounds;
  for (const vec2 &v : dr.mesh.vertices) {
    bounds.Bound(v);
  }

  if (bounds.Empty()) {
    return plan;
  }

  // Initial transform from polygon space to the SVG
  // space, which is "screen coordinates" from
  // (0,0) to (WIDTH, HEIGHT). Note that if we place
  // an insert outside of the current bounds, we will
  // need to rescale.
  plan.scaler = bounds.ScaleToFitWithMargin(WIDTH, HEIGHT, 32, true);


  static constexpr int DIRTY_SCALE = 2;
  // The "dirty" buffer, which tells us which parts of the image are
  // used. This is a raster using integer coordinates. We use the same
  // scaler to transform between the polygon space and the SVG space
  // here.
  std::unique_ptr<Dirty> dirty(new Dirty(WIDTH, HEIGHT, DIRTY_SCALE));

  const double poly_local_dim = std::max(bounds.Width(), bounds.Height());

  struct Candidate {
    vec2 poly_min, poly_max;
  };

  struct Placed {
    Candidate c;
    vec2 unscaled_screen_min;
    vec2 unscaled_screen_max;
  };
  std::vector<Placed> placed_inserts;

  if (include_inserts) {
    // We also keep track of a discretized measure of image complexity,
    // which is a function of the vertices and edges that are in the
    // cell (mostly vertices).
    ImageF complexity(CELLSW, CELLSH);
    complexity.Clear(0.0);

    for (const vec2 &v : dr.mesh.vertices) {
      auto [sx, sy] = plan.scaler.Scale(v);
      int cx = std::clamp((int)(sx / CELL_SIZE), 0, CELLSW - 1);
      int cy = std::clamp((int)(sy / CELL_SIZE), 0, CELLSH - 1);
      complexity.SetPixel(cx, cy, complexity.GetPixel(cx, cy) + 1.0f);
    }

    Image1 visited(CELLSW, CELLSH);
    visited.Clear(false);
    std::vector<IntBounds> complex_regions;
    for (int cy = 0; cy < CELLSH; ++cy) {
      for (int cx = 0; cx < CELLSW; ++cx) {
        if (complexity.GetPixel(cx, cy) >= COMPLEXITY_THRESHOLD &&
            !visited.GetPixel(cx, cy)) {
          IntBounds b;
          std::vector<std::pair<int, int>> stack = {{cx, cy}};
          visited.SetPixel(cx, cy, true);
          while (!stack.empty()) {
            auto [x, y] = stack.back();
            stack.pop_back();
            b.Bound(x, y);
            for (int dy = -1; dy <= 1; ++dy) {
              for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue;
                int nx = x + dx, ny = y + dy;
                if (nx >= 0 && nx < CELLSW && ny >= 0 && ny < CELLSH) {
                  if (complexity.GetPixel(nx, ny) >= COMPLEXITY_THRESHOLD &&
                      !visited.GetPixel(nx, ny)) {
                    visited.SetPixel(nx, ny, true);
                    stack.push_back({nx, ny});
                  }
                }
              }
            }
          }
          complex_regions.push_back(b);
        }
      }
    }

    Print("Got {} complex regions\n", complex_regions.size());


    std::vector<Candidate> candidates;

    for (const IntBounds &cb : complex_regions) {
      double s_minx = cb.MinX() * CELL_SIZE;
      double s_miny = cb.MinY() * CELL_SIZE;
      double s_maxx = (cb.MaxX() + 1) * CELL_SIZE;
      double s_maxy = (cb.MaxY() + 1) * CELL_SIZE;

      auto [minx_u, miny_u] = plan.scaler.Unscale(s_minx, s_miny);
      auto [maxx_u, maxy_u] = plan.scaler.Unscale(s_maxx, s_maxy);

      Candidate c;
      c.poly_min = {std::min(minx_u, maxx_u), std::min(miny_u, maxy_u)};
      c.poly_max = {std::max(minx_u, maxx_u), std::max(miny_u, maxy_u)};
      candidates.push_back(c);
    }

    Print("Got {} candidates.\n", candidates.size());

    // If any of them is larger than half the image, this will be
    // pointless. Suppress them all.
    double graph_area = bounds.Width() * bounds.Height();
    for (const Candidate &c : candidates) {
      double cand_area = (c.poly_max.x - c.poly_min.x) *
                         (c.poly_max.y - c.poly_min.y);
      if (cand_area > 0.5 * graph_area) {
        candidates.clear();
        Print("... But they are too big!\n");
        break;
      }
    }

    for (const Candidate &c : candidates) {
      auto RecalcDirty = [&]() {
          Print("Recalc...\n");
          dirty.reset(new Dirty(WIDTH, HEIGHT, DIRTY_SCALE));
        for (size_t f_idx = 0; f_idx < dr.mesh.polygons.size(); ++f_idx) {
          const auto &pf = dr.mesh.polygons[f_idx];
          if (pf.v.empty()) continue;
          Bounds fb;
          for (int v_idx : pf.v) {
            fb.Bound(plan.scaler.Scale(dr.mesh.vertices[v_idx]));
          }
          if (!fb.Empty()) {
            dirty->MarkUsed((int)fb.MinX(), (int)fb.MinY(),
                            (int)fb.Width(), (int)fb.Height());
          }
        }
        for (const Placed &p : placed_inserts) {
          auto [sx0, sy0] = plan.scaler.Scale(p.unscaled_screen_min);
          auto [sx1, sy1] = plan.scaler.Scale(p.unscaled_screen_max);
          double sx = std::min(sx0, sx1);
          double sy = std::min(sy0, sy1);
          double sw = std::abs(sx1 - sx0);
          double sh = std::abs(sy1 - sy0);
          dirty->MarkUsed((int)sx, (int)sy, (int)sw, (int)sh);
        }
      };

      RecalcDirty();
      Print("Recalculated.\n");

      double ins_w = (c.poly_max.x - c.poly_min.x) * plan.scaler.SizeX() *
                     INSERT_ZOOM_FACTOR;
      double ins_h = (c.poly_max.y - c.poly_min.y) * plan.scaler.SizeY() *
                     INSERT_ZOOM_FACTOR;
      int iw = std::max(1, (int)std::ceil(ins_w));
      int ih = std::max(1, (int)std::ceil(ins_h));

      vec2 center = {(c.poly_max.x + c.poly_min.x) * 0.5,
                     (c.poly_max.y + c.poly_min.y) * 0.5};
      auto [f_srcx, f_srcy] = plan.scaler.Scale(center);

      Print("FindEmptySpace with src={},{} {}x{}, out {}, near {}\n",
            (int)f_srcx, (int)f_srcy, iw, ih,
            OUTSIDE_PENALTY, NEARNESS_PENALTY);
      auto [px, py] = dirty->FindEmptySpace(
          (int)f_srcx, (int)f_srcy, iw, ih,
          OUTSIDE_PENALTY, NEARNESS_PENALTY);
      Print("Found at {},{}\n", px, py);

      Placed p;
      p.c = c;
      auto [uminx, uminy] = plan.scaler.Unscale(px, py);
      p.unscaled_screen_min = {uminx, uminy};
      auto [umaxx, umaxy] = plan.scaler.Unscale(px + iw, py + ih);
      p.unscaled_screen_max = {umaxx, umaxy};
      placed_inserts.push_back(p);

      if (px < 0 || py < 0 || px + iw > WIDTH || py + ih > HEIGHT) {
        bounds.Bound(p.unscaled_screen_min);
        bounds.Bound(p.unscaled_screen_max);
        plan.scaler = bounds.ScaleToFitWithMargin(WIDTH, HEIGHT, 32, true);
      }
    }

    Print("Finished candidates loop.\n");

    for (const Placed &p : placed_inserts) {
      Insert ins;
      ins.poly_min = p.c.poly_min;
      ins.poly_max = p.c.poly_max;

      auto [sx0, sy0] = plan.scaler.Scale(p.unscaled_screen_min);
      auto [sx1, sy1] = plan.scaler.Scale(p.unscaled_screen_max);

      ins.screen_min = {std::min(sx0, sx1), std::min(sy0, sy1)};
      ins.screen_max = {std::max(sx0, sx1), std::max(sy0, sy1)};

      plan.inserts.push_back(ins);
    }
    Print("Placed inserts.\n");
  }

  struct EdgePlacement {
    int f_idx;
    vec2 p0, p1;
    vec2 mid;
    vec2 out_dir;
  };
  std::vector<std::vector<EdgePlacement>> edge_placements(
      aug.poly.faces->NumEdges());

  for (size_t f_idx = 0; f_idx < dr.mesh.polygons.size(); ++f_idx) {
    const auto &pf = dr.mesh.polygons[f_idx];
    if (pf.v.empty()) continue;

    vec2 center = {0.0, 0.0};
    for (size_t i = 0; i < pf.v.size(); ++i) {
      const vec2 &v = dr.mesh.vertices[pf.v[i]];
      center.x += v.x;
      center.y += v.y;
    }
    center.x /= pf.v.size();
    center.y /= pf.v.size();

    double min_dist = std::numeric_limits<double>::infinity();
    for (size_t i = 0; i < pf.v.size(); ++i) {
      int edge_idx = aug.face_edges[f_idx][i];
      vec2 p0 = dr.mesh.vertices[pf.v[i]];
      vec2 p1 = dr.mesh.vertices[pf.v[(i + 1) % pf.v.size()]];
      vec2 mid = {(p0.x + p1.x) * 0.5, (p0.y + p1.y) * 0.5};

      vec2 edge_v = {p1.x - p0.x, p1.y - p0.y};
      double len = std::hypot(edge_v.x, edge_v.y);
      vec2 out_dir = {0.0, 0.0};
      if (len > 1e-9) {
        out_dir = {-edge_v.y / len, edge_v.x / len};
        if ((out_dir.x * (mid.x - center.x) +
             out_dir.y * (mid.y - center.y)) < 0) {
          out_dir.x = -out_dir.x;
          out_dir.y = -out_dir.y;
        }
        double d = std::abs((center.x - p0.x) * out_dir.x +
                            (center.y - p0.y) * out_dir.y);
        if (d < min_dist) min_dist = d;
      } else {
        out_dir = {mid.x - center.x, mid.y - center.y};
        double olen = std::hypot(out_dir.x, out_dir.y);
        if (olen > 1e-9) {
          out_dir.x /= olen;
          out_dir.y /= olen;
        }
      }

      edge_placements[edge_idx].push_back({(int)f_idx, p0, p1, mid, out_dir});
    }

    double chars = std::format("{}", f_idx).size();
    double limit_w = (min_dist * 1.8) / std::max(1.0, chars * 0.6);
    double limit_h = min_dist * 1.8;
    double poly_fs = std::min(limit_w, limit_h);

    Label lbl;
    lbl.id = f_idx;
    lbl.pos = center;
    lbl.size = poly_fs;
    plan.poly_labels.push_back(lbl);
  }

  double e_fs = poly_local_dim * 0.01;
  for (int edge_idx = 0; edge_idx < (int)edge_placements.size(); ++edge_idx) {
    const auto &placements = edge_placements[edge_idx];
    if (placements.empty()) continue;

    bool is_cut = false;
    if (placements.size() == 1) {
      is_cut = true;
    } else if (placements.size() == 2) {
      double dx = placements[0].mid.x - placements[1].mid.x;
      double dy = placements[0].mid.y - placements[1].mid.y;
      if (std::hypot(dx, dy) > 1e-5) {
        is_cut = true;
      }
    }

    if (is_cut) {
      for (const auto &pl : placements) {
        double offset = e_fs * 0.8;
        double ex = pl.mid.x + pl.out_dir.x * offset;
        double ey = pl.mid.y + pl.out_dir.y * offset;

        Label lbl;
        lbl.id = edge_idx;
        lbl.pos = {ex, ey};
        lbl.size = e_fs;
        plan.edge_labels.push_back(lbl);
      }
    }
  }

  Print("Placed edges.\n");

  auto AssignToInserts = [&](std::vector<Label> &labels) {
      for (Label &lbl : labels) {
        for (size_t i = 0; i < plan.inserts.size(); ++i) {
          const Insert &ins = plan.inserts[i];
          if (lbl.pos.x >= ins.poly_min.x && lbl.pos.x <= ins.poly_max.x &&
              lbl.pos.y >= ins.poly_min.y && lbl.pos.y <= ins.poly_max.y) {
            lbl.inserts.push_back(i);
          }
        }
      }
    };
  AssignToInserts(plan.poly_labels);
  AssignToInserts(plan.edge_labels);

  return plan;
}


SVG::Doc Albrecht::MakeSVG(const AugmentedPoly &aug,
                           const DebugResult &dr,
                           bool inserts,
                           bool labels) {
  SVG::Doc doc;

  Bounds bounds;
  for (const vec2 &v : dr.mesh.vertices) {
    bounds.Bound(v);
  }

  if (dr.mesh.vertices.empty()) {
    doc.view_box = std::array<double, 4>{0, 0, WIDTH, HEIGHT};
    doc.root = SVG::Node{SVG::G{}};
    return doc;
  }

  LayoutPlan plan = MakePlan(aug, dr, inserts, labels);
  Print("Got LayoutPlan.\n");

  auto DrawMesh = [&](const Bounds::Scaler &scaler, int insert_idx,
                      double zoom) {
      // The clip rectangle that will be applied. When there's
      // no insert, this is just the whole canvas.
      vec2 clip_min = {0.0, 0.0};
      vec2 clip_max = {WIDTH, HEIGHT};

      if (insert_idx >= 0) {
        clip_min = plan.inserts[insert_idx].poly_min;
        clip_max = plan.inserts[insert_idx].poly_max;
      }

      SVG::G group;

      SVG::G poly_group;
      poly_group.style.fill_color = 0xCCCCFFFF;
      poly_group.style.fill_opacity = 0.25;
      poly_group.style.stroke_color = SVG::COLOR_NONE;

      SVG::G edges_group;
      edges_group.style.stroke_color = 0x000000FF;
      edges_group.style.stroke_width = 1.0 / zoom;
      edges_group.style.fill_color = SVG::COLOR_NONE;

      SVG::G verts_group;
      verts_group.style.fill_color = 0x000080FF;
      verts_group.style.stroke_color = SVG::COLOR_NONE;

      SVG::G text_group;
      text_group.style.fill_color = 0x000000FF;
      text_group.style.fill_opacity = 1.0;
      text_group.style.stroke_color = SVG::COLOR_NONE;
      text_group.style.font_family = {"sans-serif"};
      text_group.style.text_anchor = SVG::TextAnchor::MIDDLE;

      SVG::G edge_text_group;
      edge_text_group.style.fill_color = 0x000000FF;
      edge_text_group.style.fill_opacity = 1.0;
      edge_text_group.style.stroke_color = SVG::COLOR_NONE;
      edge_text_group.style.font_family = {"sans-serif"};
      edge_text_group.style.text_anchor = SVG::TextAnchor::MIDDLE;

      double c_minx = -5.0, c_miny = -5.0;
      double c_maxx = WIDTH + 5.0, c_maxy = HEIGHT + 5.0;
      if (insert_idx >= 0) {
        auto [sx0, sy0] = scaler.Scale(clip_min);
        auto [sx1, sy1] = scaler.Scale(clip_max);
        c_minx = std::min(sx0, sx1) - 5.0;
        c_maxx = std::max(sx0, sx1) + 5.0;
        c_miny = std::min(sy0, sy1) - 5.0;
        c_maxy = std::max(sy0, sy1) + 5.0;
      }

      auto AddText = [&](SVG::G *g, double cx, double cy,
                         std::string_view text, double font_size) {
        const auto &[scx, scy] = scaler.Scale(cx, cy);
        SVG::G e_node;
        const double baseline_fudge = font_size * 0.4;
        e_node.style.transform =
            std::array<double, 6>{1.0, 0.0, 0.0, 1.0, scx, scy + baseline_fudge};
        e_node.style.font_size = font_size;
        e_node.children.push_back(SVG::Node{SVG::Text{std::string(text)}});
        g->children.push_back(SVG::Node{std::move(e_node)});
      };

      SVG::Path mesh_edges_path;
      for (size_t f_idx = 0; f_idx < dr.mesh.polygons.size(); ++f_idx) {
        const auto &pf = dr.mesh.polygons[f_idx];
        if (pf.v.empty()) continue;

        double p_minx = std::numeric_limits<double>::infinity();
        double p_miny = std::numeric_limits<double>::infinity();
        double p_maxx = -std::numeric_limits<double>::infinity();
        double p_maxy = -std::numeric_limits<double>::infinity();

        for (size_t i = 0; i < pf.v.size(); ++i) {
          const auto &[sx, sy] = scaler.Scale(dr.mesh.vertices[pf.v[i]]);
          if (sx < p_minx) p_minx = sx;
          if (sy < p_miny) p_miny = sy;
          if (sx > p_maxx) p_maxx = sx;
          if (sy > p_maxy) p_maxy = sy;
        }

        if (p_maxx < c_minx || p_minx > c_maxx ||
            p_maxy < c_miny || p_miny > c_maxy) {
          continue;
        }

        SVG::Path path;
        for (size_t i = 0; i < pf.v.size(); ++i) {
          const vec2 &v0 = dr.mesh.vertices[pf.v[i]];
          const vec2 &v1 = dr.mesh.vertices[pf.v[(i + 1) % pf.v.size()]];
          const auto &[sx0, sy0] = scaler.Scale(v0);
          const auto &[sx1, sy1] = scaler.Scale(v1);

          if (i == 0) {
            path.data.push_back(SVG::MoveTo{sx0, sy0});
          } else {
            path.data.push_back(SVG::LineTo{sx0, sy0});
          }

          mesh_edges_path.data.push_back(SVG::MoveTo{sx0, sy0});
          mesh_edges_path.data.push_back(SVG::LineTo{sx1, sy1});
        }
        path.data.push_back(SVG::ClosePath{});

        if (dr.face_overlap[f_idx] != -1) {
          SVG::G error_group;
          error_group.style.fill_color = 0xFF0000FF;
          error_group.children = {SVG::Node{std::move(path)}};
          poly_group.children.push_back(SVG::Node{std::move(error_group)});
        } else {
          poly_group.children.push_back(SVG::Node{std::move(path)});
        }
      }
      edges_group.children.push_back(SVG::Node{std::move(mesh_edges_path)});

      SVG::Path verts_path;
      double r = 1.2 / zoom;
      double k = r * 0.5522847498;
      for (const vec2 &v : dr.mesh.vertices) {
        const auto &[sx, sy] = scaler.Scale(v);
        if (sx < c_minx || sx > c_maxx || sy < c_miny || sy > c_maxy) {
          continue;
        }
        verts_path.data.push_back(SVG::MoveTo{sx + r, sy});
        verts_path.data.push_back(
            SVG::CubicBezier{sx + r, sy + k, sx + k, sy + r, sx, sy + r});
        verts_path.data.push_back(
            SVG::CubicBezier{sx - k, sy + r, sx - r, sy + k, sx - r, sy});
        verts_path.data.push_back(
            SVG::CubicBezier{sx - r, sy - k, sx - k, sy - r, sx, sy - r});
        verts_path.data.push_back(
            SVG::CubicBezier{sx + k, sy - r, sx + r, sy - k, sx + r, sy});
        verts_path.data.push_back(SVG::ClosePath{});
      }
      verts_group.children.push_back(SVG::Node{std::move(verts_path)});

      if (labels) {
        for (const Label &lbl : plan.poly_labels) {
          bool draw = false;
          if (insert_idx == -1) {
            draw = lbl.inserts.empty();
          } else {
            draw = std::find(lbl.inserts.begin(), lbl.inserts.end(),
                             insert_idx) != lbl.inserts.end();
          }
          if (draw) {
            double scaled_fs = std::max(1.0, lbl.size * scaler.SizeX());
            AddText(&text_group, lbl.pos.x, lbl.pos.y,
                    std::format("{}", lbl.id), scaled_fs);
          }
        }

        for (const Label &lbl : plan.edge_labels) {
          bool draw = false;
          if (insert_idx == -1) {
            draw = lbl.inserts.empty();
          } else {
            draw = std::find(lbl.inserts.begin(), lbl.inserts.end(),
                             insert_idx) != lbl.inserts.end();
          }
          if (draw) {
            double scaled_fs = std::max(1.0, lbl.size * scaler.SizeX());
            AddText(&edge_text_group, lbl.pos.x, lbl.pos.y,
                    std::format("{}", lbl.id), scaled_fs);
          }
        }
      }

      group.children.push_back(SVG::Node{std::move(poly_group)});
      group.children.push_back(SVG::Node{std::move(edges_group)});
      group.children.push_back(SVG::Node{std::move(verts_group)});
      group.children.push_back(SVG::Node{std::move(edge_text_group)});
      group.children.push_back(SVG::Node{std::move(text_group)});

      return group;
    };

  SVG::G root_group;

  SVG::G main_img = DrawMesh(plan.scaler, -1, 1.0);
  root_group.children.push_back(SVG::Node{std::move(main_img)});

  for (size_t i = 0; i < plan.inserts.size(); ++i) {
    const Insert &ins = plan.inserts[i];

    auto [psx0, psy0] = plan.scaler.Scale(ins.poly_min);
    auto [psx1, psy1] = plan.scaler.Scale(ins.poly_max);

    double min_px = std::min(psx0, psx1);
    double max_px = std::max(psx0, psx1);
    double min_py = std::min(psy0, psy1);
    double max_py = std::max(psy0, psy1);

    double min_ix = ins.screen_min.x;
    double max_ix = ins.screen_max.x;
    double min_iy = ins.screen_min.y;
    double max_iy = ins.screen_max.y;

    double iw = max_ix - min_ix;
    double ih = max_iy - min_iy;

    double pw = max_px - min_px;
    double ph = max_py - min_py;

    if (pw < 1e-5 || ph < 1e-5) continue;

    double scale_x = iw / pw;
    double scale_y = ih / ph;

    double tx = min_ix - min_px * scale_x;
    double ty = min_iy - min_py * scale_y;

    SVG::Path conn_path;
    conn_path.data.push_back(SVG::MoveTo{min_px, min_py});
    conn_path.data.push_back(SVG::LineTo{min_ix, min_iy});
    conn_path.data.push_back(SVG::MoveTo{max_px, min_py});
    conn_path.data.push_back(SVG::LineTo{max_ix, min_iy});
    conn_path.data.push_back(SVG::MoveTo{min_px, max_py});
    conn_path.data.push_back(SVG::LineTo{min_ix, max_iy});
    conn_path.data.push_back(SVG::MoveTo{max_px, max_py});
    conn_path.data.push_back(SVG::LineTo{max_ix, max_iy});

    SVG::G conn_group;
    conn_group.style.stroke_color = 0x000000FF;
    conn_group.style.stroke_opacity = 0.15;
    conn_group.style.stroke_width = 1.0;
    conn_group.style.stroke_dasharray = std::vector<double>{4.0, 4.0};
    conn_group.children.push_back(SVG::Node{std::move(conn_path)});
    root_group.children.push_back(SVG::Node{std::move(conn_group)});

    SVG::Path src_rect;
    src_rect.data.push_back(SVG::MoveTo{min_px, min_py});
    src_rect.data.push_back(SVG::LineTo{max_px, min_py});
    src_rect.data.push_back(SVG::LineTo{max_px, max_py});
    src_rect.data.push_back(SVG::LineTo{min_px, max_py});
    src_rect.data.push_back(SVG::ClosePath{});

    SVG::G src_group;
    src_group.style.stroke_color = 0x000000FF;
    src_group.style.stroke_opacity = 0.3;
    src_group.style.stroke_width = 1.0;
    src_group.style.fill_color = SVG::COLOR_NONE;
    src_group.style.stroke_dasharray = std::vector<double>{4.0, 4.0};
    src_group.children.push_back(SVG::Node{std::move(src_rect)});
    root_group.children.push_back(SVG::Node{std::move(src_group)});

    SVG::Path shadow_path;
    shadow_path.data.push_back(SVG::MoveTo{min_ix + 4, min_iy + 4});
    shadow_path.data.push_back(SVG::LineTo{max_ix + 4, min_iy + 4});
    shadow_path.data.push_back(SVG::LineTo{max_ix + 4, max_iy + 4});
    shadow_path.data.push_back(SVG::LineTo{min_ix + 4, max_iy + 4});
    shadow_path.data.push_back(SVG::ClosePath{});

    SVG::G shadow_group;
    shadow_group.style.fill_color = 0x000000FF;
    shadow_group.style.fill_opacity = 0.15;
    shadow_group.style.stroke_color = SVG::COLOR_NONE;
    shadow_group.children.push_back(SVG::Node{std::move(shadow_path)});
    root_group.children.push_back(SVG::Node{std::move(shadow_group)});

    std::string clip_id = std::format("clip_insert_{}", i);
    SVG::G clip_g;
    SVG::Path clip_path;
    clip_path.data.push_back(SVG::MoveTo{min_px, min_py});
    clip_path.data.push_back(SVG::LineTo{max_px, min_py});
    clip_path.data.push_back(SVG::LineTo{max_px, max_py});
    clip_path.data.push_back(SVG::LineTo{min_px, max_py});
    clip_path.data.push_back(SVG::ClosePath{});

    SVG::Path bg_path = clip_path;

    clip_g.children.push_back(SVG::Node{std::move(clip_path)});
    doc.defs[clip_id] = std::move(clip_g);

    SVG::G insert_group = DrawMesh(plan.scaler, i, scale_x);
    insert_group.style.transform = std::array<double, 6>{
        scale_x, 0.0, 0.0, scale_y, tx, ty};
    insert_group.style.clip_path = clip_id;

    SVG::G bg_group;
    bg_group.style.fill_color = 0xFFFFFFFF;
    bg_group.style.stroke_color = SVG::COLOR_NONE;
    bg_group.children.push_back(SVG::Node{std::move(bg_path)});
    insert_group.children.insert(insert_group.children.begin(),
                                 SVG::Node{std::move(bg_group)});

    root_group.children.push_back(SVG::Node{std::move(insert_group)});

    SVG::Path border_path;
    border_path.data.push_back(SVG::MoveTo{min_ix, min_iy});
    border_path.data.push_back(SVG::LineTo{max_ix, min_iy});
    border_path.data.push_back(SVG::LineTo{max_ix, max_iy});
    border_path.data.push_back(SVG::LineTo{min_ix, max_iy});
    border_path.data.push_back(SVG::ClosePath{});

    SVG::G border_group;
    border_group.style.stroke_color = 0x000000FF;
    border_group.style.stroke_width = 1.0;
    border_group.style.fill_color = SVG::COLOR_NONE;
    border_group.children.push_back(SVG::Node{std::move(border_path)});
    root_group.children.push_back(SVG::Node{std::move(border_group)});
  }

  doc.root = SVG::Node{std::move(root_group)};
  doc.view_box = std::array<double, 4>{
      0, 0, static_cast<double>(WIDTH), static_cast<double>(HEIGHT)};

  return doc;
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

