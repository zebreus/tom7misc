
#ifndef _TOWARD_LETTERS_H
#define _TOWARD_LETTERS_H

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "geom/polygons.h"
#include "geom/polygonization.h"

struct Letter {
  // A mesh of convex polygons.
  // Coordinates are computer graphics style (y-down). Note that
  // the origin is at the top left of the glyph, however, not the
  // baseline.
  Polygonization::Mesh mesh;

  // The y-coordinate of the baseline in the same coordinate system.
  double baseline_y = 0.0;

  // The nominal horizontal advance (width) of this letter. See also
  // the kerning table below.
  double width = 0.0;

  // TODO: center of mass, moment, AABB, etc.
};

struct Letters {
  // If triangle_cells is true, the result will all be triangles,
  // and the shape will be subdivided finely (e.g. for FEA).
  static std::unique_ptr<Letters> LoadFont(std::string_view filename,
                                           bool triangle_cells = false);

  // Keyed by codepoint.
  std::unordered_map<uint32_t, Letter> letter;

  // The nominal distance between baselines of consecutive lines of text.
  double line_height = 1.0;

  // Multiply mesh coordinates (and baseline, width, etc.) by this
  // in order to go from the unit scale to the original font metrics.
  // Everything else is using the normalized unit scale.
  double scale = 1.0;

  double GetKerning(uint32_t c1, uint32_t c2) const;

 private:
  // The kerning table gives the distance to advance (i.e. including
  // the first letter's normal width absent kerning) for a specific
  // pair of codepoints. Only present for codepoints in the letters
  // map.
  static constexpr uint64_t KernKey(uint32_t a, uint32_t b) {
    return (uint64_t{a} << 32) | uint64_t{b};
  }
  std::unordered_map<uint64_t, double> kerning;
};

#endif
