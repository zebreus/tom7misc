
#include "scene.h"

#include <cmath>
#include <mutex>
#include <numbers>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "ansi.h"
#include "arcfour.h"
#include "base/print.h"
#include "box2d.h"
#include "color-util.h"
#include "geom/polygonization.h"
#include "geom/polygons.h"
#include "math_functions.h"
#include "randutil.h"
#include "rendering.h"
#include "threadutil.h"

constexpr bool VERBOSE = false;

// In Box2D, the global worlds structure is thread hostile.
// But it seems like aside from world creation/destruction,
// you can use them in parallel.
static std::mutex world_mutex;

Scene::Scene(bool walls) {
  b2WorldDef world_def = b2DefaultWorldDef();
  // XXX... necessary for parallelism?
  world_def.workerCount = 0;
  world_def.gravity = {0.0f, 9.8f};

  {
    MutexLock ml(&world_mutex);
    world_id = b2CreateWorld(&world_def);
  }

  if (walls) {
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
}

Scene::~Scene() {
  MutexLock ml(&world_mutex);
  b2DestroyWorld(world_id);
}

void Scene::Update() {
  b2World_Step(world_id, 1.0f / 120.0f, 8);
}

bool Scene::AllAsleep() {
  for (const Scene::Obj &obj : objects) {
    if (b2Body_IsValid(obj.body_id) && b2Body_IsAwake(obj.body_id)) {
      return false;
    }
  }
  return true;
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

    const float angle = 0.0f;
    const float avel = 0.0f;
    AddObject(mesh, color, {x, y}, angle,
              {vx, vy}, avel, 0.7f, 0.2f);
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
    if (b2Body_IsValid(obj.body_id)) {
      int count = b2Body_GetShapeCount(obj.body_id);
      std::vector<b2ShapeId> shapes(count);
      if (count > 0) {
        b2Body_GetShapes(obj.body_id, shapes.data(), count);
      }
      all_shapes.push_back(std::move(shapes));
    }
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
      if (!b2Body_IsValid(obj.body_id)) continue;

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

size_t Scene::AddObject(const Polygonization::Mesh &mesh,
                        uint32_t color, vec2f pos, float angle,
                        vec2f vel, float avel,
                        float restitution,
                        float friction) {
  b2BodyDef body_def = b2DefaultBodyDef();
  body_def.type = b2_dynamicBody;
  body_def.position = {pos.x, pos.y};
  body_def.rotation = b2MakeRot(angle);
  body_def.linearVelocity = {vel.x, vel.y};
  body_def.angularVelocity = avel;

  b2BodyId body_id = b2CreateBody(world_id, &body_def);

  b2ShapeDef shape_def = b2DefaultShapeDef();
  // Since all the objects have the same density, it actually
  // doesn't matter what constant we use!
  // 6.25 is chosen so that the 1x4 block has a mass of 1kg.
  // shape_def.density = 1.0f;
  shape_def.density = 6.25f;
  shape_def.material.restitution = restitution;
  shape_def.material.friction = friction;

  return Attach(body_id, shape_def, mesh, color);

  // Print("Object has mass of {:.4f}kg\n", b2Body_GetMass(body_id));
}

size_t Scene::AddFixedObject(const Polygonization::Mesh &mesh,
                           uint32_t color, vec2f pos,
                           float restitution,
                           float friction) {
  b2BodyDef body_def = b2DefaultBodyDef();
  body_def.type = b2_staticBody;
  body_def.position = {pos.x, pos.y};

  b2BodyId body_id = b2CreateBody(world_id, &body_def);

  b2ShapeDef shape_def = b2DefaultShapeDef();
  shape_def.density = 1.0f;
  shape_def.material.restitution = restitution;
  shape_def.material.friction = friction;

  return Attach(body_id, shape_def, mesh, color);
}

static std::vector<Rendering::Triangle> MakeTriangles(
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
  }

  return render_mesh;
}

void Scene::AddGraphics(const Polygonization::Mesh &mesh, uint32_t color,
                        vec2f pos, bool foreground) {
  Obj obj;
  obj.mesh = MakeTriangles(mesh, color);

  // Transform the mesh to put it at pos, since we won't have a box2d
  // position to derive.
  for (Rendering::Triangle &t : obj.mesh) {
    t.a += pos;
    t.b += pos;
    t.c += pos;
  }

  if (foreground) {
    fg_objects.push_back(std::move(obj));
  } else {
    bg_objects.push_back(std::move(obj));
  }
}

void Scene::Detach(size_t index) {
  CHECK(index < objects.size());
  Obj &obj = objects[index];
  CHECK(b2Body_IsValid(obj.body_id));
  b2DestroyBody(obj.body_id);
  obj.body_id = {};
  obj.mesh.clear();
}

size_t Scene::Attach(b2BodyId body_id,
                     b2ShapeDef shape_def,
                     const Polygonization::Mesh &mesh,
                     uint32_t color) {
  std::vector<Rendering::Triangle> render_mesh;

  // TODO: This can use MakeTriangles I think
  bool has_shapes = false;
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
    has_shapes = true;
  }

  if (!has_shapes) {
    if (VERBOSE) {
      Print("Object of color [{}⏹" ANSI_RESET "] {:08x} with "
            "no shapes ({} v {} p)!\n",
            ANSI::ForegroundRGB32(color),
            color,
            mesh.vertices.size(), mesh.polygons.size());
      for (size_t i = 0; i < mesh.vertices.size(); i++) {
        auto [x, y] = mesh.vertices[i];
        Print("  v[{}] = {}, {}\n", i, x, y);
      }
      for (size_t i = 0; i < mesh.polygons.size(); i++) {
        Print("  poly[{}]:", i);
        for (int idx : mesh.polygons[i]) {
          Print(" {}", idx);
        }
        Print("\n");
      }
    }

    // Fallback: Use the bounding box, honoring Box2D's minimum size.

    float min_x = 1e9f, max_x = -1e9f;
    float min_y = 1e9f, max_y = -1e9f;

    if (mesh.vertices.empty()) {
      min_x = -0.05f; max_x = 0.05f;
      min_y = -0.05f; max_y = 0.05f;
    } else {
      for (const auto &v : mesh.vertices) {
        auto [vx, vy] = v;
        if (vx < min_x) min_x = vx;
        if (vx > max_x) max_x = vx;
        if (vy < min_y) min_y = vy;
        if (vy > max_y) max_y = vy;
      }
    }

    float w = max_x - min_x;
    float h = max_y - min_y;

    // Minimum acceptable side length to avoid degenerate shapes in Box2D.
    const float min_len = 0.05f;
    if (w < min_len) {
      float cx = (min_x + max_x) * 0.5f;
      min_x = cx - min_len * 0.5f;
      max_x = cx + min_len * 0.5f;
    }
    if (h < min_len) {
      float cy = (min_y + max_y) * 0.5f;
      min_y = cy - min_len * 0.5f;
      max_y = cy + min_len * 0.5f;
    }

    b2Vec2 aabb_pts[4] = {
      {(float)min_x, (float)min_y},
      {(float)max_x, (float)min_y},
      {(float)max_x, (float)max_y},
      {(float)min_x, (float)max_y}
    };

    b2Hull hull = b2ComputeHull(aabb_pts, 4);
    if (hull.count >= 3) {
      b2Polygon b2_poly = b2MakePolygon(&hull, 0.0f);
      b2CreatePolygonShape(body_id, &shape_def, &b2_poly);
    }
  }

  size_t idx = objects.size();
  objects.push_back(Obj{
      .rgba = color,
      .body_id = body_id,
      .mesh = render_mesh,
  });

  return idx;
}

