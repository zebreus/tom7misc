
#ifndef _TOWARD_CELL_LIBRARY_H
#define _TOWARD_CELL_LIBRARY_H

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

  // Access to parameterized cells.
  // These will abort on invalid arguments.

  // Can get a spacer of any positive width.
  static Cell Spacer(int width);

  // 2^6 = 64
  static constexpr int MAX_WIRE_EXP = 6;

  // Wire A:
  //  Input is at x=1
  //  Output is at x=1 - offset
  //     for offset in (0, 1, 2, 4, 8, 16, 32, 64).
  static Cell WireA(int offset, CType type = CType::MIXED);
  // Wire B:
  // Input is at x=6
  // Output is at x=6 + offset
  //     for offset in (0, 1, 2, 4, 8, 16, 32, 64).
  static Cell WireB(int offset, CType type = CType::MIXED);

 private:

  // Private implementation.
  std::unique_ptr<CellLibraryImpl> impl;
};

#endif

