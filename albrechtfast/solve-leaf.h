
#ifndef _ALBRECHT_SOLVE_LEAF_H
#define _ALBRECHT_SOLVE_LEAF_H

#include <optional>

#include "bit-string.h"
#include "albrecht.h"

struct SolveLeaf {
  // Find a valid net (if it exists) where given face is a leaf
  // and the edge is not cut. Returns nullopt if none exists.
  static std::optional<BitString> FindLeafUnfolding(
      const Albrecht::AugmentedPoly &aug,
      int face_idx,
      int edge_idx);
};

#endif
