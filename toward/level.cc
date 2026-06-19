
#include "level.h"

#include <algorithm>
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

#include "base/print.h"
#include "geom/bezier.h"
#include "geom/polygonization.h"
#include "geom/polygons.h"
#include "scene.h"
#include "svg.h"
#include "toward-util.h"
#include "util.h"
#include "yocto-math.h"

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
  body.item = LevelItem::ONE;

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
  body.item = LevelItem::ZERO;

  return body;
}

static std::optional<vec2f> IsSVGRectangle(
    const SVG::GraphicsState &outer_state,
    const SVG::Node &node,
    bool stroke,
    uint32_t expected_color,
    int block_width,
    int block_height) {
  const SVG::G *g = std::get_if<SVG::G>(&node.v);
  if (g == nullptr || g->children.size() != 1) return std::nullopt;

  SVG::GraphicsState state = SVG::UpdateState(outer_state, g->style);

  if ((stroke ? state.stroke_color : state.fill_color) != expected_color) {
    return std::nullopt;
  }

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

  float expected_width = block_width * Levels::BLOCK_SIZE * Levels::SVG_SCALE;
  float expected_height = block_height * Levels::BLOCK_SIZE * Levels::SVG_SCALE;
  constexpr float epsilon = 0.1f;

  if (std::abs(width - expected_width) < epsilon &&
      std::abs(height - expected_height) < epsilon) {
    return vec2f{(min_x + max_x) / 2.0f, (min_y + max_y) / 2.0f};
  }

  return std::nullopt;
}

std::optional<vec2f> Levels::IsSVGOne(const SVG::GraphicsState &outer_state,
                                      const SVG::Node &node) {
  // Must have magenta stroke.
  return IsSVGRectangle(outer_state, node, true, 0xFF00FFFF, 1, 4);
}

std::optional<vec2f> Levels::IsInput(const SVG::GraphicsState &outer_state,
                                     const SVG::Node &node) {
  return IsSVGRectangle(outer_state, node, false, INPUT_COLOR,
                        IN_WIDTH, IN_HEIGHT);
}

std::optional<vec2f> Levels::IsOutput(const SVG::GraphicsState &outer_state,
                                      const SVG::Node &node) {
  return IsSVGRectangle(outer_state, node, false, OUTPUT_COLOR,
                        OUT_WIDTH, OUT_HEIGHT);
}

LevelBody Levels::WallRect(vec2f center,
                           int blockwidth, int blockheight) {
  LevelBody wall;
  wall.dynamic = false;
  wall.color = 0x888888FF;

  float rect_w = blockwidth * BLOCK_SIZE;
  float rect_h = blockheight * BLOCK_SIZE;

  wall.mesh.vertices = {
    {-rect_w / 2.0f, -rect_h / 2.0f},
    { rect_w / 2.0f, -rect_h / 2.0f},
    { rect_w / 2.0f,  rect_h / 2.0f},
    {-rect_w / 2.0f,  rect_h / 2.0f}
  };
  wall.mesh.polygons = {{0, 1, 2, 3}};
  wall.dynamic = false;
  wall.pos = center;
  return wall;
}

/*
  Since svg.h will normalize circles into beziers, we need to recognize
  a series of curves that draw the concentric discs.
  Example:
    <g fill="#ff00ff">
      <path d="M 80.0000 1.5000C 101.5050 1.5000 119.0000 18.9950 119.0000 40.5000C 119.0000 62.0050 101.5050 79.5000 80.0000 79.5000C 58.4950 79.5000 41.0000 62.0050 41.0000 40.5000C 41.0000 18.9950 58.4950 1.5000 80.0000 1.5000M 80.0000 61.5000C 91.5790 61.5000 101.0000 52.0790 101.0000 40.5000C 101.0000 28.9210 91.5790 19.5000 80.0000 19.5000C 68.4210 19.5000 59.0000 28.9210 59.0000 40.5000C 59.0000 52.0790 68.4210 61.5000 80.0000 61.5000M 80.0000 0.5000C 57.9090 0.5000 40.0000 18.4090 40.0000 40.5000C 40.0000 62.5910 57.9090 80.5000 80.0000 80.5000C 102.0910 80.5000 120.0000 62.5910 120.0000 40.5000C 120.0000 18.4090 102.0910 0.5000 80.0000 0.5000L 80.0000 0.5000ZM 80.0000 60.5000C 68.9540 60.5000 60.0000 51.5460 60.0000 40.5000C 60.0000 29.4540 68.9540 20.5000 80.0000 20.5000C 91.0460 20.5000 100.0000 29.4540 100.0000 40.5000C 100.0000 51.5460 91.0460 60.5000 80.0000 60.5000L 80.0000 60.5000Z" />
    </g>
  */
