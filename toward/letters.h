
#ifndef _TOWARD_LETTERS_H
#define _TOWARD_LETTERS_H

#include <cstdint>
#include <memory>
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
  static std::unique_ptr<Letters> LoadFont(std::string_view filename);

  // Keyed by codepoint.
  std::unordered_map<uint32_t, Letter> letter;

  double GetKerning(uint32_t c1, uint32_t c2) {
    uint64_t k = KernKey(c1, c2);
    auto it = kerning.find(k);
    if (it != kerning.end()) return it->second;
    // XXX use k1's nominal width?
    return 1.0;
  }

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
