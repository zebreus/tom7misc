
#include "optimization.h"

#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "cell-library.h"
#include "circuit.h"
#include "layout.h"
#include "rc.h"

static void TestRemoveAllWireLayer() {
  CellLibrary library;

  Cell const0(Gate::CONST0);
  Cell wire_a1 = CellLibrary::WireA(1);
  Cell sink(Gate::SINK);

  int const0_out_x = library.GetInfo(const0).outputs[0].xblock;
  int wire_in_x = library.GetInfo(wire_a1).inputs[0].xblock;
  int wire_out_x = library.GetInfo(wire_a1).outputs[0].xblock;
  int sink_in_x = library.GetInfo(sink).inputs[0].xblock;

  // Calculate x positions for cells such that their ports line up exactly.
  int x0 = 0;
  int x1 = 0;
  int x2 = 0;

  if (const0_out_x < wire_in_x) {
    x0 = wire_in_x - const0_out_x;
  } else {
    x1 = const0_out_x - wire_in_x;
  }

  int target_sink_in = x1 + wire_out_x;
  if (target_sink_in < sink_in_x) {
    int shift = sink_in_x - target_sink_in;
    x0 += shift;
    x1 += shift;
    x2 = 0;
  } else {
    x2 = target_sink_in - sink_in_x;
  }

  auto MakeLayer = [](int x, Cell cell) {
    std::vector<Cell> layer;
    if (x > 0) {
      layer.push_back(CellLibrary::Spacer(x));
    }
    layer.push_back(cell);
    return layer;
  };

  Layout layout;
  layout.input_vars = {};
  layout.circuit.layers.push_back(MakeLayer(x0, const0));
  layout.circuit.layers.push_back(MakeLayer(x1, wire_a1));
  layout.circuit.layers.push_back(MakeLayer(x2, sink));

  CHECK(layout.circuit.layers.size() == 3);

  // Make sure it's valid before we start!
  DRC::CheckLayout(library, "before", layout);

  Layout optimized = Optimization::Optimize(library, layout);

  DRC::CheckLayout(library, "after", optimized);

  CHECK(optimized.circuit.layers.size() == 2)
      << "All-wire layer should be removed.";

  auto GetNonSpacers = [](const Layer &layer) {
    std::vector<Cell> cells;
    for (const Cell &c : layer) {
      if (c.gate != Gate::SPACER) {
        cells.push_back(c);
      }
    }
    return cells;
  };

  std::vector<Cell> opt_l0 = GetNonSpacers(optimized.circuit.layers[0]);
  CHECK(opt_l0.size() == 1);
  CHECK(opt_l0[0].gate == Gate::CONST0);

  std::vector<Cell> opt_l1 = GetNonSpacers(optimized.circuit.layers[1]);
  CHECK(opt_l1.size() == 1);
  CHECK(opt_l1[0].gate == Gate::SINK);
}

int main(int argc, char **argv) {
  ANSI::Init();

  TestRemoveAllWireLayer();

  Print("OK\n");
  return 0;
}
