
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

  struct Obj {
    uint32_t rgba = 0xFFFFFFFF;
    b2BodyId body_id = {};
    float r = 0.1f;
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

    for (int i = 0; i < 1000; i++) {
      float r = 0.02f + RandDouble(&rc) * 0.2f;
      float rmargin = MARGIN + r;
      float w = WIDTH - 2.0f * rmargin;
      float h = HEIGHT - 2.0f * rmargin;

      uint32_t color =
        ColorUtil::HSVAToRGBA32(
            RandDouble(&rc), 0.5 + RandDouble(&rc) * 0.5,
            0.25 + RandDouble(&rc) * 0.5, 0.8);

      b2BodyDef body_def = b2DefaultBodyDef();
      body_def.type = b2_dynamicBody;
      body_def.position = {(float)RandDouble(&rc) * w + rmargin,
                           (float)RandDouble(&rc) * h + rmargin};

      float vx = ((float)RandDouble(&rc) * 2.0f - 1.0f) * 6.0f;
      float vy = ((float)RandDouble(&rc) * 2.0f - 1.0f) * 6.0f;
      body_def.linearVelocity = {vx, vy};

      // body_def.angularDamping = 0.01f;
      // body_def.enableSleep = false;

      b2BodyId body_id = b2CreateBody(world_id, &body_def);

      b2ShapeDef shape_def = b2DefaultShapeDef();
      shape_def.density = 1.0f;
      shape_def.material.restitution = 0.8f;
      shape_def.material.friction = 0.2f;

      b2Vec2 pts[5];
      for (int j = 0; j < 5; j++) {
        float theta = j * 2.0f * std::numbers::pi / 5.0f;
        pts[j] = { r * std::cos(theta), r * std::sin(theta) };
      }

      #if 0
      // Link three triangles to form the pentagon body
      b2Vec2 t1[3] = {pts[0], pts[1], pts[2]};
      b2Hull hull1 = b2ComputeHull(t1, 3);
      b2Polygon poly1 = b2MakePolygon(&hull1, 0.0f);
      b2CreatePolygonShape(body_id, &shape_def, &poly1);

      b2Vec2 t2[3] = {pts[0], pts[2], pts[3]};
      b2Hull hull2 = b2ComputeHull(t2, 3);
      b2Polygon poly2 = b2MakePolygon(&hull2, 0.0f);
      b2CreatePolygonShape(body_id, &shape_def, &poly2);

      b2Vec2 t3[3] = {pts[0], pts[3], pts[4]};
      b2Hull hull3 = b2ComputeHull(t3, 3);
      b2Polygon poly3 = b2MakePolygon(&hull3, 0.0f);
      b2CreatePolygonShape(body_id, &shape_def, &poly3);
      #else

      // Using a single polygon.
      b2Hull hull = b2ComputeHull(pts, 5);
      b2Polygon poly = b2MakePolygon(&hull, 0.0f);
      b2CreatePolygonShape(body_id, &shape_def, &poly);
      #endif

      std::vector<Rendering::Triangle> mesh;
      Rendering::vec2f v[5];
      for (int j = 0; j < 5; j++) {
        v[j] = {pts[j].x, pts[j].y};
      }
      mesh.push_back({v[0], v[1], v[2], color, 0});
      mesh.push_back({v[0], v[2], v[3], color, 0});
      mesh.push_back({v[0], v[3], v[4], color, 0});

      objects.push_back(Obj{
        .rgba = color,
        .body_id = body_id,
        .r = r,
        .mesh = mesh,
      });
    }
  }

  ~Scene() {
    b2DestroyWorld(world_id);
  }

  void Update() {
    b2World_Step(world_id, 1.0f / 120.0f, 8);
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

void Simulate() {
  Scene scene;

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
          scene.ApplyRandomImpulse();
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

    scene.Update();
    rendering->RenderScene(vec2f{0.0f, 0.0f},
                           vec2f{Scene::WIDTH, Scene::HEIGHT},
                           scene.GetScene());
  }

}


int main(int argc, char* argv[]) {
  Initialization::Initialize();

  Simulate();

  Initialization::Exit();
  return 0;
}
