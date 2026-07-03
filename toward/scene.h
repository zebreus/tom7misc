#ifndef _TOWARD_SCENE_H
#define _TOWARD_SCENE_H

#include <cstdint>
#include <optional>
#include <vector>

#include "arcfour.h"
#include "box2d.h"
#include "geom/polygonization.h"
#include "rendering.h"
#include "yocto-math.h"

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
    // Can be used to attach application-specific info by the
    // client.
    // Otherwise, the Scene data structures should be free from
    // application logic.
    std::optional<uint64_t> user_data;
  };

  b2WorldId world_id = {};

  // Not simulated, but drawn behind the objects.
  std::vector<Obj> bg_objects;
  // Simulated.
  std::vector<Obj> objects;
  // Not simulated, but drawn on top of the objects.
  std::vector<Obj> fg_objects;

  Scene(bool walls = true);

  ~Scene();

  void Update();

  void AddDirt(ArcFour *rc);

  // True if every object is asleep, so the simulation is
  // quiescent. Note that this may be more conservative than
  // you want, if for example an object has fallen out of the arena.
  bool AllAsleep();

  // Attempt to place the mesh at pos, but then move it in the
  // reject_dir while it overlapping an existing object. If it exits
  // the arena (overlapping a wall) while we try to resolve (or some
  // unresolvable conflict occurs, then returns nullopt. Otherwise,
  // returns the deconflicted position.
  std::optional<vec2f> RejectObject(
      const Polygonization::Mesh &mesh, vec2f pos,
      vec2f reject_dir);

  void AddObject(const Polygonization::Mesh &mesh, uint32_t color,
                 // Initial position and angle. The angle is in
                 // radians and rotation happens around the local
                 // origin, which is not necessarily the center of mass.
                 vec2f pos, float angle,
                 // Velocity and angular velocity.
                 vec2f vel, float avel,
                 float restitution,
                 float friction);

  void AddFixedObject(const Polygonization::Mesh &mesh, uint32_t color,
                      vec2f pos,
                      float restitution,
                      float friction);

  void AddGraphics(const Polygonization::Mesh &mesh, uint32_t color,
                   vec2f pos, bool foreground);

  void ApplyImpulse(vec2f v);

  // Get the triangles for rendering, using Cartesian coordinates.
  std::vector<Rendering::Triangle> GetTriangles();

  Scene(Scene &&other) noexcept = default;
  Scene &operator=(Scene &&other) noexcept = default;

 private:
  void Attach(b2BodyId body_id, b2ShapeDef shape_def,
              const Polygonization::Mesh &mesh,
              uint32_t color);

  // Move-only.
  Scene(const Scene &) = delete;
  Scene &operator=(const Scene &) = delete;
};


#endif
