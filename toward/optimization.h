
#ifndef _TOWARD_OPTIMIZATION_H
#define _TOWARD_OPTIMIZATION_H

#include "layout.h"
#include "cell-library.h"

// Optimize circuit layout.
struct Optimization {

  // Return an equivalent circuit that may be smaller.
  // We are especially interested in reducing the number
  // of layers and increasing the density.
  //
  // XXX: Doesn't seem to work yet?
  static Layout Optimize(const CellLibrary &library,
                         const Layout &layout);

};

#endif
