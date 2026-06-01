
#ifndef _ALBRECHT_SOLVE_STRONG_H
#define _ALBRECHT_SOLVE_STRONG_H

#include <optional>

#include "bit-string.h"
#include "albrecht.h"

struct SolveStrong {
  // Find an unfolding (if it exists) where the described edge
  // appears on the 2D convex hull (on the given face). Requires
  // that the edge is on that face, of course.
  // This implies the edge is cut. Returns nullopt if none exists,
  // which is the case for some polyhedra!
  static std::optional<BitString> FindStrongUnfolding(
      const Albrecht::AugmentedPoly &aug,
      int face_idx,
      int edge_idx);
};

#endif
