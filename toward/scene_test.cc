
#include "scene.h"

#include <cmath>
#include <memory>
#include <numbers>
#include <string_view>
#include <utility>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "constants.h"
#include "geom/polygonization.h"
#include "rendering.h"

static Polygonization::Mesh MakeDecagon() {
  Polygonization::Mesh mesh;
  std::vector<int> poly;
  for (int i = 0; i < 10; i++) {
    double theta = i * 2.0 * std::numbers::pi / 10.0;
    mesh.vertices.push_back({std::cos(theta), std::sin(theta)});
    poly.push_back(i);
  }
  mesh.polygons.push_back(poly);
  return mesh;
}

static Polygonization::Mesh MakeSurface() {
  Polygonization::Mesh mesh;
  mesh.vertices = {
    {-10.0, -10.0},
    {10.0, 10.0},
    {10.0, 11.0},
    {-10.0, -9.0}
  };
  mesh.polygons = {{0, 1, 2, 3}};
  return mesh;
}

static Polygonization::Mesh MakeBox(float x, float y, float w, float h) {
  Polygonization::Mesh mesh;
  mesh.vertices = {
    {x, y},
    {x + w, y},
    {x + w, y + h},
    {x, y + h}
  };
  mesh.polygons = {{0, 1, 2, 3}};
  return mesh;
}

static void RenderScene(std::string_view base, const Scene &scene) {
  std::unique_ptr<Rendering> r = CreateImageRendering(base);
  r->ClearBackground();
  std::vector<Rendering::Triangle> t1 = scene.GetTriangles();
  r->RenderScene({0.0f, 0.0f}, {Scene::WIDTH, Scene::HEIGHT}, t1);
}

static void TestHibernate() {
  Scene s1;
  Scene s2;

  Polygonization::Mesh decagon = MakeDecagon();
  Polygonization::Mesh surface = MakeSurface();
  Polygonization::Mesh floor =
    MakeBox(0.0f, Scene::HEIGHT - 1.0f, Scene::WIDTH, 1.0f);
  Polygonization::Mesh wall =
    MakeBox(Scene::WIDTH - 1.0f, 0.0f, 1.0f, Scene::HEIGHT);

  // The two scenes should be identical. Returns the decagon's index.
  auto BuildScene = [&](Scene *s) -> size_t {
      s->AddFixedObject(surface, 0xFFFFFFFF, {10.0f, Scene::HEIGHT - 10.0f},
                        0.2f, 0.2f);
      s->AddFixedObject(floor, 0xFFFFFFFF, {0.0f, 0.0f}, 0.2f, 0.2f);
      s->AddFixedObject(wall, 0xFFFFFFFF, {0.0f, 0.0f}, 0.2f, 0.2f);
      return s->AddObject(decagon, 0xFFFFFFFF, {5.0f, Scene::HEIGHT - 15.0f},
                          0.0f,
                          {0.0f, 0.0f}, 0.0f, 0.2f, 0.2f);
    };

  // We need the dynamic object's id.
  size_t dec1 = BuildScene(&s1);
  size_t dec2 = BuildScene(&s2);

  for (int i = 0; i < 256; i++) {
    s1.Update();

    if (i % 10 == 0) s2.Hibernate();
    if (i % 10 == 5) s2.Unhibernate();

    if (s2.Hibernating()) {
      s2.Unhibernate();
      s2.Update();
      s2.Hibernate();
    } else {
      s2.Update();
    }
  }

  s2.Unhibernate();

  vec2f p1 = s1.GetPosition(s1.objects[dec1]);
  vec2f p2 = s2.GetPosition(s2.objects[dec2]);

  float dx = p1.x - p2.x;
  float dy = p1.y - p2.y;
  float dist = std::sqrt(dx * dx + dy * dy);

  if (dist >= 1e-4f) {
    RenderScene("hibernate-fail-orig", s1);
    RenderScene("hibernate-fail-hiber", s2);
    LOG(FATAL) << "Hibernate test failed, dist = " << dist;
  }
}

