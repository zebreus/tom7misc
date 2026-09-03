
#include "optimization.h"

#include <algorithm>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "cell-library.h"
#include "circuit.h"
#include "dense-int-set.h"
#include "drc.h"
#include "interesting-props.h"
#include "layout.h"
#include "prop.h"
#include "render-circuit.h"
#include "status-bar.h"
#include "threadutil.h"
#include "timer.h"
#include "util.h"

using Bias = CellLibrary::Bias;

static void TestRemoveAllWireLayer(const CellLibrary &library) {
  Print("{}\n", __func__);
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

static void TestStraightenParallelWires(const CellLibrary &library) {
  Print("{}\n", __func__);
  Cell const0(Gate::CONST0);
  Cell wire8 = CellLibrary::Wire(8, CellLibrary::Bias::RIGHT);
  Cell wire0 = CellLibrary::Wire(0, CellLibrary::Bias::RIGHT);
  Cell sink(Gate::SINK);

  auto info_c = library.GetInfo(const0);
  auto info_w8 = library.GetInfo(wire8);
  auto info_s = library.GetInfo(sink);

  int c_w = info_c.block_width;

  int x1_c = 0;
  int x1_w = x1_c + info_c.outputs[0].xblock - info_w8.inputs[0].xblock;
  int x1_s = x1_w + info_w8.outputs[0].xblock - info_s.inputs[0].xblock;

  // Place Strand 2 far enough right to prevent wire and sink
  // overlaps. Wire(8) extends right, and Sinks need left clearance.
  // 12 units is enough to avoid overlap, which also gives Strand 1
  // enough room to fully straighten.
  int x2_c = x1_c + c_w + 12;

  auto info_w0 = library.GetInfo(wire0);
  int x2_w = x2_c + info_c.outputs[0].xblock - info_w0.inputs[0].xblock;
  int x2_s = x2_w + info_w0.outputs[0].xblock - info_s.inputs[0].xblock;

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

  Layout layout;
  layout.input_vars = {};
  layout.circuit.layers.push_back(MakeLayer({{x1_c, const0}, {x2_c, const0}}));
  layout.circuit.layers.push_back(MakeLayer({{x1_w, wire8}, {x2_w, wire0}}));
  layout.circuit.layers.push_back(MakeLayer({{x1_s, sink}, {x2_s, sink}}));

  DRC::CheckLayout(library, "parallel_before", layout);

  Layout optimized = Optimization::Optimize(library, layout);

  DRC::CheckLayout(library, "parallel_after", optimized);

  if (optimized.circuit.layers.size() != 2) {
    Print("After:\n{}\n",
          LayoutEngine::ToString(optimized));

    LOG(FATAL) << "Expected the wire layer to be fully removed!";
  }
}

static void TestStraightenZigZagWire(const CellLibrary &library) {
  Print("{}\n", __func__);
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
  Print("{}\n", __func__);
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
  Print("{}\n", __func__);
  StatusBar status(1);

  World world;
  std::vector<Prop> props = TestProps();
  for (const Prop &prop : props) NameVars(&world, prop);

  std::mutex m;
  int done = 0;
  UnParallelApp(
      props,
      [&](const Prop &prop) {
        std::string name = PropString(prop);
        Print("Run " ABLUE("{}") " ...\n", name);
        std::unique_ptr<LayoutEngine> le =
          LayoutEngine::Create(library, world);
        le->SetVerbose(0);
        std::vector<Prop> output = {prop};
        Layout layout = le->DoLayout(output);
        std::optional<std::string> err_before =
          DRC::GetLayoutError(library, name, layout);

        if (CircuitSize(layout.circuit) < 128) {
          status.Print(ABLUE("{}") " " AWHITE("before") ":\n{}\n",
                       name,
                       LayoutEngine::ToString(layout));
        }

        CHECK(!err_before.has_value()) << name << " fails DRC before:\n"
                                       << err_before.value();

        Timer opt_timer;
        Layout opt_layout = Optimization::Optimize(library, layout);
        status.Print(ABLUE("{}") " optimized in {}\n",
                     name, ANSI::Time(opt_timer.Seconds()));

        if (CircuitSize(opt_layout.circuit) < 128) {
          status.Print(ABLUE("{}") " " AWHITE("After") ":\n{}\n",
                       name,
                       LayoutEngine::ToString(opt_layout));
        }

        DRC::AssertEquivalentLayout(library,
                                    name, layout, opt_layout);
        status.Print("{} " AGREY("->") " {}\n",
                     LayoutEngine::LayoutInfo(layout),
                     LayoutEngine::LayoutInfo(opt_layout));

        MutexLock ml(&m);
        done++;

        std::string ctrs = Optimization::DebugCounters();
        status.Print("After {}:\n{}\n", name, ctrs);

        status.Progress(done, props.size(), "Interesting");
      },
      8);
}

static void TestResolveBeamShiftUpward(const CellLibrary &library) {
  Print("{}\n", __func__);
  Cell gate(Gate::NOT);

  std::vector<Layer> network;
  // Place two gates separated by a spacer of 3.
  int spacer_width = 3;
  network.push_back({gate, CellLibrary::Spacer(spacer_width), gate});
  network.push_back({gate, CellLibrary::Spacer(spacer_width), gate});

  std::vector<int> deltas = {0}; // Ask the beam (left gate) to shift right by
                                 // at least spacer_width + 2.
  // The available space is only spacer_width, so this should fail.
  DenseIntSet fail_set =
      DenseIntSet::Top((spacer_width + 10) - (spacer_width + 2) + 1);
  DenseIntSet shift_fail = Optimization::ResolveBeamShiftUpward(
      library, network, 0, deltas, spacer_width + 2, fail_set);
  CHECK(shift_fail.Empty()) << "Should fail due to collision!";

  // Ask the beam to shift right by at least 1, up to spacer_width + 10.
  // It should succeed and pick a shift that fits (e.g. 1).
  DenseIntSet succ_set = DenseIntSet::Top((spacer_width + 10) - 1 + 1);
  DenseIntSet shift_succ = Optimization::ResolveBeamShiftUpward(
      library, network, 0, deltas, 1, succ_set);
  CHECK(!shift_succ.Empty()) << "Should succeed by eating the spacer!";

  // What if we want to shift the RIGHT gate (chute 1) to the LEFT?
  // Shift left by at least spacer_width + 1 (min_s = -10, max_s =
  // -(spacer_width + 1)). Available space is spacer_width. Should fail.
  DenseIntSet fail_set2 = DenseIntSet::Top(-(spacer_width + 1) - (-10) + 1);
  DenseIntSet shift_fail2 = Optimization::ResolveBeamShiftUpward(
      library, network, 1, deltas, -10, fail_set2);
  CHECK(shift_fail2.Empty()) << "[2] Should fail due to collision!";

  // Shift left by at most 2 (min_s = -10, max_s = -2).
  // Assuming spacer_width >= 2, it should succeed.
  CHECK(spacer_width >= 2);
  DenseIntSet succ_set2 = DenseIntSet::Top(-2 - (-10) + 1);
  DenseIntSet shift_succ2 = Optimization::ResolveBeamShiftUpward(
      library, network, 1, deltas, -10, succ_set2);
  CHECK(!shift_succ2.Empty());
}

static void TestResolveDisplacementUpward(const CellLibrary &library) {
  Print("{}\n", __func__);
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
  Print("{}\n", __func__);
  CellLibrary library;

  // NOT gate passes displacement directly to its input.
  Cell not_gate(Gate::NOT);
  std::vector<Layer> network;
  network.push_back({not_gate});

  std::vector<int> deltas = {5};
  bool success = Optimization::ResolveDisplacementUpward(
      library, network, 0, deltas);
  CHECK(success) << "Should allow input delta to propagate without failing!";

  // The NOT gate should now be shifted by 5, meaning there's a spacer
  // before it.
  CHECK(network.size() == 1);
  CHECK(network[0].size() == 2);
  CHECK(network[0][0].gate == Gate::SPACER);
  CHECK(network[0][0].v == 5);
  CHECK(network[0][1].gate == Gate::NOT);
}

static void TestResolveDisplacementFailsOverlap() {
  Print("{}\n", __func__);
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

static void TestResolveDisplacementDownward(const CellLibrary &library) {
  Print("{}\n", __func__);
  Cell wire4 = CellLibrary::Wire(4, Bias::RIGHT);
  std::vector<Layer> network;
  network.push_back({wire4});
  network.push_back({wire4});

  std::vector<int> deltas = {1};
  bool success = Optimization::ResolveDisplacementDownward(
      library, network, 0, deltas);
  CHECK(success) << "Failed to resolve displacement!";

  // Check that the top wire changed to WIRE(3) to absorb the +1 delta on its
  // input.
  CHECK(network.size() == 2);

  int w0_idx = network[0][0].gate == Gate::SPACER ? 1 : 0;
  CHECK(network[0].size() == w0_idx + 1);
  CHECK(IsWire(network[0][w0_idx].gate));
  CHECK(network[0][w0_idx].v == 3);
  CHECK(!network[0][w0_idx].flip);

  // The bottom layer should remain WIRE(4).
  int w1_idx = network[1][0].gate == Gate::SPACER ? 1 : 0;
  CHECK(network[1].size() == w1_idx + 1);
  CHECK(network[1][w1_idx].v == 4);
}

static void DownwardMovesOutputs(const CellLibrary &library) {
  Print("{}\n", __func__);
  // NOT gate passes displacement directly to its output.
  Cell not_gate(Gate::NOT);
  std::vector<Layer> network;
  network.push_back({not_gate});

  std::vector<int> deltas = {5};
  bool success = Optimization::ResolveDisplacementDownward(
      library, network, 0, deltas);
  CHECK(success) << "Should allow output delta to propagate without failing!";

  // The NOT gate should now be shifted by 5, meaning there's a spacer
  // before it.
  CHECK(network.size() == 1);
  CHECK(network[0].size() == 2);
  CHECK(network[0][0].gate == Gate::SPACER);
  CHECK(network[0][0].v == 5);
  CHECK(network[0][1].gate == Gate::NOT);
}

static void DownwardFailsOverlap(
    const CellLibrary &library) {
  Print("{}\n", __func__);
  Cell wire4 = CellLibrary::Wire(4, Bias::RIGHT);
  std::vector<Layer> network;
  network.push_back({wire4, wire4});

  // Shift the first wire's input heavily to the right.
  // It will collide with the second wire.
  std::vector<int> deltas = {32};
  bool success = Optimization::ResolveDisplacementDownward(
      library, network, 0, deltas);
  CHECK(!success) << "Should fail due to overlap!";
}

static void DownwardCornerCases(const CellLibrary &library) {
  Print("{}\n", __func__);
  {
    // Test that shifting a wire input so much that it crosses its output
    // causes the wire to flip.
    Cell wire = CellLibrary::Wire(4, Bias::RIGHT);
    std::vector<Layer> network;
    network.push_back({wire});

    auto info = library.GetInfo(wire);
    int original_disp = info.outputs[0].xblock - info.inputs[0].xblock;
    int shift = original_disp + 6;
    std::vector<int> deltas = {shift};

    bool success = Optimization::ResolveDisplacementDownward(
        library, network, 0, deltas);
    CHECK(success) << "Failed to resolve downward displacement with flip!";

    CHECK(network.size() == 1);
    int wire_idx = (network[0][0].gate == Gate::SPACER) ? 1 : 0;
    CHECK(network[0].size() == wire_idx + 1);
    Cell top_wire = network[0][wire_idx];
    CHECK(IsWire(top_wire.gate));
    CHECK(top_wire.v == 6);
    CHECK(top_wire.flip == true);
  }

  {
    // Test that multi-input gates can only be shifted rigidly.
    Cell gate(Gate::AND0110);
    std::vector<Layer> network;
    network.push_back({gate});

    auto info = library.GetInfo(gate);
    std::vector<int> deltas(info.inputs.size(), 4);
    if (deltas.size() > 1) {
      deltas[0] = 5; // Make the shift non-rigid
      bool success = Optimization::ResolveDisplacementDownward(
          library, network, 0, deltas);
      CHECK(!success) << "Should fail to non-rigidly shift a multi-input gate!";
    }
  }

  {
    // Test shifting the first cell in a multi-cell layer without overlapping.
    Cell gate(Gate::NOT);
    auto info = library.GetInfo(gate);

    std::vector<Layer> network;
    network.push_back({gate, CellLibrary::Spacer(100), gate});

    std::vector<int> deltas(info.inputs.size(), 5);
    bool success = Optimization::ResolveDisplacementDownward(
        library, network, 0, deltas);
    CHECK(success) << "Failed to shift one cell in a multi-cell layer!";

    CHECK(network.size() == 1);
    CHECK(network[0].size() >= 2);
    CHECK(network[0][0].gate == Gate::SPACER);
    CHECK(network[0][0].v == 5);
    CHECK(network[0][1].gate == Gate::NOT);
  }
}

static void TestOptimizeWindowCornerCases(const CellLibrary &library) {
  Print("{}\n", __func__);
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
    Print("  Case 1.\n");
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
    Print("  Case 2.\n");
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
    Print("  Case 3.\n");
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
  Print("{}\n", __func__);
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

static void TestTightUpward(const CellLibrary &library) {
  Print("{}\n", __func__);
  // Simulate a tightly packed gate that can move up.
  Cell wire = CellLibrary::Wire(0, Bias::RIGHT);
  Cell gate(Gate::NOT);
  Cell sink(Gate::SINK);

  auto info_w = library.GetInfo(wire);
  auto info_g = library.GetInfo(gate);
  auto info_s = library.GetInfo(sink);

  int x_w = 0;
  int x_g = x_w + info_w.outputs[0].xblock - info_g.inputs[0].xblock;
  int x_s = x_g + info_g.outputs[0].xblock - info_s.inputs[0].xblock;

  int min_x = std::min({x_w, x_g, x_s});
  x_w -= min_x;
  x_g -= min_x;
  x_s -= min_x;

  auto MakeLayer = [](int x, Cell cell) {
    std::vector<Cell> layer;
    if (x > 0) {
      layer.push_back(CellLibrary::Spacer(x));
    }
    layer.push_back(cell);
    return layer;
  };

  Layout l;
  l.input_vars = {{0, CType::MIXED}};
  l.circuit.layers.push_back(MakeLayer(x_w, wire));
  l.circuit.layers.push_back(MakeLayer(x_g, gate));
  l.circuit.layers.push_back(MakeLayer(x_s, sink));

  DRC::CheckLayout(library, "tight_upward_before", l);
  Layout opt = Optimization::Optimize(library, l);
  CHECK(opt.circuit.layers.size() == 2) << "Failed to move tight gate up!";
}

static void TestExactSpaceUpward(const CellLibrary &library) {
  Print("{}\n", __func__);
  Cell gate(Gate::NOT);
  Cell const0(Gate::CONST0);
  Cell sink(Gate::SINK);

  auto gate_info = library.GetInfo(gate);
  auto const0_info = library.GetInfo(const0);
  auto sink_info = library.GetInfo(sink);

  int x_left_obs = 0;
  int x_new_gate = x_left_obs + const0_info.block_width;
  int x_right_obs = x_new_gate + gate_info.block_width;

  int x_left_sink = x_left_obs +
    const0_info.outputs[0].xblock - sink_info.inputs[0].xblock;
  int x_right_sink = x_right_obs +
    const0_info.outputs[0].xblock - sink_info.inputs[0].xblock;

  int best_offset = -1;
  Cell wire(Gate::SPACER);
  int x_wire = 0;
  int x_gate = 0;
  int x_mid_src = 0;

  // Search for a wire size that produces a valid non-overlapping
  // layout initially.
  for (int w : CellLibrary::WIRE_SIZES) {
    Cell w_cell = CellLibrary::Wire(w, CellLibrary::Bias::RIGHT);
    auto wi = library.GetInfo(w_cell);
    int xw = x_new_gate + gate_info.inputs[0].xblock - wi.inputs[0].xblock;

    // Check layer 1 overlaps
    if (xw < x_left_obs + const0_info.block_width) continue;
    if (xw + wi.block_width > x_right_obs) continue;

    // Check layer 2 overlaps
    int xg = xw + wi.outputs[0].xblock - gate_info.inputs[0].xblock;
    if (xg < x_left_sink + sink_info.block_width) continue;
    if (xg + gate_info.block_width > x_right_sink) continue;

    best_offset = w;
    wire = w_cell;
    x_wire = xw;
    x_gate = xg;
    x_mid_src = xw + wi.inputs[0].xblock - const0_info.outputs[0].xblock;
    break;
  }

  CHECK(best_offset != -1)
      << "Could not find a wire that fits the test geometry constraints.";

  int x_gate_sink = x_gate +
    gate_info.outputs[0].xblock - sink_info.inputs[0].xblock;

  int shift = 0;
  if (x_mid_src < shift) shift = -x_mid_src;
  if (x_left_sink < shift) shift = -x_left_sink;
  if (x_gate < shift) shift = -x_gate;
  if (x_gate_sink < shift) shift = -x_gate_sink;

  x_left_obs += shift;
  x_new_gate += shift;
  x_right_obs += shift;
  x_left_sink += shift;
  x_right_sink += shift;
  x_wire += shift;
  x_gate += shift;
  x_mid_src += shift;
  x_gate_sink += shift;

  auto MakeLayer = [&](std::vector<std::pair<int, Cell>> items) {
      std::sort(items.begin(), items.end(),
                [](const auto &a, const auto &b) {
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

  Layout l;
  l.input_vars = {};

  l.circuit.layers.push_back(MakeLayer({
        {x_mid_src, const0}
      }));

  l.circuit.layers.push_back(MakeLayer({
        {x_left_obs, const0},
        {x_wire, wire},
        {x_right_obs, const0}
      }));

  l.circuit.layers.push_back(MakeLayer({
        {x_left_sink, sink},
        {x_gate, gate},
        {x_right_sink, sink}
      }));

  l.circuit.layers.push_back(MakeLayer({
        {x_gate_sink, sink}
      }));

  DRC::CheckLayout(library, "exact_upward_before", l);

  Layout opt = Optimization::Optimize(library, l);

  DRC::CheckLayout(library, "exact_upward_after", opt);

  CHECK(opt.circuit.layers.size() < 4) << "Failed to move exact-fit gate up!";
}

struct TestCase {
  std::string_view name;
  std::string_view comment;
  std::string_view layout;
};

static std::initializer_list<TestCase> TEST_CASES = {
  {
    .name = "move-up1",
    .comment = "the dup can move up",
    .layout = R"(
0I 1M
0 wf 42 s0
0 wf 16 Wc42 14 we8
4 we1 12 wc9 55 we
5 we 20 wc 5 d1
5 we 20 wc 32 we10 4 we64
)",
  },
  {
    .name = "move-upquad",
    .comment = "the three bottom not gates can move up",
    .layout = R"(
0I 1O 2O 3I 4I 5O
21 we 13 wd 0 aa
21 we 17 wc3 16 s0
21 we 16 Wc 24 Wc9 0 We14
21 we 16 Wc 20 Wc 5 We3
21 we 16 wd 0 n0 6 we
21 we 20 wc 12 We8 6 we
0 n1 0 n0 16 we 10 N1
21 wc 10 We10 16 we 10 wc
21 aa
)",
  },

};

static void TryTestCases(const CellLibrary &library) {
  Print("{}\n", __func__);
  std::vector<TestCase> cases(TEST_CASES.begin(), TEST_CASES.end());

  std::mutex m;
  int done = 0;
  int successes = 0;
  int64_t before_layers = 0;
  int64_t before_cells = 0;
  int64_t after_layers = 0;
  int64_t after_cells = 0;

  StatusBar status(1);
  Timer run_timer;

  UnParallelApp(
      cases,
      [&](const TestCase &tc) {
        std::optional<Layout> opt_layout = LayoutEngine::Parse(tc.layout);
        if (!opt_layout.has_value()) {
          LOG(FATAL) << "Failed to parse layout for " << tc.name;
        }
        Layout layout = std::move(opt_layout.value());

        // Make sure it's valid before we start!
        DRC::CheckLayout(library, std::string(tc.name) + "_before", layout);

        size_t b_layers = layout.circuit.layers.size();
        size_t b_cells = CircuitSize(layout.circuit);

        Layout optimized = Optimization::Optimize(library, layout);

        DRC::CheckLayout(library, std::string(tc.name) + "_after", optimized);
        DRC::AssertEquivalentLayout(library, std::string(tc.name),
                                    layout, optimized);

        size_t a_layers = optimized.circuit.layers.size();
        size_t a_cells = CircuitSize(optimized.circuit);

        bool success = (a_layers < b_layers);

        {
          MutexLock ml(&m);
          before_layers += b_layers;
          before_cells += b_cells;
          after_layers += a_layers;
          after_cells += a_cells;
          if (success) {
            successes++;
          }
          done++;
          status.Progress(done, cases.size(), "Benchmarking");
        }
      },
      8);

  const double took = run_timer.Seconds();

  status.Clear();
  status.Remove();

  Print("Benchmark results:\n");
  Print("  Time taken: {}\n", ANSI::Time(took));
  Print("  Successes:  {} / {}\n", successes, cases.size());
  Print("  Layers:     {} -> {}\n", before_layers, after_layers);
  Print("  Cells:      {} -> {}\n", before_cells, after_cells);
}

int main(int argc, char **argv) {
  ANSI::Init();

  CellLibrary library;

  TestResolveDisplacementUpward(library);
  TestResolveDisplacementMovesInputs();
  TestResolveDisplacementFailsOverlap();
  TestResolveDisplacementDownward(library);
  DownwardMovesOutputs(library);
  DownwardFailsOverlap(library);
  DownwardCornerCases(library);
  Print("Resolve displacement OK.\n");

  TestResolveBeamShiftUpward(library);
  Print("Resolve beam shift OK.\n");

  TestOptimizeWindowCornerCases(library);

  TestRemoveAllWireLayer(library);
  TestStraightenZigZagWire(library);
  TestStraightenParallelWires(library);
  TestTightUpward(library);
  TestExactSpaceUpward(library);

  TestWindowed(library);

  TryTestCases(library);

  OptimizeInteresting(library);

  OptimizeModest(library);

  Print("OK\n");
  return 0;
}
