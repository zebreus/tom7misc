
#include <functional>
#include <algorithm>

#include "drc.h"
#include "util.h"
#include "base/logging.h"
#include "base/print.h"
#include "ansi.h"
#include "arcfour.h"
#include "layout.h"
#include "randutil.h"
#include "cell-library.h"
#include "circuit.h"
#include "optimization.h"
#include "periodically.h"
#include "status-bar.h"

static bool IsAllWires(const Layer &layer) {
  for (const Cell &cell : layer) {
    if (!IsWire(cell.gate) && cell.gate != SPACER) {
      return false;
    }
  }
  return true;
}

static int ConsecutiveWireLayers(const Layout &layout) {
  int max_consec = 0;
  int lower_bound = 0;
  for (int i = 0; i < layout.circuit.layers.size(); i++) {
    if (IsAllWires(layout.circuit.layers[i])) {
      max_consec = std::max(max_consec, (i + 1) - lower_bound);
    } else {
      lower_bound = i;
    }
  }

  return max_consec;
}

struct Reducer {
  const CellLibrary &library;
  ArcFour rc;

  bool CellFits(const Layout &layout, int layer_idx, int cell_idx,
                const Cell &new_cell) const {
    const Cell &old_cell = layout.circuit.layers[layer_idx][cell_idx];
    return library.GetInfo(new_cell).block_width ==
           library.GetInfo(old_cell).block_width;
  }

  int GetOutputStart(const Layer &layer, int cell_idx) const {
    int cur_out = 0;
    for (int i = 0; i < cell_idx; i++) {
      cur_out += GateArity(layer[i].gate).second;
    }
    return cur_out;
  }

  int GetInputStart(const Layer &layer, int cell_idx) const {
    int cur_in = 0;
    for (int i = 0; i < cell_idx; i++) {
      cur_in += GateArity(layer[i].gate).first;
    }
    return cur_in;
  }

  int CellForInput(const Layer &layer, int target_in) const {
    int cur_in = 0;
    for (int i = 0; i < layer.size(); i++) {
      int ins = GateArity(layer[i].gate).first;
      if (target_in >= cur_in && target_in < cur_in + ins) return i;
      cur_in += ins;
    }
    return -1;
  }

  int CellForOutput(const Layer &layer, int target_out) const {
    int cur_out = 0;
    for (int i = 0; i < layer.size(); i++) {
      int outs = GateArity(layer[i].gate).second;
      if (target_out >= cur_out && target_out < cur_out + outs) return i;
      cur_out += outs;
    }
    return -1;
  }

  // Reduce the size/complexity of the layout randomly by one step. This
  // must keep the layout valid, but it doesn't preserve the semantics
  // (including the I/O size).
  std::optional<Layout> OneReduction(const Layout &layout) {
    std::vector<std::function<Layout()>> moves;

    // - Remove the topmost or bottommost layer.
    if (layout.circuit.layers.size() > 1) {
      moves.push_back([this, &layout]() {
          Print("Move: Remove topmost layer\n");
          Layout res = layout;
          res.circuit.layers.erase(res.circuit.layers.begin());
          size_t new_arity = LayerArity(res.circuit.layers.front()).first;
          if (new_arity < res.input_vars.size()) {
            res.input_vars.resize(new_arity);
          } else {
            res.input_vars.resize(new_arity, {0, CType::MIXED});
          }
          return res;
        });
      moves.push_back([this, &layout]() {
          Print("Move: Remove bottommost layer\n");
          Layout res = layout;
          res.circuit.layers.pop_back();
          return res;
        });
    }

    Print("A: {} moves so far.\n", moves.size());

    // - Delete an input wire, replacing it with CONST0 or CONST1.
    if (!layout.circuit.layers.empty()) {
      const Layer &L0 = layout.circuit.layers[0];
      for (int i = 0; i < L0.size(); i++) {
        if (IsWire(L0[i].gate)) {
          Cell c0(Gate::CONST0);
          Cell c1(Gate::CONST1);
          if (CellFits(layout, 0, i, c0)) {
            moves.push_back([this, &layout, i, c0]() {
              Print("Move: Replace input wire {} with CONST0\n", i);
              Layout res = layout;
              int in_idx = GetInputStart(res.circuit.layers[0], i);
              res.circuit.layers[0][i] = c0;
              if (in_idx < res.input_vars.size()) {
                res.input_vars.erase(res.input_vars.begin() + in_idx);
              }
              return res;
            });
          }
          if (CellFits(layout, 0, i, c1)) {
            moves.push_back([this, &layout, i, c1]() {
              Print("Move: Replace input wire {} with CONST1\n", i);
              Layout res = layout;
              int in_idx = GetInputStart(res.circuit.layers[0], i);
              res.circuit.layers[0][i] = c1;
              if (in_idx < res.input_vars.size()) {
                res.input_vars.erase(res.input_vars.begin() + in_idx);
              }
              return res;
            });
          }
        }
      }
    }

    Print("B: {} moves so far.\n", moves.size());

    // - Move a constant output with a wire below it down some number of layers.
    for (int l = 0; l + 1 < layout.circuit.layers.size(); l++) {
      const Layer &L = layout.circuit.layers[l];
      const Layer &Lnext = layout.circuit.layers[l + 1];
      for (int i = 0; i < L.size(); i++) {
        if (L[i].gate == Gate::CONST0 || L[i].gate == Gate::CONST1) {
          int out_idx = GetOutputStart(L, i);
          int next_i = CellForInput(Lnext, out_idx);
          if (next_i >= 0 && IsWire(Lnext[next_i].gate)) {
            if (CellFits(layout, l + 1, next_i, L[i])) {
              moves.push_back([this, &layout, l, i, next_i]() {
                Print("Move: Move CONST from layer {} cell {} down to layer {} cell {}\n", l, i, l + 1, next_i);
                Layout res = layout;
                Cell c = res.circuit.layers[l][i];
                int w = library.GetInfo(c).block_width;
                res.circuit.layers[l][i] = CellLibrary::Spacer(w);
                res.circuit.layers[l + 1][next_i] = c;
                return res;
              });
            }
          }
        }
      }
    }

    Print("C: {} moves so far.\n", moves.size());

    // - Delete an output wire, replacing it with SINK.
    if (!layout.circuit.layers.empty()) {
      int last_l = layout.circuit.layers.size() - 1;
      const Layer &L = layout.circuit.layers[last_l];
      for (int i = 0; i < L.size(); i++) {
        if (IsWire(L[i].gate)) {
          Cell sink(Gate::SINK);
          if (CellFits(layout, last_l, i, sink)) {
            moves.push_back([this, &layout, last_l, i, sink]() {
              Print("Move: Replace output wire {} with SINK\n", i);
              Layout res = layout;
              res.circuit.layers[last_l][i] = sink;
              return res;
            });
          }
        }
      }
    }

    Print("D: {} moves so far.\n", moves.size());

    // - Move a sink cell with a wire above it up some number of steps.
    for (int l = 1; l < layout.circuit.layers.size(); l++) {
      const Layer &L = layout.circuit.layers[l];
      const Layer &Lprev = layout.circuit.layers[l - 1];
      for (int i = 0; i < L.size(); i++) {
        if (L[i].gate == Gate::SINK) {
          int in_idx = GetInputStart(L, i);
          int prev_i = CellForOutput(Lprev, in_idx);
          if (prev_i >= 0 && IsWire(Lprev[prev_i].gate)) {
            if (CellFits(layout, l - 1, prev_i, L[i])) {
              moves.push_back([this, &layout, l, i, prev_i]() {
                Print("Move: Move SINK from layer {} cell {} up to layer {} cell {}\n", l, i, l - 1, prev_i);
                Layout res = layout;
                Cell c = res.circuit.layers[l][i];
                int w = library.GetInfo(c).block_width;
                res.circuit.layers[l][i] = CellLibrary::Spacer(w);
                res.circuit.layers[l - 1][prev_i] = c;
                return res;
              });
            }
          }
        }
      }
    }

    Print("End: {} moves so far.\n", moves.size());

    if (moves.empty()) {
      return std::nullopt;
    }

    const auto &f = moves[RandTo(&rc, moves.size())];
    return f();
  }

