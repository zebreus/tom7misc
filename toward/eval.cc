
#include "eval.h"

#include <cmath>
#include <format>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>

#include "box2d.h"
#include "geom/polygonization.h"
#include "image.h"
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

  for (int i = 0; i < 5 * 60; i++) {
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

static constexpr int IMAGE_WIDTH = 1920;
static constexpr int IMAGE_HEIGHT = 1080;

ImageRGBA Eval::DebugStability(const Letter &letter) {
  ImageRGBA img(IMAGE_WIDTH, IMAGE_HEIGHT);
  img.Clear32(0x000000FF);

  int text_y = 10;
  auto LogText = [&](std::string_view s) {
    img.BlendText32(10, text_y, 0xFFFFFFFF, s);
    text_y += 15;
  };

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

  auto DrawBounds = [&]() {
    int x1 = (int)(Scene::MARGIN * 100.0f);
    int y1 = (int)(Scene::MARGIN * 100.0f);
    int x2 = (int)((Scene::WIDTH - Scene::MARGIN) * 100.0f);
    int y2 = (int)((Scene::HEIGHT - Scene::MARGIN) * 100.0f);
    img.BlendBox32(x1, y1, x2 - x1, y2 - y1, 0xFFFFFFFF, std::nullopt);
  };

  auto DrawMesh = [&](b2Vec2 p, b2Rot r, uint32_t color) {
    if (scaled_mesh.vertices.empty()) return;
    for (size_t i = 0; i < scaled_mesh.vertices.size(); i++) {
      size_t j = (i + 1) % scaled_mesh.vertices.size();
      auto [p1x, p1y] = scaled_mesh.vertices[i];
      auto [p2x, p2y] = scaled_mesh.vertices[j];

      float x1 = p.x + r.c * p1x - r.s * p1y;
      float y1 = p.y + r.s * p1x + r.c * p1y;
      float x2 = p.x + r.c * p2x - r.s * p2y;
      float y2 = p.y + r.s * p2x + r.c * p2y;

      img.BlendLine32((int)(x1 * 100.0f), (int)(y1 * 100.0f),
                      (int)(x2 * 100.0f), (int)(y2 * 100.0f), color);
    }
  };

  if (min_x > max_x) {
    LogText("Failed: min_x > max_x (penalty 1000.0)");
    DrawBounds();
    DrawMesh(b2Vec2{Scene::WIDTH / 2.0f, Scene::HEIGHT / 2.0f},
             b2Rot{0.0f, 1.0f}, 0xFF0000FF);
    return img;
  }

  float start_x = Scene::WIDTH / 2.0f;
  // Start with the bottom of the bounding box overlapping the floor by 1.0
  float start_y = Scene::HEIGHT - Scene::MARGIN - max_y + 1.0f;

  auto pos = scene.RejectObject(scaled_mesh, {start_x, start_y}, {0.0f, -1.0f});
  if (!pos) {
    LogText("Failed: RejectObject returned nullopt (penalty 1000.0)");
    DrawBounds();
    DrawMesh(b2Vec2{start_x, start_y}, b2Rot{0.0f, 1.0f}, 0xFF0000FF);
    return img;
  }

  auto [px, py] = *pos;
  scene.AddObject(scaled_mesh, 0xFFFFFFFF, *pos, {0.0f, 0.0f});
  if (scene.objects.empty()) {
    LogText("Failed: scene.objects is empty (penalty 1000.0)");
    DrawBounds();
    DrawMesh(b2Vec2{px, py}, b2Rot{0.0f, 1.0f}, 0xFF0000FF);
    return img;
  }

  b2BodyId body_id = scene.objects.front().body_id;
  b2Vec2 init_pos = b2Body_GetPosition(body_id);
  b2Rot init_rot = b2Body_GetRotation(body_id);

  LogText("Simulation started.");
  DrawBounds();

  for (int i = 0; i <= 5 * 60; i++) {
    bool awake = b2Body_IsAwake(body_id);
    bool last_frame = (i == 5 * 60) || !awake;

    if (i % 30 == 0 || last_frame) {
      b2Vec2 curr_pos = b2Body_GetPosition(body_id);
      b2Rot curr_rot = b2Body_GetRotation(body_id);

      DrawMesh(curr_pos, curr_rot, 0xFFFFFF88);

      float cpx = curr_pos.x * 100.0f;
      float cpy = curr_pos.y * 100.0f;

      img.BlendText32((int)(cpx) + 10, (int)(cpy) - 10,
                      0xFFFFFFFF, std::format("F{}", i));

      float ax = curr_pos.x + curr_rot.c * 2.0f;
      float ay = curr_pos.y + curr_rot.s * 2.0f;
      img.BlendThickLine32(cpx, cpy, ax * 100.0f, ay * 100.0f,
                           2.0f, 0x0000FF88);

      float ipx = init_pos.x * 100.0f;
      float ipy = init_pos.y * 100.0f;
      img.BlendThickLine32(ipx, ipy, cpx, cpy, 2.0f, 0xFF000088);
    }

    if (last_frame) {
      if (!awake) {
        LogText(std::format("Body fell asleep at frame {}", i));
      } else {
        LogText("Simulation reached maximum frames (300)");
      }
      break;
    }

    scene.Update();
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

  double penalty = std::hypot(dx, dy) + std::abs(d_angle);

  LogText("Success. Penalty: " + std::to_string(penalty));

  return img;
}