std::optional<vec2f> Levels::IsSVGZero(const SVG::GraphicsState &outer_state,
                                       const SVG::Node &node) {
  const SVG::G *g = std::get_if<SVG::G>(&node.v);
  if (g == nullptr || g->children.size() != 1) return std::nullopt;

  SVG::GraphicsState state = SVG::UpdateState(outer_state, g->style);

  if (state.fill_color != 0xFF00FFFF && state.stroke_color != 0xFF00FFFF) {
    return std::nullopt;
  }

  const SVG::Path *path = std::get_if<SVG::Path>(&g->children[0].v);
  if (!path) return std::nullopt;

  bool first = true;
  vec2f min_pt = {0.0f, 0.0f};
  vec2f max_pt = {0.0f, 0.0f};

  std::vector<vec2f> endpoints;

  for (const SVG::PathCommand &cmd : path->data) {
    vec2f pt = {0.0f, 0.0f};
    if (const SVG::MoveTo *m = std::get_if<SVG::MoveTo>(&cmd)) {
      pt = {(float)m->x, (float)m->y};
    } else if (const SVG::LineTo *l = std::get_if<SVG::LineTo>(&cmd)) {
      pt = {(float)l->x, (float)l->y};
    } else if (const SVG::CubicBezier *c =
                  std::get_if<SVG::CubicBezier>(&cmd)) {
      pt = {(float)c->x, (float)c->y};
    } else if (std::holds_alternative<SVG::ClosePath>(cmd)) {
      continue;
    } else {
      return std::nullopt;
    }

    if (first) {
      min_pt = max_pt = pt;
      first = false;
    } else {
      min_pt = min(min_pt, pt);
      max_pt = max(max_pt, pt);
    }
    endpoints.push_back(pt);
  }

  if (endpoints.size() < 4) return std::nullopt;

  vec2f center = (min_pt + max_pt) / 2.0f;

  float expected_r_outer = 2.0f * Levels::BLOCK_SIZE * Levels::SVG_SCALE;
  float expected_r_inner = 1.0f * Levels::BLOCK_SIZE * Levels::SVG_SCALE;
  constexpr float epsilon = 5.0f; // Allow some deviation

  bool has_inner = false;
  bool has_outer = false;

  for (const vec2f &pt : endpoints) {
    float dist = distance(pt, center);

    bool near_inner = std::abs(dist - expected_r_inner) < epsilon;
    bool near_outer = std::abs(dist - expected_r_outer) < epsilon;

    if (near_inner) has_inner = true;
    if (near_outer) has_outer = true;

    if (!near_inner && !near_outer) {
      // Endpoint is neither near inner nor outer circle
      return std::nullopt;
    }
  }

  if (has_inner && has_outer) {
    return center;
  }

  return std::nullopt;
}


