
// Take propositions and encode them as a circuit.

#ifndef _TOWARD_LAYOUT_H
#define _TOWARD_LAYOUT_H

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cell-library.h"
#include "circuit.h"
#include "prop.h"

struct Layout {
  // The topmost layer needs variables as inputs.
  // A future version might guarantee that CType is mixed.
  // This will match the input arity of the first layer.
  std::vector<std::pair<int, CType>> input_vars;
  Circuit circuit;
};

struct LayoutEngine {
  static std::unique_ptr<LayoutEngine> Create(const CellLibrary &library,
                                              const World &world);
  virtual ~LayoutEngine();

  // Props must all be in the same world.
  virtual Layout DoLayout(std::span<const Prop> props) = 0;

  virtual void SetVerbose(int v) = 0;

  // This stuff is just exposed for testing and visualization.

  // Layout cell is a working representation, where we have
  // a Cell (or perhaps an abstract cell) and the vector of
  // input propositions for it.
  struct LC {
    std::vector<Prop> inprops;
    Cell cell;
  };

  // What we want to do with a chute. This is thinking about the
  // bottom-up direction; "permute left" means a wire would slope
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

  virtual int MinClearanceClose() const = 0;
  virtual int MinClearanceFar() const = 0;

  virtual bool CanPlaceCell(int for_chute_ctx,
                            std::span<const Chute> top,
                            const std::vector<bool> &assigned,
                            std::span<const PC> next,
                            const Cell &cell,
                            int xpos) const = 0;

  static std::string_view DesireTypeString(DesireType dt);
  static std::string ChuteString(const Chute &chute);

 protected:
  LayoutEngine();
};

#endif
