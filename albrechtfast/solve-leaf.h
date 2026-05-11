
#ifndef _ALBRECHT_SOLVE_LEAF_H
#define _ALBRECHT_SOLVE_LEAF_H

#include <optional>

#include "albrecht.h"
#include "arcfour.h"
#include "bit-string.h"

struct SolveLeaf {
  // Find a valid net (if it exists) where given face is a leaf
  // and the edge is not cut. Returns nullopt if none exists.
  // Proving that none exists is exponential time, so it will
  // only work for polyhedra with a few dozen faces.
  static std::optional<BitString> FindLeafUnfolding(
      const Albrecht::AugmentedPoly &aug,
      int face_idx,
      int edge_idx,
      std::optional<double> max_stretch = {});

  // Sample an unfolding that has face_idx as a leaf. It may
  // or may not have overlap, but it will be a proper spanning
  // tree with the indicated face as a leaf.
  static BitString SampleFace(
      ArcFour *rc,
      const Albrecht::AugmentedPoly &aug,
      int face_idx);
};

#endif
