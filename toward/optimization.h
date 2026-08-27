
#ifndef _TOWARD_OPTIMIZATION_H
#define _TOWARD_OPTIMIZATION_H

#include <optional>
#include <span>

#include "circuit.h"
#include "layout.h"
#include "cell-library.h"

struct StatusBar;

// Optimize circuit layout.
struct Optimization {

  // Return an equivalent circuit that may be smaller.
  // We are especially interested in reducing the number
  // of layers and increasing the density.
  static Layout Optimize(const CellLibrary &library,
                         const Layout &layout,
                         StatusBar *status = nullptr);

  // Attempt to resolve a horizontal displacement of output chutes by
  // rewriting the network upward.
  //
  // The requested displacement is represented by a `start_chute` index and a
  // vector of `deltas`. Chutes before `start_chute` or after
  // `start_chute + deltas.size() - 1` have a requested delta of 0.
  //
  // For each layer, working from the bottom up, the function uses dynamic
  // programming to find new horizontal positions for all cells and new shapes
  // for wires such that:
  // 1. Their output chutes are exactly at the requested shifted positions.
  // 2. Cells do not overlap (maintaining valid horizontal spacing).
  // 3. The absolute displacements passed to the inputs are minimized.
  //
  // If a valid non-overlapping configuration cannot be found, the
  // function returns false and leaves the layers unmodified.
  // Otherwise, it updates the layers in place and returns true. If
  // the displacement reaches the top inputs of the network without
  // being fully absorbed, the network inputs will move.
  static bool ResolveDisplacementUpward(
      const CellLibrary &library,
      std::span<Layer> network,
      int start_chute,
      std::span<const int> deltas);

  // Attempt to resolve a horizontal displacement of input chutes by
  // rewriting the network downward.
  //
  // Similar to ResolveDisplacementUpward, but works from the top down.
  // The requested displacement is represented by a `start_chute` index and a
  // vector of `deltas` applied to the input chutes.
  //
  // If the displacement reaches the bottom outputs of the network without
  // being fully absorbed, the network outputs will move.
  static bool ResolveDisplacementDownward(
      const CellLibrary &library,
      std::span<Layer> network,
      int start_chute,
      std::span<const int> deltas);

  // Attempt to resolve a displacement applied to a "beam" of chutes
  // by rewriting the network upward. A beam is a sequence of
  // contiguous chutes that can move rigidly together.
  //
  // Similar to ResolveDisplacementUpward, but allows the shifted
  // section to choose a rigid displacement from the range
  // [min_beam_disp, max_beam_disp].
  //
  // Returns the chosen displacement for the beam, or std::nullopt
  // if no valid configuration was found within the bounds.
  static std::optional<int> ResolveBeamDisplacementUpward(
      const CellLibrary &library,
      std::span<Layer> network,
      int start_chute,
      std::span<const int> deltas,
      int min_beam_disp,
      int max_beam_disp);

};

#endif
