
#include <cmath>
#include <cstdio>
#include <memory>
#include <numbers>
#include <variant>
#include <vector>

#include "SDL_main.h"
#include "arcfour.h"
#include "base/logging.h"
#include "base/print.h"
#include "color-util.h"
#include "geom/polygonization.h"
#include "geom/polygons.h"
#include "id.h"
#include "math_functions.h"
#include "randutil.h"
#include "types.h"
#include "utf8.h"
#include "yocto-math.h"

#include "box2d.h"
#include "collision.h"
#include "initialization.h"
#include "inputs.h"
#include "letters.h"
#include "rendering.h"

using vec2f = yocto::vec2f;

struct Scene {

  // Dimensions of the world.
  static constexpr float WIDTH = 19.2f;
  static constexpr float HEIGHT = 10.8f;

  static constexpr float MARGIN = 0.1f;

  static constexpr float ARENA_WIDTH = WIDTH - 2.0f * MARGIN;
  static constexpr float ARENA_HEIGHT = HEIGHT - 2.0f * MARGIN;

  struct Obj {
    uint32_t rgba = 0xFFFFFFFF;
    b2BodyId body_id = {};
    std::vector<Rendering::Triangle> mesh;
  };

  b2WorldId world_id = {};
  std::vector<Obj> objects;
  ArcFour rc;

  Scene() : rc("scene") {
    b2WorldDef world_def = b2DefaultWorldDef();
    world_def.gravity = {0.0f, 9.8f};
    world_id = b2CreateWorld(&world_def);

    b2BodyDef wall_body_def = b2DefaultBodyDef();
    b2BodyId ground_id = b2CreateBody(world_id, &wall_body_def);

    b2ShapeDef wall_shape_def = b2DefaultShapeDef();
    wall_shape_def.material.restitution = 0.8f;
    wall_shape_def.material.friction = 0.2f;

    b2Segment top_wall = {{MARGIN, MARGIN}, {WIDTH - MARGIN, MARGIN}};
    b2CreateSegmentShape(ground_id, &wall_shape_def, &top_wall);

    b2Segment bottom_wall = {{MARGIN, HEIGHT - MARGIN},
                             {WIDTH - MARGIN, HEIGHT - MARGIN}};
    b2CreateSegmentShape(ground_id, &wall_shape_def, &bottom_wall);

    b2Segment left_wall = {{MARGIN, MARGIN}, {MARGIN, HEIGHT - MARGIN}};
    b2CreateSegmentShape(ground_id, &wall_shape_def, &left_wall);

    b2Segment right_wall = {{WIDTH - MARGIN, MARGIN},
                            {WIDTH - MARGIN, HEIGHT - MARGIN}};
    b2CreateSegmentShape(ground_id, &wall_shape_def, &right_wall);
  }

  ~Scene() {
    b2DestroyWorld(world_id);
  }

  void Update() {
    b2World_Step(world_id, 1.0f / 120.0f, 8);
  }

  void AddDirt() {
    for (int i = 0; i < 1000; i++) {
      float r = 0.02f + RandDouble(&rc) * 0.2f;
      float rmargin = MARGIN + r;
      float w = WIDTH - 2.0f * rmargin;
      float h = HEIGHT - 2.0f * rmargin;

      uint32_t color =
        ColorUtil::HSVAToRGBA32(
            RandDouble(&rc), 0.5 + RandDouble(&rc) * 0.5,
            0.25 + RandDouble(&rc) * 0.5, 0.8);

      float x = (float)RandDouble(&rc) * w + rmargin;
      float y = (float)RandDouble(&rc) * h + rmargin;
      float vx = ((float)RandDouble(&rc) * 2.0f - 1.0f) * 6.0f;
      float vy = ((float)RandDouble(&rc) * 2.0f - 1.0f) * 6.0f;

      Polygonization::Mesh mesh;
      std::vector<int> poly;
      for (int j = 0; j < 5; j++) {
        float theta = j * 2.0f * std::numbers::pi / 5.0f;
        mesh.vertices.push_back({r * std::cos(theta), r * std::sin(theta)});
        poly.push_back(j);
      }
      mesh.polygons.push_back(poly);

      AddObject(mesh, color, x, y, vx, vy);
    }
  }

  void AddObject(const Polygonization::Mesh &mesh, uint32_t color,
                 float x, float y, float vx, float vy,
                 float restitution = 0.7f) {
    b2BodyDef body_def = b2DefaultBodyDef();
    body_def.type = b2_dynamicBody;
    body_def.position = {x, y};
    body_def.linearVelocity = {vx, vy};

    b2BodyId body_id = b2CreateBody(world_id, &body_def);

    b2ShapeDef shape_def = b2DefaultShapeDef();
    shape_def.density = 1.0f;
    shape_def.material.restitution = restitution;
    shape_def.material.friction = 0.2f;

    std::vector<Rendering::Triangle> render_mesh;

    for (const auto &poly : mesh.polygons) {
      if (poly.size() < 3) continue;

      std::vector<b2Vec2> pts;
      pts.reserve(poly.size());
      for (int idx : poly) {
        auto [px, py] = mesh.vertices[idx];
        pts.push_back({(float)px, (float)py});
      }

      for (size_t i = 1; i + 1 < poly.size(); i++) {
        auto [x0, y0] = mesh.vertices[poly[0]];
        auto [x1, y1] = mesh.vertices[poly[i]];
        auto [x2, y2] = mesh.vertices[poly[i + 1]];

        render_mesh.push_back({
          {(float)x0, (float)y0},
          {(float)x1, (float)y1},
          {(float)x2, (float)y2},
          color, 0
        });
      }

      b2Hull hull = b2ComputeHull(pts.data(), pts.size());
      if (hull.count < 3) continue;

      b2Polygon b2_poly = b2MakePolygon(&hull, 0.0f);
      b2CreatePolygonShape(body_id, &shape_def, &b2_poly);
    }

    objects.push_back(Obj{
      .rgba = color,
      .body_id = body_id,
      .mesh = render_mesh,
    });
  }

