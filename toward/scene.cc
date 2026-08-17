
#include "scene.h"

#include <cmath>
#include <memory>
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
#include "constants.h"
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
static int world_count = 0;

static b2Rot MakeExactRot(float angle) {
  return b2Rot{std::cos(angle), std::sin(angle)};
}

static std::optional<b2WorldId> AcquireWorld() {
  b2WorldDef world_def = b2DefaultWorldDef();
  // XXX... necessary for parallelism?
  world_def.workerCount = 0;
  world_def.gravity = {0.0f, 9.8f};

  {
    MutexLock ml(&world_mutex);
    if (world_count == B2_MAX_WORLDS)
      return std::nullopt;
    b2WorldId world_id = b2CreateWorld(&world_def);
    world_count++;
    return world_id;
  }
}

static b2WorldId AcquireWorldOrDie() {
  auto wo = AcquireWorld();
  CHECK(wo.has_value()) << "Exceeded maximum per-process worlds";
  return wo.value();
}

std::unique_ptr<Scene> Scene::Create() {
  auto wo = AcquireWorld();
  if (!wo.has_value()) return {nullptr};
  return std::unique_ptr<Scene>(new Scene(wo.value()));
}

std::unique_ptr<Scene> Scene::CreateHibernating() {
  return std::unique_ptr<Scene>(new Scene(Hibernation::tag));
}

Scene::Scene() : Scene(AcquireWorldOrDie()) {}

Scene::Scene(b2WorldId world) : world_id(world) {}

Scene::Scene(Scene::Hibernation h) : world_id(std::nullopt) {}

Scene::~Scene() {
  MutexLock ml(&world_mutex);
  if (world_id.has_value()) {
    b2DestroyWorld(world_id.value());
    world_count--;
  }
}

void Scene::Update() {
  if (world_id.has_value()) {
    b2World_Step(world_id.value(), 1.0f / 120.0f, 8);
  }
}

void Scene::Hibernate() {
  if (!world_id.has_value()) {
    return;
  }

  for (Obj &obj : objects) {
    if (b2Body_IsValid(obj.body_id)) {
      HibernationState state;
      b2Vec2 p = b2Body_GetPosition(obj.body_id);
      state.pos = {p.x, p.y};

      b2Rot rot = b2Body_GetRotation(obj.body_id);
      state.angle = std::atan2(rot.s, rot.c);

      b2Vec2 v = b2Body_GetLinearVelocity(obj.body_id);
      state.vel = {v.x, v.y};
      state.avel = b2Body_GetAngularVelocity(obj.body_id);

      state.is_static = (b2Body_GetType(obj.body_id) == b2_staticBody);

      int count = b2Body_GetShapeCount(obj.body_id);
      std::vector<b2ShapeId> shapes(count);
      if (count > 0) {
        b2Body_GetShapes(obj.body_id, shapes.data(), count);
        state.restitution = b2Shape_GetRestitution(shapes[0]);
        state.friction = b2Shape_GetFriction(shapes[0]);

        for (b2ShapeId shape_id : shapes) {
          if (b2Shape_GetType(shape_id) == b2_polygonShape) {
            b2Polygon b2_poly = b2Shape_GetPolygon(shape_id);
            Polygon poly;
            poly.reserve(b2_poly.count);
            for (int i = 0; i < b2_poly.count; i++) {
              poly.push_back({b2_poly.vertices[i].x, b2_poly.vertices[i].y});
            }
            state.shapes.push_back(std::move(poly));
          }
        }
      }

      obj.hibernation_state = std::move(state);
      obj.body_id = {};
    }
  }

  {
    MutexLock ml(&world_mutex);
    b2DestroyWorld(world_id.value());
    world_count--;
  }
  world_id = std::nullopt;
}

bool Scene::Unhibernate() {
  if (world_id.has_value()) {
    return true;
  }

  auto wo = AcquireWorld();
  if (!wo.has_value()) return false;

  world_id = wo.value();

  for (Obj &obj : objects) {
    if (obj.hibernation_state.has_value()) {
      Attach(obj);
    }
  }

  return true;
}

