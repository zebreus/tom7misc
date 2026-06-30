

#include "cell-library.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "base/stringprintf.h"
#include "circuit.h"
#include "level.h"

static void Simple() {
  // Simply creating the library validates that nothing is seriously
  // wrong with the SVG files.
  CellLibrary library;

  {
    CellLibrary::Info info = library.GetInfo(
        CellLibrary::Spacer(7));
    CHECK(info.block_width == 7);
    CHECK(info.inputs.empty());
    CHECK(info.outputs.empty());
  }

  {
    std::unique_ptr<Level> spacer1 =
      library.GetLevel(CellLibrary::Spacer(1));
    CHECK(spacer1.get() != nullptr);
    CHECK(spacer1->inputs.empty());
    CHECK(spacer1->outputs.empty());
    CHECK(spacer1->bodies.empty()) << "Expected that a spacer "
      "is empty, although it would not be wrong to define it "
      "to have some geometry in there.";
  }

}


static void VerifyFlippedWidths() {
  CellLibrary library;

  auto check_cell = [&library](Cell cell) {
    cell.flip = false;
    int normal_width = library.GetInfo(cell).block_width;
    cell.flip = true;
    int flipped_width = library.GetInfo(cell).block_width;
    CHECK(normal_width == flipped_width)
        << "Width mismatch for " << CellString(cell);
  };

  for (int g = 0; g <= CONST1; g++) {
    Gate gate = static_cast<Gate>(g);
    if (gate == SPACER || gate == WIREA || gate == WIREB) continue;
    check_cell(Cell{gate, 0, false});
  }

  check_cell(CellLibrary::Spacer(1));
  check_cell(CellLibrary::Spacer(5));
  check_cell(CellLibrary::WireA(1));
  check_cell(CellLibrary::WireA(8));
  check_cell(CellLibrary::WireB(2));
  check_cell(CellLibrary::WireB(16));
}

int main(int argc, char **argv) {
  ANSI::Init();

  Simple();
  VerifyFlippedWidths();

  Print("OK\n");
  return 0;
}
