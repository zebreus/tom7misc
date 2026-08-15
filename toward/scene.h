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

  using Polygon = std::vector<vec2f>;

  // Object state, when hibernating.
  struct HibernationState {
    vec2f pos = {};
    float angle = 0.0;
    vec2f vel = {};
    // angular velocity
    float avel = 0.0;
    float restitution = 0.0;
    float friction = 0.0;
    bool is_static = false;

    // The shapes that make up the object; each a convex
    // polygon suitable for Box2D. They are in local coordinates.
    std::vector<Polygon> shapes;
  };

  struct Obj {
    uint32_t rgba = 0xFFFFFFFF;
    // background and foreground objects will not have a valid
    // body_id (not simulated). Same for objects that are detached
    // or dormant.
    b2BodyId body_id = {};
    std::vector<Rendering::Triangle> mesh;
    // When the simulation is hibernating, body_id is absent but the
    // data needed to reconstitute the simulation is in here. For
    // unsimulated objects, this will remain nullopt.
    std::optional<HibernationState> hibernation_state;
    // Can be used to attach application-specific info by the
    // client.
    // Otherwise, the Scene data structures should be free from
    // application logic.
    std::optional<uint64_t> user_data;
  };

  // The Box2D world that is simulating the scene.
  // nullopt if hibernating.
  std::optional<b2WorldId> world_id = std::nullopt;

  // Not simulated, but drawn behind the objects.
  std::vector<Obj> bg_objects;
  // Simulated.
  std::vector<Obj> objects;
  // Not simulated, but drawn on top of the objects.
  std::vector<Obj> fg_objects;

  Scene();
  ~Scene();

  // Run the simulation for one step. Not allowed when
  // hibernating.
  void Update();

  // Stop the simulation, freeing up global resources for Box2D.
  void Hibernate();
  // Restart the simulation, acquiring the global resources.
  // Note that world ids and body id may change; use user_data
  // for long-lived references.
  void Unhibernate();
  bool Hibernating() const;

  // True if every object is asleep, so the simulation is
  // quiescent. Note that this may be more conservative than
  // you want, if for example an object has fallen out of the arena
  // it will be awake forever.
  // When already hibernating, this will always return true.
  bool AllAsleep();

  // Get the position (etc.) of a simulated object. These can be
  // used on objects while they are hibernating.
  vec2f GetPosition(const Obj &obj);
  vec2f GetVelocity(const Obj &obj);
  float GetAngle(const Obj &obj);
  float GetAngularVelocity(const Obj &obj);

  // Attempt to place the mesh at pos, but then move it in the
  // reject_dir while it overlapping an existing object. If it exits
  // the arena while we try to resolve (or some unresolvable conflict
  // occurs, then returns nullopt. Otherwise, returns the deconflicted
  // position.
  std::optional<vec2f> RejectObject(
      const Polygonization::Mesh &mesh, vec2f pos,
      vec2f reject_dir);

  // Returns its index.
  size_t AddObject(const Polygonization::Mesh &mesh, uint32_t color,
                   // Initial position and angle. The angle is in
                   // radians and rotation happens around the local
                   // origin, which is not necessarily the center of mass.
                   vec2f pos, float angle,
                   // Velocity and angular velocity.
                   vec2f vel, float avel,
                   float restitution,
                   float friction);

  // Returns its index.
  size_t AddFixedObject(const Polygonization::Mesh &mesh, uint32_t color,
                        vec2f pos,
                        float restitution,
                        float friction);

  void AddGraphics(const Polygonization::Mesh &mesh, uint32_t color,
                   vec2f pos, bool foreground);

  // Remove an Obj by index into the objects vector. Detaches the
  // Box2D body and clears the mesh in place, so that indices into the
  // object vector stay stable. OK when hibernating. (You can just
  // std::vector::erase from the background and foreground objects,
  // as they are not part of the box2d simulation.)
  void Detach(size_t index);

  // True if the object is simulated (even if it is currently hibernating).
  bool IsSimulated(const Obj &obj) const;

  // Get the triangles for rendering, using Cartesian coordinates.
  std::vector<Rendering::Triangle> GetTriangles() const;

  Scene(Scene &&other) noexcept = default;
  Scene &operator=(Scene &&other) noexcept = default;

  // Stuff for debugging.
  void AddDirt(ArcFour *rc);
  void ApplyImpulse(vec2f v);

 private:
  size_t Attach(b2BodyId body_id, b2ShapeDef shape_def,
                const Polygonization::Mesh &mesh,
                uint32_t color);

  // Move-only.
  Scene(const Scene &) = delete;
  Scene &operator=(const Scene &) = delete;
};


#endif
