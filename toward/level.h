
#ifndef _TOWARD_LEVEL_H
#define _TOWARD_LEVEL_H

#include <memory>
#include <optional>
#include <string_view>
#include <vector>
#include <cstdint>

#include "geom/polygonization.h"
#include "scene.h"
#include "svg.h"
#include "toward-util.h"

enum class LevelItem {
  ONE,
  ZERO,
};

enum class LevelLayer {
  BACKGROUND,
  PHYSICAL,
  FOREGROUND,
};

struct LevelBody {
  // Polygon mesh for the body.
  // Coordinates are in Level space, which is y-down.
  Polygonization::Mesh mesh;
  uint32_t color = 0xFFFFFFFF;
  vec2f pos = {0.0f, 0.0f};
  float angle = 0.0f;
  // linear and angular velocity. Unused for dynamic bodies.
  vec2f vel = {0.0f, 0.0f};
  float avel = 0.0f;
  // This would be similar to a dead-blow hammer. Plain PTFE
  // would have restitution more like 0.3 (hard to get a good
  // figure).
  float restitution = 0.01f;
  // Realistic for PTFE.
  float friction = 0.04f;
  // If true, then it is moved by physics. If false,
  // bodies can collide with it, but this body
  // never moves.
  bool dynamic = false;
  // If deleted, the body is ignored for simulation, etc.
  // This is useful when we have indices pointing at the
  // bodies array (e.g. from the simulation).
  bool deleted = false;
  // Some bodies are instances of a special thing, e.g. a
  // '0' or '1'.
  std::optional<LevelItem> item;

  // If false, then this object is rendered but not simulated.
  // The physics fields above become meaningless.
  LevelLayer layer = LevelLayer::PHYSICAL;
};

// The starting state of the level.
struct Level {
  std::vector<LevelBody> bodies;
  // The location (x coordinate of its left edge) of each input, in
  // ascending order.
  std::vector<int> inputs;
  // The location (x coordinate of its left edge) of each output, in
  // ascending order.
  std::vector<int> outputs;
  bool scene_walls = true;
};


// New standardized level representation:
// A 2x1 level is 96 by 48 blocks.
// In a standard 1920x1080 frame, we have 6 blocks at the bottom (y
// block index 48) that are reserved for the outputs. The only
// geometry allowed here is the output regions, because this will
// overlap the input regions. The input regions take up the first 5
// rows. Geometry is allowed up here but not recommended. The input
// regions are 5x5 and overlap the bottom 5x5 square of the 5x6 output
// region. (Output regions are one square taller because we happen to
// have 6 blocks of space, and this also helps during validation to
// detect when two objects end up in the output.)
//
// Input and output regions only appear on integer block boundaries,
// and must appear at y=0 and y=48 (in blocks). During simulation
// there will be an implied one block wide wall on both sides of the
// input and output (outside the region). The input regions may not
// overlap, nor output regions.
struct Levels {
  // Color of an input rectangle.
  static constexpr uint32_t INPUT_COLOR = 0xAAFFFFFF;
  // Color of an output rectangle.
  static constexpr uint32_t OUTPUT_COLOR = 0xFFAAAAFF;

  // Measured in blocks.
  static constexpr int IN_WIDTH = 5;
  static constexpr int IN_HEIGHT = 5;
  static constexpr int OUT_WIDTH = 5;
  static constexpr int OUT_HEIGHT = 6;

  // These are measured in blocks.
  static constexpr int IN_Y = 0;
  static constexpr int OUT_Y = 48;

  // Full scene size, including the output region.
  static constexpr float WIDTH = Scene::WIDTH;
  static constexpr float HEIGHT = Scene::HEIGHT;

  // The nominal playfield, not including the output region (OUT_HEIGHT).
  static constexpr int BLOCKS_ACROSS = 96;
  static constexpr int BLOCKS_DOWN = 48;

  // Size of a unit block in the level. About 8 inches in the scene.
  static constexpr float BLOCK_SIZE = 0.2f;

  // SVG is nominally 1920x1080. Scene is 19.2x10.8.
  static constexpr float SVG_SCALE = 100.0f;

  static std::unique_ptr<Level> LoadSVG(std::string_view filename,
                                        bool verbose = true);

  struct Options {
    bool include_text = false;
    // Text is dynamic even if the paths are filled.
    // If false, the text must be outlines (like with paths).
    bool all_text_dynamic = true;
    // Discard paths that have less than 20% alpha; we sometimes
    // use these to ensure the view box is what we want.
    bool discard_low_alpha = true;
  };

  static std::unique_ptr<Level> LoadSVGExt(const Options &options,
                                           std::string_view filename,
                                           bool verbose = true);

  static void SaveSVG(const Level &level, std::string_view filename);

  // Flip the level horizontally, assuming a bounding box of `block_width` blocks.
  static void FlipLevel(Level *level, int block_width);

  // Add standard chute walls to both the input and output. The chutes
  // extend into the space where the connected level would be.
  static void AddChutes(Level *level, uint32_t in_color, uint32_t out_color);

  // user_data will be the index of the body in the level.
  static std::unique_ptr<Scene> CreateScene(const Level &level);

  // For interactive editing, with an existing scene. Usually you just
  // want to use CreateScene from a static Level.
  static void AddBodyToScene(Scene *scene, const LevelBody &level_body,
                             std::optional<uint64_t> user_data = {});

  // Create a one or zero object. You need to set the
  // position and color.
  static LevelBody One();
  static LevelBody Zero();
  // Mouse cursor.
  static LevelBody Cursor();
  // Create a fixed rectangular body.
  static LevelBody WallRect(vec2f center, int blockwidth, int blockheight,
                            uint32_t color = 0x888888FF);

  // Recognize an input. This a rectangle 5x5 blocks in size, with
  // color INPUT_COLOR. It does not become a body.
  static std::optional<vec2f> IsInput(const SVG::GraphicsState &outer_state,
                                      const SVG::Node &node);
  static std::optional<int> IsStandardInput(
      const SVG::GraphicsState &outer_state,
      const SVG::Node &node);

  // Recognize an output. This a rectangle 5x6 blocks in size, with
  // color OUTPUT_COLOR. It does not become a body.
  static std::optional<vec2f> IsOutput(const SVG::GraphicsState &outer_state,
                                       const SVG::Node &node);
  static std::optional<int> IsStandardOutput(
      const SVG::GraphicsState &outer_state,
      const SVG::Node &node);

  // Recognize a "1" symbol in the SVG data. This is a simple 1x4
  // block rectangle.
  static std::optional<vec2f> IsSVGOne(const SVG::GraphicsState &outer_state,
                                       const SVG::Node &node);

  // Recognize a "0" symbol in the SVG Data. This is a radius 2 circle
  // with a radius 1 circle cut out of it.
  static std::optional<vec2f> IsSVGZero(const SVG::GraphicsState &outer_state,
                                        const SVG::Node &node);

 private:
  // Mostly for internal use.
  static void AddNodesToLevel(std::string_view error_context,
                              const Options &options,
                              const SVG::Node &node,
                              const SVG::GraphicsState &state,
                              Level *level,
                              bool verbose,
                              LevelLayer layer,
                              std::optional<float> restitution = std::nullopt,
                              std::optional<float> friction = std::nullopt);
};

#endif