  void ApplyRandomImpulse() {
    float max_mag = 6.0f;
    float dx = ((float)RandDouble(&rc) * 2.0f - 1.0f) * max_mag;
    float dy = ((float)RandDouble(&rc) - 1.0f) * max_mag;
    for (const Obj &obj : objects) {
      float mass = b2Body_GetMass(obj.body_id);
      float ix = dx * mass;
      float iy = dy * mass;
      b2Body_ApplyLinearImpulseToCenter(obj.body_id, {ix, iy}, true);
    }
  }

  // Get the scene, using Cartesian coordinates.
  std::vector<Rendering::Triangle> GetScene() {
    std::vector<Rendering::Triangle> scene;
    size_t num_triangles = 0;
    for (const Obj &obj : objects) {
      num_triangles += obj.mesh.size();
    }
    scene.reserve(num_triangles);

    for (const Obj &obj : objects) {
      b2Vec2 pos = b2Body_GetPosition(obj.body_id);
      b2Rot rot = b2Body_GetRotation(obj.body_id);

      auto transform = [&](const Rendering::vec2f &v) -> Rendering::vec2f {
        float rx = rot.c * v.x - rot.s * v.y;
        float ry = rot.s * v.x + rot.c * v.y;
        return {
          .x = pos.x + rx,
          .y = HEIGHT - (pos.y + ry),
        };
      };

      for (const Rendering::Triangle &tri : obj.mesh) {
        scene.push_back({
          transform(tri.a),
          transform(tri.b),
          transform(tri.c),
          tri.rgba,
          tri.reserved
        });
      }
    }

    return scene;
  }

};

struct Game {
  Game() {
    letters = Letters::LoadFont("helvetica.ttf");
    CHECK(letters.get() != nullptr);
    scene = std::make_unique<Scene>();
  }

  static constexpr float LETTER_SCALE = 2.0f;

  uint32_t prev_codepoint = 0;
  float letter_x = Scene::MARGIN + 0.01 * Scene::ARENA_WIDTH;
  float letter_y =
    Scene::HEIGHT - Scene::MARGIN - 0.01 * Scene::ARENA_HEIGHT -
    LETTER_SCALE;

  void AddLetter(uint32_t codepoint) {
    auto it = letters->letter.find(codepoint);
    if (it == letters->letter.end()) return;

    const Letter &letter = it->second;
    if (letter.mesh.polygons.empty()) return;

    Polygonization::Mesh scaled_mesh;
    scaled_mesh.vertices.reserve(letter.mesh.vertices.size());
    for (const vec2 &v : letter.mesh.vertices) {
      scaled_mesh.vertices.push_back(LETTER_SCALE * v);
    }
    scaled_mesh.polygons = letter.mesh.polygons;

    uint32_t color = ColorUtil::HSVAToRGBA32(
        RandDouble(&scene->rc), 0.5 + RandDouble(&scene->rc) * 0.5,
        0.25 + RandDouble(&scene->rc) * 0.5, 0.8);

    letter_x += letters->GetKerning(prev_codepoint, codepoint) * LETTER_SCALE;
    if (letter_x + LETTER_SCALE > Scene::WIDTH - Scene::MARGIN) {
      letter_x = Scene::MARGIN + 0.01 * Scene::ARENA_WIDTH;
      prev_codepoint = 0;
    } else {
      prev_codepoint = codepoint;
    }

    constexpr float RESTITUTION = 0.05;
    scene->AddObject(scaled_mesh, color, letter_x, letter_y,
                     0.0, 0.0, RESTITUTION);
  }

  std::unique_ptr<Letters> letters;
  std::unique_ptr<Scene> scene;
};

void Simulate() {
  Game game;
  bool paused = false;

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
        if (kdown->codepoint == ' ') {
          paused = !paused;
        }
      }

      if (const Inputs::KeyUp *kup = std::get_if<Inputs::KeyUp>(&input)) {
        if (kup->codepoint == 0x1b) {
          // Escape
          return;
        }

        Print("KeyUp: {}\n", UTF8::Encode(kup->codepoint));
        fflush(stdout);

        if (kup->codepoint >= 32 && kup->codepoint < 127) {
          game.AddLetter(kup->codepoint);
        }
      }
    }

    if (!paused) {
      game.scene->Update();
    }
    rendering->RenderScene(vec2f{0.0f, 0.0f},
                           vec2f{Scene::WIDTH, Scene::HEIGHT},
                           game.scene->GetScene());
  }

}


int main(int argc, char* argv[]) {
  Initialization::Initialize();

  Simulate();

  Initialization::Exit();
  return 0;
}
