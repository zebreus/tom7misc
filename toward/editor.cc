
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "SDL_main.h"
#include "ansi.h"
#include "arcfour.h"
#include "base/logging.h"
#include "base/print.h"
#include "color-util.h"
#include "geom/bezier.h"
#include "geom/polygonization.h"
#include "geom/polygons.h"
#include "initialization.h"
#include "inputs.h"
#include "letters.h"
#include "randutil.h"
#include "rendering.h"
#include "scene.h"
#include "svg.h"
#include "utf8.h"
#include "util.h"
#include "yocto-math.h"

static constexpr int MAX_POLYGON_VERTICES = 8;

struct LevelBody {
  Polygonization::Mesh mesh;
  uint32_t color = 0xFFFFFFFF;
  vec2f pos = {0.0, 0.0};
  // If true, then it is moved by physics. If false,
  // bodies can collide with it, but this body
  // never moves.
  bool dynamic = false;
};

// The starting state of the level.
struct Level {

  std::vector<LevelBody> bodies;

};

static void AddNodesToLevel(const SVG::Node &node,
                            const SVG::GraphicsState &state,
                            Level *level) {
  if (const SVG::G *g = std::get_if<SVG::G>(&node.v)) {
    SVG::GraphicsState next_state = SVG::UpdateState(state, g->style);
    for (const auto &child : g->children) {
      AddNodesToLevel(child, next_state, level);
    }
  } else if (const SVG::Path *path = std::get_if<SVG::Path>(&node.v)) {
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
    vec2f pos = {static_cast<float>(center.x), static_cast<float>(center.y)};

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

static std::unique_ptr<Level> LoadSVG(std::string_view filename) {
  std::string contents = Util::ReadFile(filename);
  CHECK(!contents.empty()) << filename;

  SVG::Doc doc = SVG::ParseOrDie(contents);
  auto level = std::make_unique<Level>();

  SVG::GraphicsState state;
  // SVG is nominally 1920x1080; scene is 19.2x10.8.
  state.transform[0] = 0.01;
  state.transform[3] = 0.01;
  AddNodesToLevel(doc.root, state, level.get());

  return level;
}

std::unique_ptr<Scene> BeginLevel(const Level &level) {
  std::unique_ptr<Scene> scene = std::make_unique<Scene>();
  for (const LevelBody &body : level.bodies) {
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

void Simulate(std::string_view level_file) {
  std::unique_ptr<Level> level = LoadSVG(level_file);
  std::unique_ptr<Scene> scene = BeginLevel(*level);

  bool paused = true;

  std::unique_ptr<Inputs> inputs =
    Inputs::CreateSDL();
  std::unique_ptr<Rendering> rendering =
    Rendering::CreateSDLGL();
  CHECK(rendering.get() != nullptr);
  Print("Created rendering.\n");

  for (;;) {
    for (;;) {
      const Inputs::Input input = inputs->GetInput();
      if (std::holds_alternative<Inputs::None>(input))
        break;

      if (std::holds_alternative<Inputs::Exit>(input))
        return;

      if (const Inputs::KeyDown *kdown = std::get_if<Inputs::KeyDown>(&input)) {
        if (kdown->codepoint == '\r') {
          paused = !paused;
        } else if (kdown->codepoint == 'r' || kdown->codepoint == 'R') {
          level = LoadSVG(level_file);
          scene = BeginLevel(*level);
        }
      }

      if (const Inputs::KeyUp *kup = std::get_if<Inputs::KeyUp>(&input)) {
        if (kup->codepoint == 0x1b) {
          // Escape
          return;
        }

        Print("KeyUp: {}\n", UTF8::Encode(kup->codepoint));
        fflush(stdout);
      }
    }

    if (!paused) {
      scene->Update();
    }
    rendering->RenderScene(vec2f{0.0f, 0.0f},
                           vec2f{Scene::WIDTH, Scene::HEIGHT},
                           scene->GetScene());
  }

}


int main(int argc, char* argv[]) {
  ANSI::Init();

  std::string level_file = "example.svg";
  if (argc >= 2) level_file = argv[1];

  Initialization::Initialize();

  Simulate(level_file);

  Initialization::Exit();
  return 0;
}