bool Scene::AllAsleep() {
  if (!world_id.has_value()) {
    return true;
  }
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
  CHECK(world_id.has_value()) << "RejectObject requires an active simulation.";
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

static std::vector<Rendering::Triangle> MakeTriangles(
    const Polygonization::Mesh &mesh,
    uint32_t color);

static std::vector<Scene::Polygon> ExtractShapes(
    const Polygonization::Mesh &mesh) {
  std::vector<Scene::Polygon> shapes;
  for (const auto &poly : mesh.polygons) {
    if (poly.size() < 3) continue;
    Scene::Polygon p;
    p.reserve(poly.size());
    for (int idx : poly) {
      auto [px, py] = mesh.vertices[idx];
      p.push_back({(float)px, (float)py});
    }
    shapes.push_back(std::move(p));
  }

  if (shapes.empty()) {
    Scene::Polygon p;
    p.reserve(mesh.vertices.size());
    for (const auto &v : mesh.vertices) {
      auto [vx, vy] = v;
      p.push_back({(float)vx, (float)vy});
    }
    shapes.push_back(std::move(p));
  }
  return shapes;
}

size_t Scene::AddObject(const Polygonization::Mesh &mesh,
                        uint32_t color, vec2f pos, float angle,
                        vec2f vel, float avel,
                        float restitution,
                        float friction) {
  Obj obj;
  obj.rgba = color;
  obj.mesh = MakeTriangles(mesh, color);

  HibernationState state;
  state.pos = pos;
  state.angle = angle;
  state.vel = vel;
  state.avel = avel;
  state.restitution = restitution;
  state.friction = friction;
  state.is_static = false;
  state.shapes = ExtractShapes(mesh);

  obj.hibernation_state = std::move(state);

  if (world_id.has_value()) {
    Attach(obj);
  }

  size_t idx = objects.size();
  objects.push_back(std::move(obj));
  return idx;
}

size_t Scene::AddFixedObject(const Polygonization::Mesh &mesh,
                             uint32_t color, vec2f pos,
                             float restitution,
                             float friction) {
  Obj obj;
  obj.rgba = color;
  obj.mesh = MakeTriangles(mesh, color);

  HibernationState state;
  state.pos = pos;
  state.restitution = restitution;
  state.friction = friction;
  state.is_static = true;
  state.shapes = ExtractShapes(mesh);

  obj.hibernation_state = std::move(state);

  if (world_id.has_value()) {
    Attach(obj);
  }

  size_t idx = objects.size();
  objects.push_back(std::move(obj));
  return idx;
}

static std::vector<Rendering::Triangle> MakeTriangles(
    const Polygonization::Mesh &mesh,
    uint32_t color) {
  std::vector<Rendering::Triangle> render_mesh;

  for (const auto &poly : mesh.polygons) {
    if (poly.size() < 3)
      continue;

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
  if (b2Body_IsValid(obj.body_id)) {
    b2DestroyBody(obj.body_id);
    obj.body_id = {};
  }
  obj.hibernation_state = std::nullopt;
  obj.mesh.clear();
}

void Scene::Attach(Obj &obj) {
  CHECK(world_id.has_value());
  CHECK(obj.hibernation_state.has_value());
  HibernationState &state = obj.hibernation_state.value();

  b2BodyDef body_def = b2DefaultBodyDef();
  body_def.type = state.is_static ? b2_staticBody : b2_dynamicBody;
  body_def.position = {state.pos.x, state.pos.y};
  body_def.rotation = MakeExactRot(state.angle);
  body_def.linearVelocity = {state.vel.x, state.vel.y};
  body_def.angularVelocity = state.avel;

  obj.body_id = b2CreateBody(world_id.value(), &body_def);

  b2ShapeDef shape_def = b2DefaultShapeDef();
  shape_def.density = state.is_static ? 1.0f : 6.25f;
  shape_def.material.restitution = state.restitution;
  shape_def.material.friction = state.friction;

  bool has_shapes = false;
  for (const Polygon &poly : state.shapes) {
    if (poly.size() < 3) continue;

    std::vector<b2Vec2> pts;
    pts.reserve(poly.size());
    for (const vec2f &p : poly) {
      pts.push_back({p.x, p.y});
    }

    b2Hull hull = b2ComputeHull(pts.data(), pts.size());
    if (hull.count < 3) continue;

    b2Polygon b2_poly = b2MakePolygon(&hull, 0.0f);
    b2CreatePolygonShape(obj.body_id, &shape_def, &b2_poly);
    has_shapes = true;
  }

  if (!has_shapes) {
    if (VERBOSE) {
      Print("Object of color [{}⏹" ANSI_RESET "] {:08x} with "
            "no valid shapes! Falling back to AABB.\n",
            ANSI::ForegroundRGB32(obj.rgba), obj.rgba);
    }

    float min_x = 1e9f, max_x = -1e9f;
    float min_y = 1e9f, max_y = -1e9f;
    bool any_pts = false;

    for (const Polygon &poly : state.shapes) {
      for (const vec2f &v : poly) {
        if (v.x < min_x) min_x = v.x;
        if (v.x > max_x) max_x = v.x;
        if (v.y < min_y) min_y = v.y;
        if (v.y > max_y) max_y = v.y;
        any_pts = true;
      }
    }

    if (!any_pts) {
      min_x = -0.05f; max_x = 0.05f;
      min_y = -0.05f; max_y = 0.05f;
    }

    float w = max_x - min_x;
    float h = max_y - min_y;

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
      {(float)min_x, (float)max_y},
    };

    b2Hull hull = b2ComputeHull(aabb_pts, 4);
    if (hull.count >= 3) {
      b2Polygon b2_poly = b2MakePolygon(&hull, 0.0f);
      b2CreatePolygonShape(obj.body_id, &shape_def, &b2_poly);
    }
  }

  obj.hibernation_state = std::nullopt;
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
std::vector<Rendering::Triangle> Scene::GetTriangles() const {
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
    vec2f pos;
    float c, s;

    if (obj.hibernation_state.has_value()) {
      const HibernationState &hs = obj.hibernation_state.value();
      pos = hs.pos;
      c = std::cos(hs.angle);
      s = std::sin(hs.angle);
    } else if (b2Body_IsValid(obj.body_id)) {
      b2Vec2 b2pos = b2Body_GetPosition(obj.body_id);
      pos = {b2pos.x, b2pos.y};
      b2Rot rot = b2Body_GetRotation(obj.body_id);
      c = rot.c;
      s = rot.s;
    } else {
      continue;
    }

    auto Transform = [&](const Rendering::vec2f &v) -> Rendering::vec2f {
        float rx = c * v.x - s * v.y;
        float ry = s * v.x + c * v.y;
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
  if (obj.hibernation_state.has_value()) {
    return obj.hibernation_state.value().pos;
  }
  CHECK(b2Body_IsValid(obj.body_id));
  b2Vec2 p = b2Body_GetPosition(obj.body_id);
  return vec2f{p.x, p.y};
}

vec2f Scene::GetVelocity(const Obj &obj) {
  if (obj.hibernation_state.has_value()) {
    return obj.hibernation_state.value().vel;
  }
  CHECK(b2Body_IsValid(obj.body_id));
  b2Vec2 v = b2Body_GetLinearVelocity(obj.body_id);
  return vec2f{v.x, v.y};
}

float Scene::GetAngle(const Obj &obj) {
  if (obj.hibernation_state.has_value()) {
    return obj.hibernation_state.value().angle;
  }
  CHECK(b2Body_IsValid(obj.body_id));
  b2Rot rot = b2Body_GetRotation(obj.body_id);
  return std::atan2(rot.s, rot.c);
}

float Scene::GetAngularVelocity(const Obj &obj) {
  if (obj.hibernation_state.has_value()) {
    return obj.hibernation_state.value().avel;
  }
  CHECK(b2Body_IsValid(obj.body_id));
  return b2Body_GetAngularVelocity(obj.body_id);
}

bool Scene::IsSimulated(const Obj &obj) const {
  return b2Body_IsValid(obj.body_id) || obj.hibernation_state.has_value();
}

bool Scene::Hibernating() const {
  return !world_id.has_value();
}
