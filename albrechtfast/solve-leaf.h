
#ifndef _ALBRECHT_SOLVE_LEAF_H
#define _ALBRECHT_SOLVE_LEAF_H

#include <optional>

#include "bit-string.h"
#include "albrecht.h"

struct SolveLeaf {
  // Find a valid net (if it exists) where given face is a leaf
  // and the edge is not cut. Returns nullopt if none exists.
  // Proving that none exists is exponential time, so it will
  // only work for polyhedra with a few dozen faces.
  static std::optional<BitString> FindLeafUnfolding(
      const Albrecht::AugmentedPoly &aug,
      int face_idx,
      int edge_idx);
};

#endif
