
#include "cell-library.h"

#include <format>
#include <memory>
#include <string_view>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
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

  auto CheckCell = [&library](Cell cell) {
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
    CheckCell(Cell{gate, 0, false});
  }

  CheckCell(CellLibrary::Spacer(1));
  CheckCell(CellLibrary::Spacer(5));
  CheckCell(CellLibrary::Wire(1, CellLibrary::Bias::RIGHT));
  CheckCell(CellLibrary::Wire(8, CellLibrary::Bias::RIGHT));
  CheckCell(CellLibrary::Wire(2, CellLibrary::Bias::LEFT));
  CheckCell(CellLibrary::Wire(16, CellLibrary::Bias::LEFT));
}

static void VerifyWireOffsets() {
  CellLibrary library;

  for (int offset : CellLibrary::WIRE_SIZES) {
    auto CheckWire = [&](std::string_view style, const Cell &cell) {
        CellLibrary::Info info = library.GetInfo(cell);
        auto Err = [&]{
            return std::format("Wire{}({}):\n{}\n",
                               style, offset,
                               CellLibrary::InfoString(info));
          };
        CHECK(info.inputs.size() == 1) << Err();
        CHECK(info.outputs.size() == 1) << Err();
        CHECK(info.inputs[0].xblock + offset == info.outputs[0].xblock) <<
          Err();
      };

    CheckWire("A", CellLibrary::Wire(offset, CellLibrary::Bias::RIGHT));
    CheckWire("B", CellLibrary::Wire(offset, CellLibrary::Bias::LEFT));
  }
}

int main(int argc, char **argv) {
  ANSI::Init();

  Simple();
  VerifyFlippedWidths();
  VerifyWireOffsets();

  Print("OK\n");
  return 0;
}
