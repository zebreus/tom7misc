
#include "level.h"

#include <array>
#include <cmath>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "ansi.h"
#include "base/print.h"
#include "geom/bezier.h"
#include "geom/polygonization.h"
#include "geom/polygons.h"
#include "scene.h"
#include "svg.h"
#include "toward-util.h"
#include "util.h"

// 4x1 block, vertical.
LevelBody Levels::One() {
  LevelBody body;
  constexpr float b = Levels::BLOCK_SIZE;
  body.mesh.vertices = {
    {b/-2, 4 * b/-2},
    {b/-2, 4 * b/+2},
    {b/+2, 4 * b/+2},
    {b/+2, 4 * b/-2},
  };
  body.mesh.polygons = {
    {0, 1, 2, 3},
  };
  body.dynamic = true;
  body.color = 0xFF00FFFF;

  return body;
}

LevelBody Levels::Zero() {
  LevelBody body;

  // Manually create a clean O shape (wheel).
  // This is a circle of radius 2 (blocks) with a circle of radius 1
  // cut out of the center. We construct this as a mesh by creating
  // this many convex polygons.
  static constexpr int segments = 15;

  // The inner circle is just an n-gon with "segments" sides. On the
  // outer circle, we have this many additional interpolated points
  // per segment.
  constexpr int outer_points = 2;

  constexpr float r1 = Levels::BLOCK_SIZE;
  constexpr float r2 = Levels::BLOCK_SIZE * 2.0f;

  std::vector<vec2> verts;
  // cartesian CW (screen CCW), vertex indices.
  std::vector<std::vector<int>> polys;

  // As we enter the loop below, we always have the
  // previous radial segment as the last two vertices.
  // (outer, then inner). So set that up:
  verts.emplace_back(r2, 0.0f);
  verts.emplace_back(r1, 0.0f);

  for (int i = 0; i < segments; i++) {
    double angle = ((double)i / (double)segments) * std::numbers::pi * 2.0;

    double next_angle =
      ((double)(i + 1) / (double)segments) * std::numbers::pi * 2.0;

    CHECK(verts.size() >= 2);
    int prev_outer = verts.size() - 2;
    int prev_inner = verts.size() - 1;

    // Add interpolated outer points.
    std::vector<int> interp_outer;
    for (int j = 1; j <= outer_points; j++) {
      double a = angle + (next_angle - angle) * j / (outer_points + 1.0);
      verts.emplace_back((float)(std::cos(a) * r2), (float)(std::sin(a) * r2));
      interp_outer.push_back(verts.size() - 1);
    }

    int next_outer = 0;
    int next_inner = 1;
    if (i < segments - 1) {
      double x = std::cos(next_angle);
      double y = std::sin(next_angle);

      // Set up shared edge for next segment at the end.
      next_outer = verts.size();
      verts.emplace_back((float)(x * r2), (float)(y * r2));
      next_inner = verts.size();
      verts.emplace_back((float)(x * r1), (float)(y * r1));
    }

    // Add the polygon. Winding is Cartesian CW (Screen CCW).
    std::vector<int> poly;
    poly.push_back(prev_inner);
    poly.push_back(next_inner);
    poly.push_back(next_outer);
    for (int j = outer_points - 1; j >= 0; j--) {
      poly.push_back(interp_outer[j]);
    }
    poly.push_back(prev_outer);

    polys.push_back(std::move(poly));
  }

  body.mesh.vertices = std::move(verts);
  body.mesh.polygons = std::move(polys);
  body.dynamic = true;
  body.color = 0xFF00FFFF;

  return body;
}



