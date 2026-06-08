
#ifndef _ALBRECHT_SOLVE_VERTEX_H
#define _ALBRECHT_SOLVE_VERTEX_H

#include <optional>

#include "albrecht.h"
#include "arcfour.h"
#include "bit-string.h"

struct SolveVertex {
  // Find a valid net (if it exists) where every edge connected to the
  // vertex is cut, or return nullopt if none exists. Proving that
  // none exists is exponential time, so it will only work for
  // polyhedra with a few dozen faces.
  static std::optional<BitString> FindVertexUnfolding(
      const Albrecht::AugmentedPoly &aug,
      int vertex_idx);

  // Deterministic, single-threaded prover. Either returns nullopt
  // if there is no solution, or an integer indicating how many
  // steps it took us to find one, which is an approximate indication
  // of how difficult the instance is.
  static std::optional<int64_t> Prove(
      const Albrecht::AugmentedPoly &aug,
      int vertex_idx);

  // Sample an unfolding where all of the edges connected to the
  // vertex are cut. It may or may not have overlap, but it will be a
  // proper spanning tree with these cuts. Note that the vertex
  // may not necessarily be on topological leaves (if the faces
  // have more than three sides).
  static BitString SampleVertex(
      ArcFour *rc,
      const Albrecht::AugmentedPoly &aug,
      int vertex_idx);
};

#endif

