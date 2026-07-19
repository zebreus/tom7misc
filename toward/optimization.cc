
#include "optimization.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "base/print.h"
#include "cell-library.h"
#include "circuit.h"
#include "layout.h"
#include "vector-util.h"

static constexpr int VERBOSE = 0;

struct Optimizer {
  // Need to be able to access the dimensions of cells so that
  // we know how they can be moved around.
  const CellLibrary &library;

  // A normalized cell. We remove all spacers, instead recording the
  // non-negative space before a cell.
  struct NC {
    int left_space = 0;
    Cell cell;
  };

  // We mostly work on the circuit, so expand the Layout object into
  // its fields.
  std::vector<std::pair<int, CType>> input_vars;
  std::vector<std::vector<NC>> layers;

  // We increment this whenever we make definite progress. This
  // implies that we have some well-founded order on circuits in mind.
  // This is a lexicographic ordering along the lines of (number of
  // layers, number of layers with only wires, global straightness of
  // wires). (Best if we can say precisely what it is!)
  int improve_count = 0;

  static std::vector<NC> NormalizeLayer(const std::vector<Cell> &layer) {
    std::vector<NC> ret;
    int space = 0;
    for (const Cell &cell : layer) {
      if (cell.gate == Gate::SPACER) {
        space += cell.v;
      } else {
        ret.emplace_back(NC{
            .left_space = space,
            .cell = cell,
          });
        space = 0;
      }
    }
    // This deliberately ignores any trailing space.
    return ret;
  }

  Optimizer(const CellLibrary &library,
            Layout original) : library(library),
                               input_vars(std::move(original.input_vars)),
                               layers(VectorMap(original.circuit.layers,
                                                NormalizeLayer)) {
  }

  // The windowed layer pass works on three contiguous layers. It
  // tries to produce an equivalent circuit (i.e. that has the same
  // I/O behavior, including the positions of the ports) that is
  // simpler. This has two goals, which together help us reduce
  // the circuit:
  //   * Make wires straighter. If an input port is connected to
  //     an output port directly (only wire cells) then we count
  //     the total absolute displacement of each of the three
  //     wires, and consider smaller to be better. We should
  //     increment improve_count when we've made definite progress.
  //   * Move non-wire elements so that they are on the same
  //     layer as each other. We make definite progress (and can
  //     increment improve_count) when we remove the last non-wire
  //     element from a layer.
  CType GetWireType(Gate g) {
    if (g == Gate::WIRE0A || g == Gate::WIRE0B) return CType::ZERO;
    if (g == Gate::WIRE1A || g == Gate::WIRE1B) return CType::ONE;
    return CType::MIXED;
  }

  std::vector<Cell> AllWires(CType type) {
    std::vector<Cell> ret;
    for (int v : CellLibrary::WIRE_SIZES) {
      for (bool flip : {false, true}) {
        Cell a = CellLibrary::Wire(v, CellLibrary::Bias::RIGHT, type);
        a.flip = flip;
        ret.push_back(a);
        if (v < CellLibrary::SMALL_WIRE) {
          Cell b = CellLibrary::Wire(v, CellLibrary::Bias::LEFT, type);
          b.flip = flip;
          ret.push_back(b);
        }
      }
    }
    return ret;
  }

  void WindowedLayerPass() {
    bool changed = false;
    for (size_t i = 0; i + 2 < layers.size(); ++i) {
      if (OptimizeWindow(i)) {
        changed = true;
      }
    }

    // Termination metric: we make definite progress when we strictly
    // decrease the absolute displacements of wires in the layout.
    if (changed) {
      improve_count++;
    }
  }

