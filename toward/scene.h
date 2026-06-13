#ifndef _TOWARD_SCENE_H
#define _TOWARD_SCENE_H

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "yocto-math.h"

#include "box2d.h"
#include "geom/polygonization.h"
#include "rendering.h"
#include "arcfour.h"

using vec2f = yocto::vec2f;

// Handles the current configuration of objects and physics.
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

  Scene();

  ~Scene();

  void Update();

  void AddDirt(ArcFour *rc);

  // Attempt to place the mesh at (x, y), but then move it in the
  // reject_dir while it overlapping an existing object. If it exits
  // the arena (overlapping a wall) while we try to resolve (or some
  // unresolvable conflict occurs, then returns nullopt. Otherwise,
  // returns the deconflicted position.
  std::optional<std::pair<float, float>> RejectObject(
      const Polygonization::Mesh &mesh, float x, float y,
      vec2f reject_dir);

  void AddObject(const Polygonization::Mesh &mesh, uint32_t color,
                 float x, float y, float vx, float vy,
                 float restitution = 0.7f);

  void ApplyImpulse(vec2f v);

  // Get the triangles for rendering, using Cartesian coordinates.
  std::vector<Rendering::Triangle> GetScene();
};


#endif
