
#include "scene.h"

#include <cmath>
#include <mutex>
#include <numbers>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "arcfour.h"
#include "geom/polygonization.h"
#include "geom/polygons.h"
#include "math_functions.h"
#include "randutil.h"
#include "color-util.h"
#include "threadutil.h"
#include "box2d.h"
#include "initialization.h"
#include "rendering.h"

// In Box2D, the global worlds structure is thread hostile.
// But it seems like aside from world creation/destruction,
// you can use them in parallel.
static std::mutex world_mutex;

Scene::Scene() {
  b2WorldDef world_def = b2DefaultWorldDef();
  // XXX... necessary for parallelism?
  world_def.workerCount = 0;
  world_def.gravity = {0.0f, 9.8f};

  {
    MutexLock ml(&world_mutex);
    world_id = b2CreateWorld(&world_def);
  }

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

Scene::~Scene() {
  MutexLock ml(&world_mutex);
  b2DestroyWorld(world_id);
}

void Scene::Update() {
  b2World_Step(world_id, 1.0f / 120.0f, 8);
}

void Scene::AddDirt(ArcFour *rc) {
  for (int i = 0; i < 1000; i++) {
    float r = 0.02f + RandDouble(rc) * 0.2f;
    float rmargin = MARGIN + r;
    float w = WIDTH - 2.0f * rmargin;
    float h = HEIGHT - 2.0f * rmargin;

    uint32_t color =
      ColorUtil::HSVAToRGBA32(
          RandDouble(rc), 0.5 + RandDouble(rc) * 0.5,
          0.25 + RandDouble(rc) * 0.5, 0.8);

    float x = (float)RandDouble(rc) * w + rmargin;
    float y = (float)RandDouble(rc) * h + rmargin;
    float vx = ((float)RandDouble(rc) * 2.0f - 1.0f) * 6.0f;
    float vy = ((float)RandDouble(rc) * 2.0f - 1.0f) * 6.0f;

    Polygonization::Mesh mesh;
    std::vector<int> poly;
    for (int j = 0; j < 5; j++) {
      float theta = j * 2.0f * std::numbers::pi / 5.0f;
      mesh.vertices.push_back({r * std::cos(theta), r * std::sin(theta)});
      poly.push_back(j);
    }
    mesh.polygons.push_back(poly);

    AddObject(mesh, color, {x, y}, {vx, vy});
  }
}


std::optional<vec2f> Scene::RejectObject(
    const Polygonization::Mesh &mesh, vec2f pos,
    vec2f reject_dir) {
  float cx = pos.x;
  float cy = pos.y;

  auto [rx, ry] = reject_dir;
  float dir_len = std::sqrt(rx * rx + ry * ry);
  if (dir_len < 1e-6f) return std::nullopt;
  vec2 dir = {rx / dir_len, ry / dir_len};

  float min_x = 1e9f, max_x = -1e9f;
  float min_y = 1e9f, max_y = -1e9f;
  for (const auto &v : mesh.vertices) {
    auto [vx, vy] = v;
    if (vx < min_x) min_x = vx;
    if (vx > max_x) max_x = vx;
    if (vy < min_y) min_y = vy;
    if (vy > max_y) max_y = vy;
  }

  std::vector<std::vector<b2ShapeId>> all_shapes;
  all_shapes.reserve(objects.size());
  for (const auto &obj : objects) {
    int count = b2Body_GetShapeCount(obj.body_id);
    std::vector<b2ShapeId> shapes(count);
    if (count > 0) {
      b2Body_GetShapes(obj.body_id, shapes.data(), count);
    }
    all_shapes.push_back(std::move(shapes));
  }

  float step = 0.05f;
  int max_steps = 1000;

  auto HasOverlap = [&](float cx, float cy) {
    if (cx + min_x < MARGIN || cx + max_x > WIDTH - MARGIN ||
        cy + min_y < MARGIN || cy + max_y > HEIGHT - MARGIN) {
      return true;
    }

    // Check if any mesh vertex/midpoint is inside an existing object.
    for (const auto &poly : mesh.polygons) {
      for (size_t k = 0; k < poly.size(); k++) {
        auto [v1x, v1y] = mesh.vertices[poly[k]];
        auto [v2x, v2y] = mesh.vertices[poly[(k + 1) % poly.size()]];

        for (int frac = 0; frac < 2; frac++) {
          double m = frac / 2.0;
          double vx = v1x + (v2x - v1x) * m;
          double vy = v1y + (v2y - v1y) * m;
          b2Vec2 p;
          p.x = cx + vx;
          p.y = cy + vy;

          for (size_t j = 0; j < objects.size(); j++) {
            for (b2ShapeId shape : all_shapes[j]) {
              if (b2Shape_TestPoint(shape, p)) {
                return true;
              }
            }
          }
        }
      }
    }

    // Check if any existing object vertex/midpoint is inside the mesh.
    for (const auto &obj : objects) {
      b2Vec2 pos = b2Body_GetPosition(obj.body_id);
      b2Rot rot = b2Body_GetRotation(obj.body_id);

      auto transform = [&](const auto &v) {
        float rtx = rot.c * v.x - rot.s * v.y;
        float rty = rot.s * v.x + rot.c * v.y;
        return vec2{pos.x + rtx, pos.y + rty};
      };

      for (const auto &tri : obj.mesh) {
        vec2 pts[3] = {
          transform(tri.a), transform(tri.b), transform(tri.c)
        };

        for (int k = 0; k < 3; k++) {
          auto [p1x, p1y] = pts[k];
          auto [p2x, p2y] = pts[(k + 1) % 3];

          for (int frac = 0; frac < 2; frac++) {
            double m = frac / 2.0;
            double lpx = p1x + (p2x - p1x) * m - cx;
            double lpy = p1y + (p2y - p1y) * m - cy;
            vec2 local_pt = {lpx, lpy};

            for (const auto &poly : mesh.polygons) {
              if (PointInPolygon(mesh.vertices, poly, local_pt)) {
                return true;
              }
            }
          }
        }
      }
    }

    return false;
  };

  for (int i = 0; i < max_steps; i++) {
    if (!HasOverlap(cx, cy)) {
      return {vec2f{cx, cy}};
    }

    auto [dx, dy] = dir;
    cx += dx * step;
    cy += dy * step;
  }

  return std::nullopt;
}

void Scene::AddObject(const Polygonization::Mesh &mesh,
                      uint32_t color, vec2f pos,
                      vec2f vel, float restitution) {
  b2BodyDef body_def = b2DefaultBodyDef();
  body_def.type = b2_dynamicBody;
  body_def.position = {pos.x, pos.y};
  body_def.linearVelocity = {vel.x, vel.y};

  b2BodyId body_id = b2CreateBody(world_id, &body_def);

  b2ShapeDef shape_def = b2DefaultShapeDef();
  shape_def.density = 1.0f;
  shape_def.material.restitution = restitution;
  shape_def.material.friction = 0.2f;

  Attach(body_id, shape_def, mesh, color);
}

void Scene::AddFixedObject(const Polygonization::Mesh &mesh,
                           uint32_t color, vec2f pos,
                           float friction) {
  b2BodyDef body_def = b2DefaultBodyDef();
  body_def.type = b2_staticBody;
  body_def.position = {pos.x, pos.y};

  b2BodyId body_id = b2CreateBody(world_id, &body_def);

  b2ShapeDef shape_def = b2DefaultShapeDef();
  shape_def.density = 1.0f;
  shape_def.material.restitution = 0.0;
  shape_def.material.friction = friction;

  Attach(body_id, shape_def, mesh, color);
}

void Scene::Attach(b2BodyId body_id,
                   b2ShapeDef shape_def,
                   const Polygonization::Mesh &mesh,
                   uint32_t color) {
  std::vector<Rendering::Triangle> render_mesh;

  for (const auto &poly : mesh.polygons) {
    if (poly.size() < 3)
      continue;

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

      render_mesh.push_back({{(float)x0, (float)y0},
                             {(float)x1, (float)y1},
                             {(float)x2, (float)y2},
                             color,
                             0});
    }

    b2Hull hull = b2ComputeHull(pts.data(), pts.size());
    if (hull.count < 3)
      continue;

    b2Polygon b2_poly = b2MakePolygon(&hull, 0.0f);
    b2CreatePolygonShape(body_id, &shape_def, &b2_poly);
  }

  objects.push_back(Obj{
      .rgba = color,
      .body_id = body_id,
      .mesh = render_mesh,
  });
}

void Scene::ApplyImpulse(vec2f v) {
  for (const Obj &obj : objects) {
    float mass = b2Body_GetMass(obj.body_id);
    float ix = v.x * mass;
    float iy = v.y * mass;
    b2Body_ApplyLinearImpulseToCenter(obj.body_id, {ix, iy}, true);
  }
}

// Get the scene, using Cartesian coordinates.
std::vector<Rendering::Triangle> Scene::GetScene() {
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
