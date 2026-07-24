
// For use internally within layout.cc. Represents
// the state while working on adding a single layer
// to the top of the circuit. We work bottom up, so
// newly added cells have their outputs attached to
// the inputs of the existing circuit.

#ifndef _TOWARD_LAYOUT_CANVAS_H
#define _TOWARD_LAYOUT_CANVAS_H

#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cell-library.h"
#include "circuit.h"
#include "prop.h"

struct LayoutCanvas {
  // Layout cell is a working representation, where we have a Cell (or
  // perhaps an abstract cell) and the vector of input propositions
  // for it.
  struct LC {
    std::vector<Prop> inprops;
    Cell cell;
  };

  // What we want to do with a chute. This is thinking about the
  // bottom-up direction; "flow left" means a wire would slope
  // like a backslash.
  enum DesireType {
    UNSPECIFIED,
    // Apply a gate to decompose the proposition.
    DECOMPOSE,
    // Apply a combiner so that we have separated inputs.
    UNCOMBINE,
    // Unduplicate adjacent identical propositions.
    UNDUP_LHS,
    UNDUP_RHS,
    // A matched pair that should be unseparated.
    UNSEPARATE_LHS,
    UNSEPARATE_RHS,
    // The chute is out of order and should swap to its left.
    EXCHANGE_LEFT,
    // ... or right.
    EXCHANGE_RIGHT,
    // The chute is in the right order, but needs to propagate
    // up or left/right to accommodate others.
    QUIESCE,
  };

  // Location and type of the transition between layers where
  // an input and output meet, plus working information during
  // the layout algorithm.
  struct Chute {
    int pos = 0;
    Prop prop = False();
    CType type = CType::MIXED;

    DesireType desire = DesireType::UNSPECIFIED;

    // The desired position in the global order.
    // If the prop and type are the same, the rank may be
    // the same.
    int rank = 0;

    // Exterior chutes that hold variables are done.
    bool done = false;
    // True if we've assigned something on the next layer.
    bool assigned = false;
    // True when we do not allow the chute to move during
    // spring solving. This is automatically set true when
    // we assign a chute, for example.
    bool anchored = false;
  };

  // A placed cell.
  struct PC {
    int xpos = 0;
    Cell cell;
    std::vector<Prop> inprops;
  };

  // Springs come between chutes, so there are #chutes - 1 of them.
  struct Spring {
    // The edge-to-edge distance that we want the chutes to be
    // (not including the width of the chutes themselves).
    // This must be set!
    int target_dist = -1;
    // Hard limit on the distance.
    int min_dist = 0;
    // Stiffness (penalties) for compressing or expanding.
    float compress = 1.0f;
    float expand = 1.0f;
  };

  std::vector<Spring> springs;

  // Update a spring in place with additional constraints.
  // The target distances are added (unless the current value
  // is -1, which means it was not set yet), and we take the max
  // min distance.
  static void UpdateSpring(Spring *spring,
                           int target_dist,
                           int min_dist,
                           float compress,
                           float expand);

  // Returns a desired position for each chute (left edge).
  // The position needs to be quantized and checked for feasibility,
  // naturally...
  std::vector<double> SolveSprings();


  LayoutCanvas(const CellLibrary &library);

  // Use FlattenInputs to create the chutes from the current
  // top layer.
  void Reset(std::vector<Chute> chutes_in);

  void SetVerbose(int v);

  // Assert that we are not already in a situation where we're
  // stuck (where no cell can be placed).
  void CheckNotStuck();

  // This stuff is just exposed for testing and visualization.

  static std::string_view DesireTypeString(DesireType dt);
  static std::string ChuteString(const Chute &chute);

  // The chutes on the top of the circuit.
  std::vector<Chute> chutes;

  // Is the chute already assigned to an output on the new layer?
  bool Assigned(int chute_idx) const;
  // Mark a chute as assigned (only once).
  void Assign(int chute_idx);

  void Anchor(int chute_idx) { chutes[chute_idx].anchored = true; }

  // Add a placed cell
  void AddNext(int xpos, const Cell &cell, std::vector<Prop> inprops);

  std::string DebugString() const;

  // Given the input chutes for the complete top layer,
  // and the in-progress next layer (next), is it possible
  // to place the cell in the next layer with its left edge
  // at xpos? Needs to check:
  //  - It does not overlap anything already in that layer
  //  - Its input chutes do not come within the safe clearance
  //    distance of neighboring input chutes. The safe clearance is
  //    the inter-chute distance such that we can fit a small
  //    wire (e.g. a 0-displacement wire) in either orientation
  //    on both chutes. This ensures that we never get completely
  //    stuck.
  bool CanPlaceCell(int for_chute_ctx,
                    const Cell &cell,
                    int xpos) const;

  // Convert the 'next' field to a proper layer with its
  // starting offset (might be negative).
  std::pair<std::vector<LC>, int> ConvertToLayer();

  // Compute chutes from layout cells.
  std::vector<Chute> FlattenInputs(
      std::span<const LC> top_layer) const;

 private:
  const CellLibrary &library;
  int verbose = 0;

  // The next layer, under construction. These should output
  // to the chutes. This is kept in sorted order by x position,
  // to make it faster to find overlap.
  std::vector<PC> next;
};

#endif
