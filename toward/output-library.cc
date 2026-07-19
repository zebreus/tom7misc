
#include "cell-library.h"

#include <algorithm>
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

static std::string IOString(const CellLibrary::Info &info) {
  std::string s;
  std::optional<int> min_x;
  std::optional<int> max_x;
  for (const CellLibrary::IO &in : info.inputs) {
    AppendFormat(&s, "  Input at x={} t={}\n", in.xblock,
                 TypeString(in.type));
    if (!min_x.has_value() || in.xblock < min_x.value()) min_x = in.xblock;
    if (!max_x.has_value() || in.xblock > max_x.value()) max_x = in.xblock;
  }
  for (const CellLibrary::IO &out : info.outputs) {
    AppendFormat(&s, "  Output at x={} t={}\n", out.xblock,
                 TypeString(out.type));
    if (!min_x.has_value() || out.xblock < min_x.value()) min_x = out.xblock;
    if (!max_x.has_value() || out.xblock > max_x.value()) max_x = out.xblock;
  }
  if (min_x.has_value() && max_x.has_value()) {
    int left_clearance = min_x.value();
    int right_clearance = info.block_width - max_x.value() - Levels::IN_WIDTH;
    AppendFormat(&s, "  Clearance: left {} right {}\n",
                 left_clearance, right_clearance);
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
  for (int offset : CellLibrary::WIRE_SIZES) {
    Cell wa = CellLibrary::Wire(offset, CellLibrary::Bias::RIGHT);
    CellLibrary::Info ainfo = library.GetInfo(wa);
    Print("Offset {}:\n"
          "Wire A({}), width {}:\n{}\n",
          offset,
          wa.v, ainfo.block_width,
          IOString(ainfo));

    if (offset < CellLibrary::SMALL_WIRE) {
      Cell wb = CellLibrary::Wire(offset, CellLibrary::Bias::LEFT);
      CellLibrary::Info binfo = library.GetInfo(wb);
      Print("Wire B({}), width {}:\n{}\n",
            wb.v, binfo.block_width,
            IOString(binfo));
    }
  }
}

int main(int argc, char **argv) {
  ANSI::Init();

  Print("\n");
  PrintWireLib();
  PrintLibrary();

  return 0;
}
