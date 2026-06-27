
// Take propositions and encode them as a circuit.

#ifndef _TOWARD_LAYOUT_H
#define _TOWARD_LAYOUT_H

#include <span>
#include <vector>

#include "cell-library.h"
#include "circuit.h"
#include "prop.h"

struct Layout {
  // The topmost layer needs variables as inputs.
  // This will match the input arity of the first layer.
  std::vector<int> input_vars;
  Circuit circuit;
};

// Props must all be in the same world.
Layout DoLayout(const CellLibrary &library,
                const World &world,
                std::span<const Prop> props);

#endif