void Levels::AddNodesToLevel(const SVG::Node &node,
                             const SVG::GraphicsState &state,
                             Level *level) {
  if (std::optional<vec2f> oone = IsSVGOne(state, node)) {
    Print("Got One at {},{}\n", oone.value().x, oone.value().y);
    LevelBody one_body = Levels::One();
    one_body.color = 0x00FF00FF;
    one_body.pos = oone.value() / SVG_SCALE;
    level->bodies.push_back(std::move(one_body));
    return;
  }

  if (std::optional<vec2f> ozero = IsSVGZero(state, node)) {
    Print("Got Zero at {},{}\n", ozero.value().x, ozero.value().y);
    LevelBody zero_body = Levels::Zero();
    zero_body.color = 0xFF0000FF;
    zero_body.pos = ozero.value() / SVG_SCALE;
    level->bodies.push_back(std::move(zero_body));
    return;
  }

  if (std::optional<vec2f> in = IsInput(state, node)) {
    Print("Got input at {},{}\n", in->x, in->y);
    level->inputs.push_back(in.value() / SVG_SCALE);
    return;
  }

  if (std::optional<vec2f> out = IsOutput(state, node)) {
    Print("Got output at {},{}\n", out->x, out->y);
    level->outputs.push_back(out.value() / SVG_SCALE);
    return;
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
  level->scene_walls = false;

  SVG::GraphicsState state;
  state.transform[0] = 1.0f / SVG_SCALE;
  state.transform[3] = 1.0f / SVG_SCALE;
  AddNodesToLevel(doc.root, state, level.get());

  auto CmpX = [](const vec2f &a, const vec2f &b) { return a.x < b.x; };
  std::sort(level->inputs.begin(), level->inputs.end(), CmpX);
  std::sort(level->outputs.begin(), level->outputs.end(), CmpX);

  return level;
}

void Levels::SaveSVG(const Level &level, std::string_view filename) {
  SVG::Doc doc;
  doc.view_box = std::array<double, 4>{
    0.0, 0.0, WIDTH * SVG_SCALE, HEIGHT * SVG_SCALE,
  };

  SVG::G root_g;

  auto AddRect = [&root_g](vec2f pos, int blocks_w, int blocks_h,
                           uint32_t color) {
    SVG::Path path;
    float hw = blocks_w * BLOCK_SIZE * SVG_SCALE / 2.0f;
    float hh = blocks_h * BLOCK_SIZE * SVG_SCALE / 2.0f;
    float cx = pos.x * SVG_SCALE;
    float cy = pos.y * SVG_SCALE;

    path.data.push_back(SVG::MoveTo{cx - hw, cy - hh});
    path.data.push_back(SVG::LineTo{cx + hw, cy - hh});
    path.data.push_back(SVG::LineTo{cx + hw, cy + hh});
    path.data.push_back(SVG::LineTo{cx - hw, cy + hh});
    path.data.push_back(SVG::ClosePath{});

    SVG::Node path_node;
    path_node.v = std::move(path);

    SVG::G g;
    g.style.fill_color = color;
    g.style.stroke_color = SVG::COLOR_NONE;
    g.children.push_back(std::move(path_node));

    SVG::Node node;
    node.v = std::move(g);
    root_g.children.push_back(std::move(node));
  };

  for (const vec2f &pos : level.inputs) {
    AddRect(pos, IN_WIDTH, IN_HEIGHT, INPUT_COLOR);
  }
  for (const vec2f &pos : level.outputs) {
    AddRect(pos, OUT_WIDTH, OUT_HEIGHT, OUTPUT_COLOR);
  }

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

void Levels::AddBodyToScene(Scene *scene, const LevelBody &body,
                            std::optional<uint64_t> user_data) {
  /*
  Print("[{}⏹" ANSI_RESET "]Body at {:.2f},{:.2f}\n",
        ANSI::ForegroundRGB32(body.color),
        body.pos.x, body.pos.y);
  */
  if (body.dynamic) {
    scene->AddObject(body.mesh, body.color,
                     body.pos, body.angle,
                     body.vel, body.avel,
                     body.restitution,
                     body.friction);
  } else {
    scene->AddFixedObject(body.mesh, body.color, body.pos,
                          body.restitution,
                          body.friction);
  }
  scene->objects.back().user_data = user_data;
}

std::unique_ptr<Scene> Levels::CreateScene(const Level &level) {
  std::unique_ptr<Scene> scene =
    std::make_unique<Scene>(level.scene_walls);

  for (size_t i = 0; i < level.bodies.size(); i++) {
    AddBodyToScene(scene.get(), level.bodies[i], {i});
  }
  return scene;
}
