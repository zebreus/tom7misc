
#ifndef _ALBRECHT_SOLVE_DUAL_LEAF_H
#define _ALBRECHT_SOLVE_DUAL_LEAF_H

#include <optional>

#include "albrecht.h"
#include "arcfour.h"
#include "bit-string.h"

struct SolveDualLeaf {
  // Find a valid net (if it exists) where the given edge is cut, and
  // the face on each side is a leaf in the graph. Returns nullopt if
  // none exists. Proving that none exists is exponential time, so it
  // will only work for polyhedra with a few dozen faces.
  static std::optional<BitString> FindDualLeafUnfolding(
      const Albrecht::AugmentedPoly &aug,
      int edge_idx);

  // Sample a dual-leaf unfolding for the given edge. It may or may
  // not have overlap, but it will be a proper spanning tree with the
  // edge cut and its two adjacent faces as leaves in the graph.
  static BitString SampleDualLeaf(
      ArcFour *rc,
      const Albrecht::AugmentedPoly &aug,
      int edge_idx);
};

#endif

