
#include "base/stringprintf.h"
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

static std::string IOString(const CellLibrary::Info &info) {
  std::string s;
  for (const CellLibrary::IO &in : info.inputs) {
    AppendFormat(&s, "  Input at x={} t={}\n", in.xblock,
                 TypeString(in.type));
  }
  for (const CellLibrary::IO &out : info.outputs) {
    AppendFormat(&s, "  Output at x={} t={}\n", out.xblock,
                 TypeString(out.type));
  }
  return s;
}

static void PrintLibrary() {
  CellLibrary library;
  static constexpr int INPUT_WIDTH = Levels::IN_WIDTH;

  std::optional<std::pair<Cell, int>> min_in;
  std::optional<std::pair<Cell, int>> min_out;

  auto ProcessPads =
    [&](const Cell &cell,
        std::span<const CellLibrary::IO> pads,
        std::optional<std::pair<Cell, int>> &overall_min,
        std::string_view pad_name) {
      if (pads.size() > 1) {
        std::vector<int> xblocks;
        xblocks.reserve(pads.size());
        for (const auto &pad : pads) xblocks.push_back(pad.xblock);
        std::sort(xblocks.begin(), xblocks.end());

        int local_min = xblocks[1] - xblocks[0] - INPUT_WIDTH;
        for (size_t i = 2; i < xblocks.size(); i++) {
          local_min = std::min(local_min,
                               xblocks[i] - xblocks[i-1] - INPUT_WIDTH);
        }
        Print("  min {} distance: {}\n", pad_name, local_min);
        if (!overall_min.has_value() ||
            local_min < overall_min.value().second) {
          overall_min = std::make_pair(cell, local_min);
        }
      }
    };

  Print("Non-parameterized cells in standard orientation:\n");
  for (Gate gate : ALL_GATES) {
    if (gate == SPACER || gate == WIREA || gate == WIREB) continue;
    Cell cell{gate, 0, false};
    CellLibrary::Info info = library.GetInfo(cell);
    Print("{}: width {}\n", GateString(gate), info.block_width);
    for (const CellLibrary::IO &in : info.inputs) {
      Print("  Input at x={} t={}\n", in.xblock, TypeString(in.type));
    }
    ProcessPads(cell, info.inputs, min_in, "input");

    for (const CellLibrary::IO &out : info.outputs) {
      Print("  Output at x={} t={}\n", out.xblock, TypeString(out.type));
    }
    ProcessPads(cell, info.outputs, min_out, "output");
  }

  if (min_in.has_value()) {
    const auto &[cell, v] = min_in.value();
    Print("Overall min input distance: {} ({})\n", v, CellString(cell));
  }
  if (min_out.has_value()) {
    const auto &[cell, v] = min_out.value();
    Print("Overall min output distance: {} ({})\n", v, CellString(cell));
  }
}

static void PrintWireLib() {
  CellLibrary library;
  Print("Available wires:\n");
  for (int e = CellLibrary::MAX_WIRE_EXP; e >= 0; e--) {
    int offset = 1 << e;
    Cell wa = CellLibrary::WireA(offset, CType::MIXED);
    CellLibrary::Info ainfo = library.GetInfo(wa);
    Cell wb = CellLibrary::WireB(offset, CType::MIXED);
    CellLibrary::Info binfo = library.GetInfo(wb);
    Print("Offset {}:\n"
          "Wire A({}), width {}:\n{}\n"
          "Wire B({}), width {}:\n{}\n",
          offset,
          wa.v, ainfo.block_width,
          IOString(ainfo),
          wb.v, binfo.block_width,
          IOString(binfo));
  }
}

int main(int argc, char **argv) {
  ANSI::Init();

  Simple();
  VerifyFlippedWidths();
  Print("\n");
  PrintWireLib();
  PrintLibrary();

  Print("OK\n");
  return 0;
}
