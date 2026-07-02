
// For use internally within layout.cc. Represents
// the state while working on adding a single layer
// (bottom up).

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
    UNDUP,
    UNSEPARATE,
    // The chute is out of order and should swap to its left.
    EXCHANGE_LEFT,
    // ... or right.
    EXCHANGE_RIGHT,
    // The chute is in order, and should flow to a relative offset
    // of its current position (number of blocks, in desire_val).
    FLOW,
    // The chute is basically where we want it, but it can move out
    // of the way to avoid conflicts.
    QUIESCE,
  };

  // Location and type of the transition between layers where
  // an input and output meet.
  struct Chute {
    int pos = 0;
    Prop prop = False();
    CType type = CType::MIXED;

    DesireType desire = DesireType::UNSPECIFIED;
    int desire_val = 0;

    // Exterior chutes that hold variables are done.
    bool done = false;
  };

  // A placed cell.
  struct PC {
    int xpos = 0;
    Cell cell;
    std::vector<Prop> inprops;
  };

  LayoutCanvas(const CellLibrary &library);

  // Use FlattenInputs to create the chutes from the current
  // top layer.
  void Reset(std::vector<Chute> top);

  void SetVerbose(int v);

  // This stuff is just exposed for testing and visualization.

  static std::string_view DesireTypeString(DesireType dt);
  static std::string ChuteString(const Chute &chute);

  // The chutes on the top of the circuit.
  std::vector<Chute> chutes;

  // Is the chute already assigned to an output on the new layer?
  bool Assigned(int chute_idx);
  // Mark a chute as assigned (only once).
  void Assign(int chute_idx);

  // The next layer, under construction. These should output
  // to the chutes.
  std::vector<PC> next;

  // Given the input chutes for the complete top layer,
  // and the in-progress next layer (next), is it possible
  // to place the cell in the next layer with its left edge
  // at xpos? Needs to check:
  //  - It does not overlap anything already in that layer
  //  - It does not block off any chutes on the top layer
  //    (this does not include the chutes that match up
  //    to the cell's output, though!). Being blocked off
  //    is a non-trivial property: We can get close
  //    to an input as long as we have a lot of space on
  //    the other side, and that space can be populated
  //    without blocking further cells!
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
  // Whether a given chute from chutes has been assigned.
  std::vector<bool> assigned;
};

#endif
