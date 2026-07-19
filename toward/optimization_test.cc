
#include "optimization.h"

#include <algorithm>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "cell-library.h"
#include "circuit.h"
#include "drc.h"
#include "interesting-props.h"
#include "layout.h"
#include "prop.h"

using Bias = CellLibrary::Bias;

static void TestRemoveAllWireLayer() {
  CellLibrary library;

  Cell const0(Gate::CONST0);
  Cell wire_a1 = CellLibrary::Wire(1, Bias::RIGHT);
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
  Cell wire1 = CellLibrary::Wire(4, Bias::RIGHT);
  Cell wire2 = CellLibrary::Wire(1, Bias::RIGHT);
  wire2.flip = true;
  Cell wire3 = CellLibrary::Wire(5, Bias::RIGHT);
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



static void TestWindowed() {
  CellLibrary library;

  Cell const0(Gate::CONST0);
  Cell wire = CellLibrary::Wire(16, Bias::RIGHT);
  Cell sink(Gate::SINK);

  auto info_c = library.GetInfo(const0);
  auto info_w = library.GetInfo(wire);
  auto info_s = library.GetInfo(sink);

  int c_out_x = info_c.outputs[0].xblock;
  int w_in_x = info_w.inputs[0].xblock;
  int w_out_x = info_w.outputs[0].xblock;
  int s_in_x = info_s.inputs[0].xblock;

  // Strand 0 positions.
  int x0_c = 0;
  int x0_w = x0_c + c_out_x - w_in_x;
  int x0_s = x0_w + w_out_x - s_in_x;

  // Strand 1 is placed in parallel to make it "nontrivial".
  int offset = 300;
  int x1_c = x0_c + offset;
  int x1_w = x0_w + offset;
  int x1_s = x0_s + offset;

  int min_x = std::min({x0_c, x0_w, x0_s});
  if (min_x < 0) {
    x0_c -= min_x; x1_c -= min_x;
    x0_w -= min_x; x1_w -= min_x;
    x0_s -= min_x; x1_s -= min_x;
  }

  auto MakeLayer2 = [&](int x0, Cell cell0, int x1, Cell cell1) {
    std::vector<Cell> layer;
    if (x0 > 0) {
      layer.push_back(CellLibrary::Spacer(x0));
    }
    layer.push_back(cell0);
    int space = x1 - (x0 + library.GetInfo(cell0).block_width);
    CHECK(space >= 0) << "Overlap in test setup!";
    if (space > 0) {
      layer.push_back(CellLibrary::Spacer(space));
    }
    layer.push_back(cell1);
    return layer;
  };

  Layout layout;
  layout.input_vars = {};
  layout.circuit.layers.push_back(MakeLayer2(x0_c, const0, x1_c, const0));
  layout.circuit.layers.push_back(MakeLayer2(x0_w, wire, x1_w, wire));
  layout.circuit.layers.push_back(MakeLayer2(x0_s, sink, x1_s, sink));

  CHECK(layout.circuit.layers.size() == 3);

  DRC::CheckLayout(library, "windowed_before", layout);

  size_t before_size = CircuitSize(layout.circuit);

  Layout optimized = Optimization::Optimize(library, layout);

  DRC::CheckLayout(library, "windowed_after", optimized);

  size_t after_size = CircuitSize(optimized.circuit);

  CHECK(after_size < before_size)
      << "Expected circuit size to shrink! Before: " << before_size
      << " After: " << after_size;
  CHECK(optimized.circuit.layers.size() < layout.circuit.layers.size())
      << "Expected fewer layers due to passthrough removal!";
}

int main(int argc, char **argv) {
  ANSI::Init();

  TestRemoveAllWireLayer();
  TestStraightenZigZagWire();

  TestWindowed();


  Print("OK\n");
  return 0;
}
