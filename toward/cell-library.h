
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

  // Access to parameterized cells.
  // These will abort on invalid arguments.

  // Can get a spacer of any positive width.
  static Cell Spacer(int width);

  // Wire A:
  //  Input is at x=1
  //  Output is at x=1 - offset
  //     for offset in (0, 1, 2, 4, 8, 16, 32, 64).
  static Cell WireA(int k);
  // Wire B:
  // Input is at x=6
  // Output is at x=6 + offset
  //     for offset in (0, 1, 2, 4, 8, 16, 32, 64).
  static Cell WireB(int k);

 private:
  // Private implementation.
  std::unique_ptr<CellLibraryImpl> impl;
};

#endif

