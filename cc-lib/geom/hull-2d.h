
#ifndef _CC_LIB_GEOM_HULL_2D_H
#define _CC_LIB_GEOM_HULL_2D_H

#include <span>
#include <vector>

#include "yocto-math.h"

struct Hull2D {
  using vec2 = yocto::vec<double, 2>;

  static bool IsHullConvex(std::span<const vec2> points,
                           std::span<const int> polygon);

  // Each method returns a convex hull (a single polygon) as indices
  // into the vertex list.

  // The intuitive "gift wrapping" algorithm. This function doesn't
  // handle collinear or coincident points well, so it's not really
  // recommended.
  static std::vector<int> GiftWrap(std::span<const vec2> vertices);

  // Compute the convex hull, using Graham's scan. O(n lg n).
  static std::vector<int> GrahamScan(std::span<const vec2> vertices);

  // Compute the convex hull, using the QuickHull algorithm.
  // Asymptotically O(n lg n), but I found it to be slower for
  // inputs of around ~100 vertices.
  static std::vector<int> QuickHull(std::span<const vec2> v);
};

#endif
