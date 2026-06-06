
#ifndef _BRECHTFAST_MAKE_SVG_H
#define _BRECHTFAST_MAKE_SVG_H

#include "albrecht.h"

#include <cstdint>
#include <optional>
#include <unordered_map>

#include "svg.h"

struct Highlights {
  // Maps a vertex index to RGBA color.
  std::unordered_map<int, uint32_t> vertex_color;
  // Maps an edge index to RGBA color.
  std::unordered_map<int, uint32_t> edge_color;
};

struct SVGOptions {
  uint32_t face_rgba = 0xCCCCFF44;
  uint32_t overlapping_face_rgba = 0xFF333344;
  uint32_t edge_rgba = 0x000000FF;
  float edge_stroke = 1.0;
  bool inserts = true;
  bool face_labels = true;
  bool edge_labels = true;
};

struct MakeSVG {
  // An SVG displaying the unfolded polyhedron.
  static SVG::Doc Make(const Albrecht::AugmentedPoly &aug,
                       const Albrecht::DebugResult &debug_result,
                       const std::optional<Highlights> &highlights = {},
                       const SVGOptions &options = SVGOptions{});

};

#endif