static void TestBasicProperties() {
  Scene scene;
  Polygonization::Mesh decagon = MakeDecagon();

  size_t obj_idx = scene.AddObject(decagon, 0xFF0000FF, {1.0f, 2.0f}, 0.5f,
                                   {3.0f, 4.0f}, 1.5f, 0.5f, 0.5f);

  CHECK(!scene.Hibernating());

  const Scene::Obj &obj = scene.objects[obj_idx];

  vec2f pos = scene.GetPosition(obj);
  CHECK(std::abs(pos.x - 1.0f) < 1e-5f);
  CHECK(std::abs(pos.y - 2.0f) < 1e-5f);

  vec2f vel = scene.GetVelocity(obj);
  CHECK(std::abs(vel.x - 3.0f) < 1e-5f);
  CHECK(std::abs(vel.y - 4.0f) < 1e-5f);

  float angle = scene.GetAngle(obj);
  CHECK(std::abs(angle - 0.5f) < 1e-5f) << angle;

  float avel = scene.GetAngularVelocity(obj);
  CHECK(std::abs(avel - 1.5f) < 1e-5f);
}

static void TestGraphics() {
  Scene scene;
  Polygonization::Mesh decagon = MakeDecagon();

  scene.AddGraphics(decagon, 0x00FF00FF, {5.0f, 5.0f}, true);
  scene.AddGraphics(decagon, 0x0000FFFF, {2.0f, 2.0f}, false);

  CHECK(scene.fg_objects.size() == 1);
  CHECK(scene.bg_objects.size() == 1);
  CHECK(scene.objects.size() == 0);

  std::vector<Rendering::Triangle> tris = scene.GetTriangles();
  CHECK(tris.size() == 16);
}

static void TestDetach() {
  Scene scene;
  Polygonization::Mesh decagon = MakeDecagon();

  size_t obj1 = scene.AddObject(decagon, 0xFF0000FF, {1.0f, 2.0f}, 0.0f,
                                {0.0f, 0.0f}, 0.0f, 0.5f, 0.5f);
  size_t obj2 = scene.AddObject(decagon, 0x00FF00FF, {3.0f, 4.0f}, 0.0f,
                                {0.0f, 0.0f}, 0.0f, 0.5f, 0.5f);

  CHECK(scene.objects.size() == 2);
  CHECK(scene.IsSimulated(scene.objects[obj1]));
  CHECK(scene.IsSimulated(scene.objects[obj2]));

  scene.Detach(obj1);

  CHECK(!scene.IsSimulated(scene.objects[obj1]));
  CHECK(scene.IsSimulated(scene.objects[obj2]));
}

static void TestWorldLimits() {
  Print("Testing world limits...\n");
  std::vector<std::unique_ptr<Scene>> scenes;
  for (int i = 0; i < B2_MAX_WORLDS - 2; i++) {
    scenes.push_back(std::make_unique<Scene>());
  }

  {
    bool hit_limit = false;
    for (int i = 0; i < 8; i++) {
      std::unique_ptr<Scene> s = Scene::Create();
      if (s.get() == nullptr) {
        Print("Hit limit as expected!\n");
        hit_limit = true;
        break;
      }
      scenes.push_back(std::move(s));
    }

    CHECK(hit_limit) << "Failed to hit world slot limit";
  }

  // Check that sparse removal still frees up slots.
  Print("Remove some scenes...\n");
  CHECK(B2_MAX_WORLDS > 255 * 5) << "You'll need to adjust the test.";
  for (int i = 0; i < B2_MAX_WORLDS; i += 255) {
    scenes[i].reset();
  }

  Print("Now we should be able to create some...\n");
  for (int i = 0; i < 5; i++) {
    std::unique_ptr<Scene> s = Scene::Create();
    CHECK(s.get() != nullptr);
    scenes.push_back(std::move(s));
  }

  Print("Destroy all.\n");
  scenes.clear();

  Print("One more...\n");
  std::unique_ptr<Scene> s = Scene::Create();
  CHECK(s.get() != nullptr);
}

int main(int argc, char **argv) {
  ANSI::Init();

  TestHibernate();
  TestBasicProperties();
  TestGraphics();
  TestDetach();
  TestWorldLimits();

  Print("OK\n");

  return 0;
}
