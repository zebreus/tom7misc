
#include "optimization.h"

#include <algorithm>
#include <memory>
#include <utility>
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
#include "render-circuit.h"
#include "util.h"

using Bias = CellLibrary::Bias;

static void TestRemoveAllWireLayer(const CellLibrary &library) {
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

static void TestStraightenZigZagWire(const CellLibrary &library) {
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



static void TestWindowed(const CellLibrary &library) {
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
  CHECK(optimized.circuit.layers.size() < layout.circuit.layers.size()) <<
    "Expected fewer layers due to cells moving up and empty "
    "layers being removed!";
}

static std::vector<Prop> TestProps() {
  std::vector<Prop> props;
  for (const Prop &p : SmallInterestingProps()) props.push_back(p);
  for (const Prop &p : MediumInterestingProps()) props.push_back(p);
  return props;
}

static void OptimizeInteresting(const CellLibrary &library) {
  World world;
  std::vector<Prop> props = TestProps();
  for (const Prop &prop : props) NameVars(&world, prop);

  for (const Prop &prop : props) {
    std::string name = PropString(prop);
    std::unique_ptr<LayoutEngine> le = LayoutEngine::Create(library, world);
    le->SetVerbose(0);
    std::vector<Prop> output = {prop};
    Layout layout = le->DoLayout(output);
    DRC::CheckLayout(library, name, layout);

    if (CircuitSize(layout.circuit) < 128) {
      Print(AWHITE("Before") ":\n{}\n",
            LayoutEngine::ToString(layout));
    }

    Layout opt_layout = Optimization::Optimize(library, layout);

    if (CircuitSize(opt_layout.circuit) < 128) {
      Print(AWHITE("After") ":\n{}\n",
            LayoutEngine::ToString(opt_layout));
    }

    DRC::AssertEquivalentLayout(library,
                                name, layout, opt_layout);
    Print("{} " AGREY("->") " {}\n",
          LayoutEngine::LayoutInfo(layout),
          LayoutEngine::LayoutInfo(opt_layout));
  }
}


static void TestResolveDisplacementUpward(const CellLibrary &library) {
  Cell wire4 = CellLibrary::Wire(4, Bias::RIGHT);
  std::vector<Layer> network;
  network.push_back({wire4});
  network.push_back({wire4});

  std::vector<int> deltas = {1};
  bool success = Optimization::ResolveDisplacementUpward(
      library, network, 0, deltas);
  CHECK(success) << "Failed to resolve displacement!";

  // Check that the bottom wire changed to WIRE(5) to absorb the +1 delta.
  CHECK(network.size() == 2);
  CHECK(network[1].size() == 1);
  CHECK(IsWire(network[1][0].gate));
  CHECK(network[1][0].v == 5);

  // The top layer should remain WIRE(4) because the delta was fully absorbed.
  CHECK(network[0].size() == 1);
  CHECK(network[0][0].v == 4);
}

static void TestResolveDisplacementMovesInputs() {
  CellLibrary library;

  // NOT gate passes displacement directly to its input.
  Cell not_gate(Gate::NOT);
  std::vector<Layer> network;
  network.push_back({not_gate});

  std::vector<int> deltas = {5};
  bool success = Optimization::ResolveDisplacementUpward(
      library, network, 0, deltas);
  CHECK(success) << "Should allow input delta to propagate without failing!";

  // The NOT gate should now be shifted by 5, meaning there's a spacer before it.
  CHECK(network.size() == 1);
  CHECK(network[0].size() == 2);
  CHECK(network[0][0].gate == Gate::SPACER);
  CHECK(network[0][0].v == 5);
  CHECK(network[0][1].gate == Gate::NOT);
}

static void TestResolveDisplacementFailsOverlap() {
  CellLibrary library;

  Cell wire4 = CellLibrary::Wire(4, Bias::RIGHT);
  std::vector<Layer> network;
  network.push_back({wire4, wire4});

  // Shift the first wire's output heavily to the right.
  // It will collide with the second wire.
  std::vector<int> deltas = {32};
  bool success = Optimization::ResolveDisplacementUpward(
      library, network, 0, deltas);
  CHECK(!success) << "Should fail due to overlap!";
}

static void TestOptimizeWindowCornerCases(const CellLibrary &library) {
  Cell const0(Gate::CONST0);
  Cell wire = CellLibrary::Wire(0, Bias::RIGHT);
  Cell not_gate(Gate::NOT);
  Cell sink(Gate::SINK);

  auto GetX = [&](Cell up, int x_up, int port_up, Cell down, int port_down) {
    int out_x = x_up + library.GetInfo(up).outputs[port_up].xblock;
    return out_x - library.GetInfo(down).inputs[port_down].xblock;
  };

  auto MakeLayer = [&](std::vector<std::pair<int, Cell>> items) {
    std::sort(items.begin(), items.end(), [](const auto &a, const auto &b) {
      return a.first < b.first;
    });
    std::vector<Cell> layer;
    int curr_x = 0;
    for (const auto &item : items) {
      int x = item.first;
      if (x > curr_x) {
        layer.push_back(CellLibrary::Spacer(x - curr_x));
      }
      layer.push_back(item.second);
      curr_x = x + library.GetInfo(item.second).block_width;
    }
    return layer;
  };

  int x0_m = 0;
  int x1_m = GetX(const0, x0_m, 0, wire, 0);
  if (x1_m < 0) { x0_m -= x1_m; x1_m = 0; }
  int x2_m = GetX(wire, x1_m, 0, not_gate, 0);
  if (x2_m < 0) { int s = -x2_m; x0_m += s; x1_m += s; x2_m = 0; }
  int x3_m = GetX(not_gate, x2_m, 0, sink, 0);
  if (x3_m < 0) { int s = -x3_m; x0_m += s; x1_m += s; x2_m += s; x3_m = 0; }

  {
    Layout layout;
    layout.input_vars = {};
    layout.circuit.layers.push_back(MakeLayer({{x0_m, const0}}));
    layout.circuit.layers.push_back(MakeLayer({{x1_m, wire}}));
    layout.circuit.layers.push_back(MakeLayer({{x2_m, not_gate}}));
    layout.circuit.layers.push_back(MakeLayer({{x3_m, sink}}));

    DRC::CheckLayout(library, "ow_success_before", layout);
    Layout optimized = Optimization::Optimize(library, layout);
    DRC::CheckLayout(library, "ow_success_after", optimized);
    CHECK(optimized.circuit.layers.size() < 4)
        << "Expected OptimizeWindow to reduce layers!";
  }

  {
    int x1_n = GetX(const0, x0_m, 0, not_gate, 0);
    int x2_n = GetX(not_gate, x1_n, 0, not_gate, 0);
    int x3_n = GetX(not_gate, x2_n, 0, sink, 0);

    int shift = 0;
    if (x1_n < shift) shift = -x1_n;
    if (x2_n < shift) shift = -x2_n;
    if (x3_n < shift) shift = -x3_n;

    Layout layout;
    layout.input_vars = {};
    layout.circuit.layers.push_back(MakeLayer({{x0_m + shift, const0}}));
    layout.circuit.layers.push_back(MakeLayer({{x1_n + shift, not_gate}}));
    layout.circuit.layers.push_back(MakeLayer({{x2_n + shift, not_gate}}));
    layout.circuit.layers.push_back(MakeLayer({{x3_n + shift, sink}}));

    DRC::CheckLayout(library, "ow_nonwire_before", layout);
    size_t before_layers = layout.circuit.layers.size();
    Layout optimized = Optimization::Optimize(library, layout);
    DRC::CheckLayout(library, "ow_nonwire_after", optimized);
    CHECK(optimized.circuit.layers.size() == before_layers)
        << "Should not optimize if cell is fed by a non-wire.";
  }

  {
    int wire_width = library.GetInfo(wire).block_width;
    int not_width = library.GetInfo(not_gate).block_width;

    int b_x0 = x0_m + 500;
    int b_x1 = GetX(const0, b_x0, 0, sink, 0);
    int target_b_x1 = x1_m + wire_width + 1;
    b_x0 += (target_b_x1 - b_x1);
    b_x1 = target_b_x1;

    if (b_x0 >= x0_m + library.GetInfo(const0).block_width &&
        b_x1 < x2_m + not_width) {

      Layout layout;
      layout.input_vars = {};
      layout.circuit.layers.push_back(
          MakeLayer({{x0_m, const0}, {b_x0, const0}}));
      layout.circuit.layers.push_back(
          MakeLayer({{x1_m, wire}, {b_x1, sink}}));
      layout.circuit.layers.push_back(MakeLayer({{x2_m, not_gate}}));
      layout.circuit.layers.push_back(MakeLayer({{x3_m, sink}}));

      DRC::CheckLayout(library, "ow_overlap_before", layout);
      size_t before_layers = layout.circuit.layers.size();
      Layout optimized = Optimization::Optimize(library, layout);
      DRC::CheckLayout(library, "ow_overlap_after", optimized);
      CHECK(optimized.circuit.layers.size() == before_layers)
          << "Should fail optimization due to overlap in layer i.";
    }
  }
}

static void OptimizeModest(const CellLibrary &library) {
  World world{.symbol_names = {"a", "b", "c", "d", "e", "f", "g", "h"}};
  std::unique_ptr<LayoutEngine> le = LayoutEngine::Create(library, world);
  Prop a{Var{.id = 0}}, b{Var{.id = 1}}, c{Var{.id = 2}}, d{Var{.id = 3}},
    e{Var{.id = 4}}, f{Var{.id = 5}}, g{Var{.id = 6}}, h{Var{.id = 7}};

  std::vector<Prop> output = {
    BalanceProp(
        (-e & -d & (f | c)) |
        (g & h & a & b & c) |
        (f & ((b & d & a) | (a & -b))) |
        ((-f) & (-(a & -d & (b | c))))
                ),
  };
  le->SetVerbose(0);
  // le->SetWriteImages(true);
  Layout layout = le->DoLayout(output);
  RenderCircuit(library, layout.circuit).Save("opt-test-modest.png");
  DRC::CheckLayout(library, "modest", layout);
  Util::WriteFileBytes("opt-test-modest.layout",
                       LayoutEngine::Serialize(layout));
  Layout opt_layout = Optimization::Optimize(library, layout);
  RenderCircuit(library, layout.circuit).Save("opt-test-modest-opt.png");
  DRC::CheckLayout(library, "odest-opt", opt_layout);
  Util::WriteFileBytes("opt-test-modest-opt.layout",
                       LayoutEngine::Serialize(opt_layout));

  DRC::AssertEquivalentLayout(library,
                              "modest", layout, opt_layout);
  Print("{} " AGREY("->") " {}\n",
        LayoutEngine::LayoutInfo(layout),
        LayoutEngine::LayoutInfo(opt_layout));
}


int main(int argc, char **argv) {
  ANSI::Init();

  CellLibrary library;

  TestResolveDisplacementUpward(library);
  TestResolveDisplacementMovesInputs();
  TestResolveDisplacementFailsOverlap();
  Print("Resolve displacement OK.\n");

  TestOptimizeWindowCornerCases(library);

  TestRemoveAllWireLayer(library);
  TestStraightenZigZagWire(library);

  TestWindowed(library);

  OptimizeInteresting();

  OptimizeModest(library);

  Print("OK\n");
  return 0;
}
