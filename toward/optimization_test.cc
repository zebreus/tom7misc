
#include "optimization.h"

#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "cell-library.h"
#include "circuit.h"
#include "layout.h"
#include "drc.h"

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

static void TestStraightenZigZagWire() {
  CellLibrary library;

  Cell const0(Gate::CONST0);
  Cell wire1 = CellLibrary::WireA(4);
  Cell wire2 = CellLibrary::WireA(1);
  wire2.flip = true;
  Cell wire3 = CellLibrary::WireA(5);
  Cell sink(Gate::SINK);

  std::vector<Cell> cells = {const0, wire1, wire2, wire3, sink};
  std::vector<int> in_x(cells.size(), 0);
  std::vector<int> out_x(cells.size(), 0);

  for (size_t i = 0; i < cells.size(); ++i) {
    auto info = library.GetInfo(cells[i]);
    if (!info.inputs.empty()) in_x[i] = info.inputs[0].xblock;
    if (!info.outputs.empty()) out_x[i] = info.outputs[0].xblock;
  }

  // Calculate x positions for cells such that their ports line up exactly.
  std::vector<int> X(cells.size(), 0);
  for (size_t i = 0; i < cells.size() - 1; ++i) {
    X[i+1] = X[i] + out_x[i] - in_x[i+1];
  }

  int min_x = 0;
  for (int x : X) {
    if (x < min_x) min_x = x;
  }
  for (int &x : X) x -= min_x;

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
  for (size_t i = 0; i < cells.size(); ++i) {
    layout.circuit.layers.push_back(MakeLayer(X[i], cells[i]));
  }

  CHECK(layout.circuit.layers.size() == 5);

  // Make sure it's valid before we start!
  DRC::CheckLayout(library, "zigzag_before", layout);

  Layout optimized = Optimization::Optimize(library, layout);

  DRC::CheckLayout(library, "zigzag_after", optimized);

  CHECK(optimized.circuit.layers.size() == 2)
      << "Zig-zag wires should be optimized away entirely. Got:\n"
      << LayoutEngine::ToString(optimized);

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

static void RealExamples() {

}

int main(int argc, char **argv) {
  ANSI::Init();

  TestRemoveAllWireLayer();
  TestStraightenZigZagWire();

  Print("OK\n");
  return 0;
}
