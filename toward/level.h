
#ifndef _TOWARD_LEVEL_H
#define _TOWARD_LEVEL_H

#include <memory>
#include <optional>
#include <string_view>
#include <vector>
#include <cstdint>

#include "geom/polygonization.h"
#include "svg.h"
#include "toward-util.h"

struct Scene;

struct LevelBody {
  Polygonization::Mesh mesh;
  uint32_t color = 0xFFFFFFFF;
  vec2f pos = {0.0, 0.0};
  // If true, then it is moved by physics. If false,
  // bodies can collide with it, but this body
  // never moves.
  bool dynamic = false;
};

// The starting state of the level.
struct Level {
  std::vector<LevelBody> bodies;
  bool scene_walls = true;
};

struct Levels {

  // Create a one or zero object. You need to set the
  // position and color.
  static LevelBody One();
  static LevelBody Zero();

  static constexpr int BLOCKS_ACROSS = 80;
  static constexpr int BLOCKS_DOWN = 54;

  // Size of a unit block in the level. About 8 inches in the scene.
  static constexpr float BLOCK_SIZE = 0.2f;

  // SVG is nominally 1920x1080. Scene is 19.2x10.8.
  static constexpr float SVG_SCALE = 100.0f;

  static std::unique_ptr<Level> LoadSVG(std::string_view filename);

  static void SaveSVG(const Level &level, std::string_view filename);

  static std::unique_ptr<Scene> CreateScene(const Level &level);

  // Recognize a "1" symbol in the SVG data. This is a simple 1x4
  // block rectangle.
  static std::optional<vec2f> IsSVGOne(const SVG::GraphicsState &outer_state,
                                       const SVG::Node &node);

  // Mostly for internal use.
  static void AddNodesToLevel(const SVG::Node &node,
                              const SVG::GraphicsState &state,
                              Level *level);
};

#endif