  bool OptimizeWindow(size_t start) {
    if (layers[start].size() != layers[start+1].size() ||
        layers[start+1].size() != layers[start+2].size()) {
      // For now, only optimize straightforward 1-to-1 strands.
      return false;
    }

    const int mcc = library.MinClearanceClose();

    size_t n = layers[start].size();
    std::vector<int> x0(n), x1(n), x2(n);
    auto compute_x = [&](int l, std::vector<int> &x_out) {
      int x = 0;
      for (size_t i = 0; i < n; ++i) {
        x += layers[l][i].left_space;
        x_out[i] = x;
        x += library.GetInfo(layers[l][i].cell).block_width;
      }
    };
    compute_x(start, x0);
    compute_x(start+1, x1);
    compute_x(start+2, x2);

    std::vector<std::vector<int>> target_x = {x0, x1, x2};
    bool improved = false;
    int min_x0 = 0, min_x1 = 0, min_x2 = 0;

    int out_prefix0 = 0, in_prefix1 = 0;
    int out_prefix1 = 0, in_prefix2 = 0;

    for (size_t c = 0; c < n; ++c) {
      NC nc0 = layers[start][c];
      NC nc1 = layers[start+1][c];
      NC nc2 = layers[start+2][c];

      auto info0 = library.GetInfo(nc0.cell);
      auto info1 = library.GetInfo(nc1.cell);
      auto info2 = library.GetInfo(nc2.cell);

      bool connected =
        (out_prefix0 == in_prefix1) && (out_prefix1 == in_prefix2) &&
        (info0.outputs.size() == 1) && (info1.inputs.size() == 1) &&
        (info1.outputs.size() == 1) && (info2.inputs.size() == 1);

      out_prefix0 += info0.outputs.size();
      in_prefix1 += info1.inputs.size();
      out_prefix1 += info1.outputs.size();
      in_prefix2 += info2.inputs.size();

      // Skip if the topology is complex (not a simple strand)
      if (!connected ||
          info0.inputs.size() > 1 || info0.outputs.size() > 1 ||
          info1.inputs.size() > 1 || info1.outputs.size() > 1 ||
          info2.inputs.size() > 1 || info2.outputs.size() > 1) {
        min_x0 = target_x[0][c] + info0.block_width + mcc;
        min_x1 = target_x[1][c] + info1.block_width + mcc;
        min_x2 = target_x[2][c] + info2.block_width + mcc;
        continue;
      }

      int current_cost = 0;
      if (IsWire(nc0.cell.gate)) current_cost += std::abs(nc0.cell.v);
      if (IsWire(nc1.cell.gate)) current_cost += std::abs(nc1.cell.v);
      if (IsWire(nc2.cell.gate)) current_cost += std::abs(nc2.cell.v);

      struct State {
        int cost = 0, x = 0;
        Cell cell{Gate::SPACER};
        int prev_out = 0;
      };

      // DP Layer 0
      std::unordered_map<int, State> dp0;
      std::vector<Cell> c0_cands = IsWire(nc0.cell.gate) ?
          AllWires(GetWireType(nc0.cell.gate)) : std::vector<Cell>{nc0.cell};

      for (const Cell &cell : c0_cands) {
        auto info = library.GetInfo(cell);
        if (info.inputs.size() == 1) {
          int fixed_in = x0[c] + info0.inputs[0].xblock;
          int x = fixed_in - info.inputs[0].xblock;
          if (x < min_x0) continue;
          if (c + 1 < n && x + info.block_width + mcc > target_x[0][c+1])
            continue;
          int out = info.outputs.empty() ? x : x + info.outputs[0].xblock;
          int cost = IsWire(cell.gate) ? std::abs(cell.v) : 0;
          if (!dp0.count(out) || cost < dp0[out].cost) {
            dp0[out] = {cost, x, cell, 0};
          }
        } else {
          for (int x = std::max(min_x0, x0[c] - 150); x <= x0[c] + 150; ++x) {
            if (c + 1 < n && x + info.block_width + mcc > target_x[0][c+1])
              continue;
            int out = info.outputs.empty() ? x : x + info.outputs[0].xblock;
            int cost = IsWire(cell.gate) ? std::abs(cell.v) : 0;
            if (!dp0.count(out) || cost < dp0[out].cost) {
              dp0[out] = {cost, x, cell, 0};
            }
          }
        }
      }

      // DP Layer 1
      std::unordered_map<int, State> dp1;
      std::vector<Cell> c1_cands = IsWire(nc1.cell.gate) ?
          AllWires(GetWireType(nc1.cell.gate)) : std::vector<Cell>{nc1.cell};

      for (auto &[prev_out, prev_state] : dp0) {
        for (const Cell &cell : c1_cands) {
          auto info = library.GetInfo(cell);
          if (info.inputs.size() == 1) {
            int x = prev_out - info.inputs[0].xblock;
            if (x < min_x1) continue;
            if (c + 1 < n && x + info.block_width + mcc > target_x[1][c+1])
              continue;

            int out = info.outputs.empty() ? x : x + info.outputs[0].xblock;
            int cost = prev_state.cost +
              (IsWire(cell.gate) ? std::abs(cell.v) : 0);
            if (!dp1.count(out) || cost < dp1[out].cost) {
              dp1[out] = {cost, x, cell, prev_out};
            }
          }
        }
      }

      // DP Layer 2
      std::unordered_map<int, State> dp2;
      std::vector<Cell> c2_cands = IsWire(nc2.cell.gate) ?
          AllWires(GetWireType(nc2.cell.gate)) : std::vector<Cell>{nc2.cell};

      for (auto &[prev_out, prev_state] : dp1) {
        for (const Cell &cell : c2_cands) {
          auto info = library.GetInfo(cell);
          if (info.inputs.size() == 1) {
            int x = prev_out - info.inputs[0].xblock;
            if (x < min_x2) continue;
            if (c + 1 < n && x + info.block_width + mcc > target_x[2][c+1])
              continue;
            if (info.outputs.size() == 1) {
              int out = x + info.outputs[0].xblock;
              int fixed_out = x2[c] + info2.outputs[0].xblock;
              if (out != fixed_out) continue;
            }
            int out = info.outputs.empty() ? x : x + info.outputs[0].xblock;
            int cost = prev_state.cost +
              (IsWire(cell.gate) ? std::abs(cell.v) : 0);
            if (!dp2.count(out) || cost < dp2[out].cost) {
              dp2[out] = {cost, x, cell, prev_out};
            }
          }
        }
      }

      // Find optimal assignments
      int best_cost = 1e9, best_out = 0;
      for (auto &[out, state] : dp2) {
        if (state.cost < best_cost) {
          best_cost = state.cost;
          best_out = out;
        }
      }

      if (best_cost < current_cost) {
        improved = true;
        State s2 = dp2[best_out];
        State s1 = dp1[s2.prev_out];
        State s0 = dp0[s1.prev_out];

        layers[start][c].cell = s0.cell;
        layers[start+1][c].cell = s1.cell;
        layers[start+2][c].cell = s2.cell;

        target_x[0][c] = s0.x;
        target_x[1][c] = s1.x;
        target_x[2][c] = s2.x;
      }

      min_x0 = target_x[0][c] +
        library.GetInfo(layers[start][c].cell).block_width + mcc;
      min_x1 = target_x[1][c] +
        library.GetInfo(layers[start+1][c].cell).block_width + mcc;
      min_x2 = target_x[2][c] +
        library.GetInfo(layers[start+2][c].cell).block_width + mcc;
    }

    if (improved) {
      auto rebuild = [&](int l, const std::vector<int> &tx) {
        int current_x = 0;
        for (size_t c = 0; c < n; ++c) {
          layers[l][c].left_space = tx[c] - current_x;
          current_x = tx[c] + library.GetInfo(layers[l][c].cell).block_width;
        }
      };
      rebuild(start, target_x[0]);
      rebuild(start+1, target_x[1]);
      rebuild(start+2, target_x[2]);
    }

    return improved;
  }

