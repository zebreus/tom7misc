
#ifndef _TOWARD_LAYOUT_H
#define _TOWARD_LAYOUT_H

#include <vector>

#include "prop.h"

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

enum class CType {
  ZERO,
  ONE,
  MIXED,
};

struct CellIO {
  // Offset of the input/output pad within the cell.
  int xblock = 0;
  CType type = CType::MIXED;
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

enum Gate : uint8_t {
  SPACER,
  AND0110,
  NOT,
  SEPARATOR,
  // TODO: The type of a wire is special; it propagates
  // from its input.
  WIRE,
  XCHG00,
  XCHG01,
  XCHG10,
  XCHG11,
  DUPSEP0011,
  SINK,
  CONST0,
  CONST1,
};

struct Cell {
  Gate gate = Gate::SPACER;
  // Gates like spacers and wires are parameterized.
  int v = 0;
  // Flip horizontally.
  bool flip = false;
};

using Layer = std::vector<Cell>;
struct Circuit {
  std::vector<Layer> layers;
};

// number of inputs, outputs
std::pair<int, int> GateSize(Gate gate);
std::pair<int, int> LayerSize(const Layer &layer);

int CellWidth(const Cell &cell);

// Check that inputs are lined up with outputs, and that
// their types match.
void DRC(const Circuit &circuit);

// All chutes are boolean functions (some proposition)
// but could be represented different ways.
struct Func {
  Prop prop;
  CType type;
};

// Compose the layer with the functions to get new ones. This ignores
// the positions of the inputs/outputs (keeping only the order) and
// aborts on length mismatch. Use DRC.
std::vector<Func> Transform(const Layer &layer, const std::vector<Func> &funcs);

#endif