void Scene::ApplyImpulse(vec2f v) {
  for (const Obj &obj : objects) {
    if (b2Body_IsValid(obj.body_id)) {
      float mass = b2Body_GetMass(obj.body_id);
      float ix = v.x * mass;
      float iy = v.y * mass;
      b2Body_ApplyLinearImpulseToCenter(obj.body_id, {ix, iy}, true);
    }
  }
}

// Get the scene, using Cartesian coordinates.
std::vector<Rendering::Triangle> Scene::GetTriangles() {
  std::vector<Rendering::Triangle> scene;
  size_t num_triangles = 0;

  std::vector<std::span<const Obj>> layers = {
    bg_objects, objects, fg_objects,
  };

  for (auto layer : layers) {
    for (const Obj &obj : layer) {
      num_triangles += obj.mesh.size();
    }
  }
  scene.reserve(num_triangles);

  auto AddRenderLayer = [&scene](std::span<const Obj> layer) {
      for (const Obj &obj : layer) {
        // (These objects do not have box2d ids.)

        for (const Rendering::Triangle &tri : obj.mesh) {
          auto Transform = [](vec2f p) -> vec2f {
              return vec2f{
                .x = p.x,
                .y = HEIGHT - p.y,
              };
            };
          scene.push_back({
              Transform(tri.a),
              Transform(tri.b),
              Transform(tri.c),
              tri.rgba,
              tri.reserved,
            });
        }
      }
    };

  AddRenderLayer(bg_objects);

  for (const Obj &obj : objects) {
    if (!b2Body_IsValid(obj.body_id)) continue;

    b2Vec2 pos = b2Body_GetPosition(obj.body_id);
    b2Rot rot = b2Body_GetRotation(obj.body_id);

    auto Transform = [&](const Rendering::vec2f &v) -> Rendering::vec2f {
        float rx = rot.c * v.x - rot.s * v.y;
        float ry = rot.s * v.x + rot.c * v.y;
        return {
          .x = pos.x + rx,
          .y = HEIGHT - (pos.y + ry),
        };
      };

    for (const Rendering::Triangle &tri : obj.mesh) {
      scene.push_back({
          Transform(tri.a),
          Transform(tri.b),
          Transform(tri.c),
          tri.rgba,
          tri.reserved,
        });
    }
  }

  AddRenderLayer(fg_objects);

  return scene;
}

vec2f Scene::GetPosition(const Obj &obj) {
  CHECK(b2Body_IsValid(obj.body_id));
  b2Vec2 p = b2Body_GetPosition(obj.body_id);
  return vec2f{p.x, p.y};
}

vec2f Scene::GetVelocity(const Obj &obj) {
  CHECK(b2Body_IsValid(obj.body_id));
  b2Vec2 v = b2Body_GetLinearVelocity(obj.body_id);
  return vec2f{v.x, v.y};
}

float Scene::GetAngle(const Obj &obj) {
  CHECK(b2Body_IsValid(obj.body_id));
  b2Rot rot = b2Body_GetRotation(obj.body_id);
  return std::atan2(rot.s, rot.c);
}

float Scene::GetAngularVelocity(const Obj &obj) {
  CHECK(b2Body_IsValid(obj.body_id));
  return b2Body_GetAngularVelocity(obj.body_id);
}

bool Scene::IsSimulated(const Obj &obj) const {
  return b2Body_IsValid(obj.body_id);
}