  Layout ReduceWhile(Layout layout, int max_consecutive_failures,
                     std::function<bool(const Layout &layout)> pred) {
    CHECK(pred(layout)) << "Predicate must be initially true!";

    Periodically status_per(1.0);
    StatusBar status(1);
    status.Status("Startup.");
    int attempts = 0;
    int failures = 0;
    int total_failures = 0;
    while (failures < max_consecutive_failures) {
      attempts++;
      if (std::optional<Layout> tmp = OneReduction(layout)) {
        if (pred(*tmp)) {
          failures = 0;
          layout = std::move(*tmp);
        } else {
          failures++;
          total_failures++;
        }
      } else {
        failures++;
        total_failures++;
      }

      status_per.RunIf([&]{
          status.Status("{} rounds. {} failures, {} consec.",
                        attempts, total_failures, failures);
        });

    }
    return layout;
  }

  Reducer(const CellLibrary &library) : library(library), rc("reducer") {}
};

bool Optimizable(const CellLibrary &library, const Layout &layout) {
  Print("Check if optimizable...\n");
  Layout optimized = Optimization::Optimize(library, layout);
  Print("Optimized returned.\n");

  if (optimized.circuit.layers.size() < layout.circuit.layers.size()) {
    return true;
  }

  size_t opt_cells = 0;
  for (const auto &layer : optimized.circuit.layers) {
    opt_cells += layer.size();
  }

  size_t orig_cells = 0;
  for (const auto &layer : layout.circuit.layers) {
    orig_cells += layer.size();
  }

  return opt_cells < orig_cells;
}

static Layout MakeStart(const CellLibrary &library) {
  World world{.symbol_names = {"a", "b", "c"}};
  std::unique_ptr<LayoutEngine> le = LayoutEngine::Create(library, world);
  Prop a{Var{.id = 0}}, b{Var{.id = 1}}, c{Var{.id = 2}};

  std::vector<Prop> output = {(a ^ (b & -c)) | (a & c)};
  Layout layout = le->DoLayout(output);
  Print("Got layout:\n{}\n", LayoutEngine::ToString(layout));
  Print("Try optimizing:\n");
  layout = Optimization::Optimize(library, layout);
  Print("Optimized!\n");
  DRC::CheckLayout(library, "start", layout);
  return layout;
}

static void ThreeLayers() {
  CellLibrary library;
  // auto olayout = LayoutEngine::Parse(Util::ReadFile("xorvars.layout"));
  //   CHECK(olayout.has_value());
  // Layout layout = std::move(olayout.value());
  Layout layout = MakeStart(library);

  // Does it have three layers of just wires in a row, but can't be optimized?
  std::function<bool(const Layout)> Pred = [&library](const Layout &layout) {
      return ConsecutiveWireLayers(layout) >= 3 &&
        !Optimizable(library, layout);
    };

  Reducer reducer(library);

  Layout result = reducer.ReduceWhile(layout, 100, Pred);

  Util::WriteFile("reduced.layout", LayoutEngine::Serialize(result));
  Print("Reduced:\n{}\n", LayoutEngine::ToString(result));
}



int main(int argc, char **argv) {
  ANSI::Init();

  ThreeLayers();

  return 0;
}