// Example:
/*
  FIXME This is out of date -- I scaled them down.
  <g fill="none" stroke="#ff00ff">
  <path d="M 400.0000 160.0000L 440.0000 160.0000L 440.0000 320.0000L 400.0000 320.0000L 400.0000 160.0000Z" />
  </g>
*/
std::optional<vec2f> Levels::IsSVGOne(const SVG::GraphicsState &outer_state,
                                      const SVG::Node &node) {
  const SVG::G *g = std::get_if<SVG::G>(&node.v);
  if (g == nullptr || g->children.size() != 1) return std::nullopt;

  SVG::GraphicsState state = SVG::UpdateState(outer_state, g->style);

  // Check for magenta (#ff00ff) stroke.
  // This covers standard 32-bit color layouts (RGBA, ARGB, RGB).
  uint32_t c = state.stroke_color;
  if (c != 0xFF00FFFF) return std::nullopt;

  const SVG::Path *path = std::get_if<SVG::Path>(&g->children[0].v);
  if (!path) return std::nullopt;

  bool first = true;
  float min_x = 0.0f, max_x = 0.0f, min_y = 0.0f, max_y = 0.0f;
  int num_points = 0;

  for (const SVG::PathCommand &cmd : path->data) {
    float x = 0.0f, y = 0.0f;
    if (const SVG::MoveTo *m = std::get_if<SVG::MoveTo>(&cmd)) {
      x = (float)m->x; y = (float)m->y;
    } else if (const SVG::LineTo *l = std::get_if<SVG::LineTo>(&cmd)) {
      x = (float)l->x; y = (float)l->y;
    } else if (std::holds_alternative<SVG::ClosePath>(cmd)) {
      continue;
    } else {
      // Curved segments are not allowed
      return std::nullopt;
    }

    if (first) {
      min_x = max_x = x;
      min_y = max_y = y;
      first = false;
    } else {
      if (x < min_x) min_x = x;
      if (x > max_x) max_x = x;
      if (y < min_y) min_y = y;
      if (y > max_y) max_y = y;
    }
    num_points++;
  }

  // A rectangle will have at least 4 points.
  if (num_points < 4) return std::nullopt;

  float width = max_x - min_x;
  float height = max_y - min_y;

  // Allow a small epsilon for floating-point inaccuracies.
  if (width > 39.9f && width < 40.1f && height > 159.9f && height < 160.1f) {
    return vec2f{(min_x + max_x) / 2.0f, (min_y + max_y) / 2.0f};
  }

  return std::nullopt;
}

void Levels::AddNodesToLevel(const SVG::Node &node,
                             const SVG::GraphicsState &state,
                             Level *level) {
  if (std::optional<vec2f> oone = IsSVGOne(state, node)) {
    Print("Got One at {},{}\n", oone.value().x, oone.value().y);
    // return;
  }

  if (const SVG::G *g = std::get_if<SVG::G>(&node.v)) {
    SVG::GraphicsState next_state = SVG::UpdateState(state, g->style);
    for (const auto &child : g->children) {
      AddNodesToLevel(child, next_state, level);
    }
  } else if (const SVG::Path *path = std::get_if<SVG::Path>(&node.v)) {
    if (state.opacity < 0.2) return;

    bool has_fill = state.fill_color != SVG::COLOR_NONE;
    bool has_stroke = state.stroke_color != SVG::COLOR_NONE;

    if (!has_fill && !has_stroke) return;

    Polygonization::Shape poly_shape;
    Polygon poly;
    Polygonization::vec2 current_pt{0.0, 0.0};

    for (const SVG::PathCommand &orig_cmd : path->data) {
      SVG::PathCommand cmd = SVG::TransformCommand(
          state.transform, orig_cmd);

      if (const SVG::MoveTo *m = std::get_if<SVG::MoveTo>(&cmd)) {
        if (!poly.empty()) {
          if (poly.size() >= 3) {
            poly_shape.polys.push_back(std::move(poly));
          }
          poly.clear();
        }
        current_pt = {m->x, m->y};
        poly.push_back(current_pt);
      } else if (const SVG::LineTo *l = std::get_if<SVG::LineTo>(&cmd)) {
        current_pt = {l->x, l->y};
        poly.push_back(current_pt);
      } else if (const SVG::CubicBezier *c =
                     std::get_if<SVG::CubicBezier>(&cmd)) {
        constexpr float MAX_ERROR_SQUARED = 0.01 * 0.01;
        auto tess = TesselateCubicBezier<double>(
            current_pt.x, current_pt.y,
            c->cx1, c->cy1, c->cx2, c->cy2, c->x, c->y,
            MAX_ERROR_SQUARED);
        for (const auto &[x, y] : tess) {
          poly.push_back({x, y});
        }
        current_pt = {c->x, c->y};
      } else if (std::holds_alternative<SVG::ClosePath>(cmd)) {
        // Polygons are implicitly closed.
      }
    }

    if (poly.size() >= 3) {
      poly_shape.polys.push_back(std::move(poly));
    }

    if (poly_shape.polys.empty()) return;

    Polygonization::PolygonizeResult res =
      Polygonization::Polygonize(poly_shape, MAX_POLYGON_VERTICES);
    Polygonization::Mesh *mesh = std::get_if<Polygonization::Mesh>(&res);

    if (mesh == nullptr) {
      if (const std::string_view *err = std::get_if<std::string_view>(&res)) {
        Print("Polygonization failed: {}\n", *err);
      }
      return;
    }

    Polygonization::vec2 center{0.0, 0.0};
    if (!mesh->vertices.empty()) {
      for (const auto& v : mesh->vertices) {
        center.x += v.x;
        center.y += v.y;
      }
      center.x /= mesh->vertices.size();
      center.y /= mesh->vertices.size();
      for (vec2 &v : mesh->vertices) {
        v.x -= center.x;
        v.y -= center.y;
      }
    }
    vec2f pos = {(float)center.x, (float)center.y};

    // If the shape has fill, it should be a static body.
    if (has_fill) {
      LevelBody body;
      body.mesh = std::move(*mesh);
      body.color = state.fill_color;
      body.pos = pos;
      body.dynamic = false;
      level->bodies.push_back(std::move(body));
    } else if (has_stroke) {
      // If it is outlined, it should be a dynamic body.
      LevelBody body;
      // Move the mesh if we're done with it, else copy.
      body.mesh = std::move(*mesh);
      body.color = state.stroke_color;
      body.pos = pos;
      body.dynamic = true;
      level->bodies.push_back(std::move(body));
    }
    CHECK(!level->bodies.empty());
    const LevelBody &body = level->bodies.back();
    Print("Add {} body at {},{} with color {:08x}\n",
          body.dynamic ? "dynamic" : "static",
          body.pos.x, body.pos.y,
          body.color);
  }
}