  // Other passes may attempt to straighten out wires so that we can
  // remove layers. In RemovePassthroughLayers we remove only layers
  // that have wires (and spacers) exclusively. Therefore the goal is
  // to put displacement-0 wires on wire-only layers. All that matters
  // about a connected (vertical) series of wires is their net
  // displacement, so we can shift displacement into adjacent layers
  // to accomplish this. The wire cells have to fit, but we can choose
  // variants (wire A/B) and flip at will. It's also acceptable to
  // move non-wire cells, but we shouldn't change anything else about
  // those.
  bool IsPassthrough(const std::vector<NC> &layer) {
    for (const NC &nc : layer) {
      if (!IsWire(nc.cell.gate) || nc.cell.v != 0) {
        return false;
      }
    }
    return true;
  }

  void RemovePassthroughLayers() {
    std::vector<std::vector<NC>> trimmed;

    for (std::vector<NC> &layer : layers) {
      if (IsPassthrough(layer)) {
        improve_count++;
      } else {
        trimmed.emplace_back(std::move(layer));
      }
    }

    layers = std::move(trimmed);
  }

  void Run() {
    int run_iter = 0;
    do {
      run_iter++;
      if (VERBOSE > 0) Print("Run pass {}\n", run_iter);
      if (run_iter > 100) {
        if (VERBOSE > 0) Print("Bailing out of Run loop!\n");
        break;
      }
      // Incremented whenever we make definite progress.
      improve_count = 0;

      WindowedLayerPass();
      RemovePassthroughLayers();

    } while (improve_count > 0);


    CompactHorizontal();
  }

  void CompactHorizontal() {
    // TODO: Increase the horizontal density of layers, removing
    // spacers.
  }

  Layout Get() {
    std::vector<Layer> ret_layers;
    ret_layers.reserve(layers.size());
    for (const std::vector<NC> &nlayer : layers) {
      std::vector<Cell> layer;
      layer.reserve(nlayer.size() * 2);
      for (const NC &nc : nlayer) {
        if (nc.left_space > 0) {
          layer.push_back(Cell(Gate::SPACER, nc.left_space));
        }
        layer.push_back(nc.cell);
      }
      ret_layers.push_back(std::move(layer));
    }

    return Layout{
      .input_vars = std::move(input_vars),
      .circuit = Circuit{.layers = std::move(ret_layers)},
    };
  }
};


Layout Optimization::Optimize(const CellLibrary &library, const Layout &layout) {
  Optimizer optimizer(library, layout);
  optimizer.Run();
  return optimizer.Get();
}


