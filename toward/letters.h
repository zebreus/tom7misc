
#ifndef _TOWARD_LETTERS_H
#define _TOWARD_LETTERS_H

#include <cstdint>
#include <optional>
#include <string_view>
#include <unordered_map>

#include "geom/polygons.h"
#include "geom/polygonization.h"

struct Letter {
  // A mesh of convex polygons.
  Polygonization::Mesh mesh;
  // TODO: center of mass, moment, AABB, etc.
};

struct Letters {
  static std::optional<Letters> LoadFont(std::string_view filename);

  // Keyed by codepoint.
  std::unordered_map<uint32_t, Letter> letter;
};

#endif
