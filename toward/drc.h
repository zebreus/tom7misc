
#ifndef _TOWARD_DRC_H
#define _TOWARD_DRC_H

#include <string_view>

#include "cell-library.h"
#include "circuit.h"
#include "layout.h"

struct DRC {
  // "Design Rules Check" for the circuit; aborts if
  // something is wrong (e.g. unconnected gates or overlapping cells).
  //
  // Check that inputs are lined up with outputs, their
  // types match, and cells don't overlap. There should be
  // no unconnected inputs or outputs except for inputs on the
  // top layer, and outputs on the bottom layer.
  static void CheckCircuit(const CellLibrary &library,
                           std::string_view error_context,
                           const Circuit &circuit);

  // The above, but also check that we have the right number of inputs.
  static void CheckLayout(const CellLibrary &library,
                          std::string_view error_context,
                          const Layout &layout);

  // Check both layouts. Verify that the two have the same I/O behavior:
  // the same input variables/types in same order, and the same functions
  // of those variables as outputs. If something is wrong, aborts with
  // a message containing error_context.
  static void AssertEquivalentLayout(const CellLibrary &library,
                                     std::string_view error_context,
                                     const Layout &a, const Layout &b);
};

#endif
