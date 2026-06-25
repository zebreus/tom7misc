
#ifndef _TOWARD_CIRCUIT_H
#define _TOWARD_CIRCUIT_H

#include <span>
#include <string>
#include <string_view>
#include <utility>
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

enum Gate : uint8_t {
  SPACER,
  AND0110,
  NOT,
  NOT01,
  SEPARATOR,
  SELFXCHG01,
  SELFXCHG10,
  WIREA,
  WIREB,
  XCHG00,
  XCHG01,
  XCHG10,
  XCHG11,
  DUPSEP0011,
  SINK,
  CONST0,
  CONST1,
  // perhaps also const empty?
};

struct Cell {
  Gate gate = Gate::SPACER;
  // Gates like spacers and wires are parameterized.
  // This has unspecified meaning.
  int v = 0;
  // Flip horizontally.
  bool flip = false;
};

using Layer = std::vector<Cell>;
struct Circuit {
  std::vector<Layer> layers;
};

// number of inputs, outputs
std::pair<int, int> GateArity(Gate gate);
std::pair<int, int> LayerArity(const Layer &layer);

std::string_view GateString(Gate g);
std::string CellString(const Cell &cell);

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

// Transform just the one cell. Assumes the input is the correct size.
std::vector<Func> TransformCell(const Cell &cell,
                                std::span<const Func> in);

#endif
