
#include "eval.h"

#include <cmath>
#include <numbers>

#include "box2d.h"
#include "geom/polygonization.h"
#include "letters.h"
#include "scene.h"
#include "yocto-math.h"

using vec2f = yocto::vec2f;

double Eval::Stability(const Letter &letter) {
  Scene scene;

  constexpr float SCALE_FACTOR = 10.0f;
  Polygonization::Mesh scaled_mesh = letter.mesh;

  float min_x = 1e9f, max_x = -1e9f;
  float min_y = 1e9f, max_y = -1e9f;
  for (auto &v : scaled_mesh.vertices) {
    auto &[vx, vy] = v;
    vx *= SCALE_FACTOR;
    vy *= SCALE_FACTOR;
    if (vx < min_x) min_x = vx;
    if (vx > max_x) max_x = vx;
    if (vy < min_y) min_y = vy;
    if (vy > max_y) max_y = vy;
  }

  if (min_x > max_x) return 1000.0;

  float start_x = Scene::WIDTH / 2.0f;
  // Start with the bottom of the bounding box overlapping the floor by 1.0
  float start_y = Scene::HEIGHT - Scene::MARGIN - max_y + 1.0f;

  auto pos = scene.RejectObject(scaled_mesh, {start_x, start_y}, {0.0f, -1.0f});
  if (!pos) return 1000.0;

  scene.AddObject(scaled_mesh, 0xFFFFFFFF, *pos, {0.0f, 0.0f});
  if (scene.objects.empty()) return 1000.0;

  b2BodyId body_id = scene.objects.front().body_id;
  b2Vec2 init_pos = b2Body_GetPosition(body_id);
  b2Rot init_rot = b2Body_GetRotation(body_id);

  for (int i = 0; i < 1000; i++) {
    scene.Update();
    if (!b2Body_IsAwake(body_id)) break;
  }

  b2Vec2 final_pos = b2Body_GetPosition(body_id);
  b2Rot final_rot = b2Body_GetRotation(body_id);

  double dx = final_pos.x - init_pos.x;
  double dy = final_pos.y - init_pos.y;

  double init_angle = std::atan2(init_rot.s, init_rot.c);
  double final_angle = std::atan2(final_rot.s, final_rot.c);

  double d_angle = final_angle - init_angle;
  while (d_angle > std::numbers::pi) d_angle -= 2.0 * std::numbers::pi;
  while (d_angle < -std::numbers::pi) d_angle += 2.0 * std::numbers::pi;

  return std::hypot(dx, dy) + std::abs(d_angle);
}

