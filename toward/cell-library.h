
#ifndef _TOWARD_CELL_LIBRARY_H
#define _TOWARD_CELL_LIBRARY_H

#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

#include "circuit.h"
#include "level.h"
#include "inline-vector.h"

// Library of specific geometry implementing gates (circuit.h).

struct CellLibraryImpl;
struct CellLibrary {

  // Initialize, loading the geometry from SVG files.
  CellLibrary();
  ~CellLibrary();

  struct IO {
    // Offset of the input/output pad within the cell.
    uint8_t xblock = 0;
    CType type = CType::MIXED;
  };

  struct Info {
    int block_width = 0;

    InlineVector<IO> inputs;
    InlineVector<IO> outputs;
  };

  Info GetInfo(const Cell &cell) const;
  // Same as GetInfo(cell).block_width, but doesn't allocate the
  // vectors; good for inner loops!
  int GetWidth(const Cell &cell) const;

  static std::string InfoString(const Info &info);

  // A new copy of the level geometry that implements the
  // cell, in its starting configuration.
  std::unique_ptr<Level> GetLevel(const Cell &cell) const;

  std::string DebugString(const Circuit &circuit) const;

  // Wires are asymmetric (even "vertical" wires have internal slopes
  // to prevent the objects from getting too fast). This is the very
  // minimum "close side" and "far side" clearance that we need in
  // order to guarantee that we can place some wire on an input.
  // Clearance does not include the width of the input itself. We use
  // this to check that we don't completely block a nearby input when
  // we place a cell. (We want to at least be able to propagate the
  // input upward with a wire.)
  int MinClearanceClose() const;
  int MinClearanceFar() const;

  // Access to parameterized cells.
  // These will abort on invalid arguments.

  // Can get a spacer of any positive width.
  static Cell Spacer(int width);

  // Wires are not necessarily symmetric; we have internal slopes so
  // that the pieces don't get too fast as they drop. This means that
  // for narrow wires, the cell will stick out on one side. The wires
  // all natively slope down and to the right like a backslash;
  // for the reverse slope, flip these.

  static constexpr std::initializer_list<int> WIRE_SIZES = {
    // Small wires (A/B variants exist).
    0, 1, 2, 3,
    // Large wires.
    4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
    21, 32, 42, 45, 64,
    80, 89,
    128, 180,
  };

  // If a wire has a displacement less than this, then it has
  // both A and B variants. Otherwise, we get the same wire for
  // both biases.
  static constexpr int SMALL_WIRE = 4;

  static bool ValidWireSize(int w);

  enum class Bias {
    // Tight clearance on the left; may stick out on the right.
    // This is wire shape A for small wires.
    RIGHT,
    // Tight clearance on the right; may stick out on the left.
    // This is wire shape B for small wires.
    LEFT,
  };

  // Get the wire with the given offset and bias. For small wires,
  // the bias affects whether this is wire shape A or B. For larger
  // wires, the bias is ignored. The type does not affect the wire
  // shape.
  static Cell Wire(int offset, Bias bias, CType type = CType::MIXED);

 private:
  // Private implementation.
  std::unique_ptr<CellLibraryImpl> impl;
};

#endif

