
#ifndef _ALBRECHT_SOLVE_LINE_H
#define _ALBRECHT_SOLVE_LINE_H

#include <optional>

#include "albrecht.h"
#include "arcfour.h"
#include "bit-string.h"

struct SolveLine {
  // Find a valid net (if it exists) where the spanning
  // tree is just a single linear path through all faces.
  // Returns nullopt if none exists.
  // Exhausting the search space is potentially exponential,
  // so
  static std::optional<BitString> FindLineUnfolding(
      const Albrecht::AugmentedPoly &aug);

  // Sample an unfolding that is a line. It may
  // or may not have overlap, but it will be a proper spanning
  // tree with a single path through all faces. Since such
  // paths don't necessarily exist, this may return nullopt.
  static std::optional<BitString> SampleLine(
      ArcFour *rc,
      const Albrecht::AugmentedPoly &aug,
      int max_attempts = 10000);
};

#endif
