
#ifndef _TOWARD_CELL_LIBRARY_H
#define _TOWARD_CELL_LIBRARY_H

#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

#include "circuit.h"
#include "level.h"

// Library of specific geometry implementing gates (circuit.h).

struct CellLibraryImpl;
struct CellLibrary {

  // Initialize, loading the geometry from SVG files.
  CellLibrary();
  ~CellLibrary();

  struct IO {
    // Offset of the input/output pad within the cell.
    int xblock = 0;
    CType type = CType::MIXED;
  };

  struct Info {
    int block_width = 0;

    std::vector<IO> inputs;
    std::vector<IO> outputs;
  };

  Info GetInfo(const Cell &cell) const;

  static std::string InfoString(const Info &info);

  // A new copy of the level geometry that implements the
  // cell, in its starting configuration.
  std::unique_ptr<Level> GetLevel(const Cell &cell) const;

  // Check that inputs are lined up with outputs, their
  // types match, and cells don't overlap. There should be
  // no unconnected inputs or outputs except for inputs on the
  // top layer, and outputs on the bottom layer.
  void DRC(const Circuit &circuit) const;

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
    0, 1, 2, /* 3, */ 4, /* 5, */ 8, 16, 32, 64,
  };

  static bool ValidWireSize(int w);

  // Wire A has its input at x=1 and output at x=1+offset.
  // It has tight clearance on the left (we say it is "right biased").
  // offset in WIRE_SIZES.
  static Cell WireA(int offset, CType type = CType::MIXED);

  // Wire B (left biased).
  // Input is at x=6
  // Output is at x=6 + offset
  // offset in WIRE_SIZES.
  static Cell WireB(int offset, CType type = CType::MIXED);

 private:
  // Private implementation.
  std::unique_ptr<CellLibraryImpl> impl;
};

#endif