std::unique_ptr<Level> Levels::LoadSVG(std::string_view filename) {
  std::string contents = Util::ReadFile(filename);
  CHECK(!contents.empty()) << filename;

  SVG::Doc doc = SVG::ParseOrDie(contents);
  auto level = std::make_unique<Level>();

  SVG::GraphicsState state;
  state.transform[0] = 1.0f / SVG_SCALE;
  state.transform[3] = 1.0f / SVG_SCALE;
  AddNodesToLevel(doc.root, state, level.get());

  return level;
}

void Levels::SaveSVG(const Level &level, std::string_view filename) {
  SVG::Doc doc;
  doc.view_box = std::array<double, 4>{0.0, 0.0, 1920.0, 1080.0};

  SVG::G root_g;

  for (const LevelBody &body : level.bodies) {
    SVG::Path path;
    for (const auto &poly : body.mesh.polygons) {
      if (poly.empty()) continue;
      path.data.push_back(SVG::MoveTo{
          (body.mesh.vertices[poly[0]].x + body.pos.x) * SVG_SCALE,
          (body.mesh.vertices[poly[0]].y + body.pos.y) * SVG_SCALE});
      for (size_t i = 1; i < poly.size(); i++) {
        path.data.push_back(SVG::LineTo{
            (body.mesh.vertices[poly[i]].x + body.pos.x) * SVG_SCALE,
            (body.mesh.vertices[poly[i]].y + body.pos.y) * SVG_SCALE});
      }
      path.data.push_back(SVG::ClosePath{});
    }

    SVG::Node path_node;
    path_node.v = std::move(path);

    SVG::G body_g;
    if (body.dynamic) {
      body_g.style.stroke_color = body.color;
      body_g.style.fill_color = SVG::COLOR_NONE;
      body_g.style.stroke_width = 2.0;
    } else {
      body_g.style.fill_color = body.color;
      body_g.style.stroke_color = SVG::COLOR_NONE;
    }
    body_g.children.push_back(std::move(path_node));

    SVG::Node body_node;
    body_node.v = std::move(body_g);
    root_g.children.push_back(std::move(body_node));
  }

  doc.root.v = std::move(root_g);

  std::string svg_str = SVG::ToSVG(doc);
  CHECK(Util::WriteFile(filename, svg_str))
      << "Failed to write " << filename;
}

std::unique_ptr<Scene> Levels::CreateScene(const Level &level) {
  std::unique_ptr<Scene> scene =
    std::make_unique<Scene>(level.scene_walls);
  for (const LevelBody &body : level.bodies) {
    /*
    Print("[{}⏹" ANSI_RESET "]Body at {:.2f},{:.2f}\n",
          ANSI::ForegroundRGB32(body.color),
          body.pos.x, body.pos.y);
    */
    if (body.dynamic) {
      scene->AddObject(body.mesh, body.color, body.pos,
                       vec2f{0.0f, 0.0f},
                       0.05f);
    } else {
      scene->AddFixedObject(body.mesh, body.color, body.pos,
                            0.1f);
    }
  }
  return scene;
}
