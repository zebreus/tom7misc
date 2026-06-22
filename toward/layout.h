
#ifndef _TOWARD_LAYOUT_H
#define _TOWARD_LAYOUT_H

#include <vector>

// Lay out components to form a circuit.

// The circuit is a series of layers; each one computes a
// cval (chute value) for some propositions at some x
// locations. A cval's type can be MIXED, which means that
// it contains a '0' glyph or a '1' glyph indicating the
// value of the proposition. It can also be ZERO, meaning
// that it contains a '0' glyph only if the proposition
// is false. Or it can be ONE, meaning that it contains
// a '1' glyph only if the proposition is true. These last
// two cases are called "separated" values; usually we
// have a pair of outputs (ZERO, ONE) for a proposition
// when working with separated values.

enum CType {
  ZERO,
  ONE,
  MIXED,
};

struct CellIO {
  // Offset of the input/output pad within the cell.
  int xblock = 0;
  CType type = MIXED;
};

struct CellDef {
  // Width of the cell in blocks. It's assumed that there
  // is no geometry outside of this (including implied IO rails).
  // All cells are the same height (48 including input, but not
  // including output).
  int width = 0;

  // The function computed is not specified, but we do know the
  // types of the inputs and outputs.
  std::vector<CellIO> inputs;
  std::vector<CellIO> outputs;
};

struct Layer {
  // TODO
};

#endif
