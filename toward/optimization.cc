
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

// Concepts needed for layout optimization (from other files):
//
// Circuit Structure:
// - A `Circuit` is a sequence of `Layer`s (evaluated top to bottom).
// - Each `Layer` is a `std::vector<Cell>`.
// - A `Cell` contains a `Gate` (enum like SPACER, AND0110, WIREA, etc.), an
//   integer `v` (parameterizing width for spacers, displacement for wires),
//   and a boolean `flip` (flips geometry horizontally).
//
// Geometry (from cell-library.h):
// - `CellLibrary::GetInfo(cell)` returns `Info` for a cell, which includes:
//   - `block_width`: The total width the cell occupies.
//   - `inputs`, `outputs`: Vectors of `IO` objects, each with an `xblock`
//     (horizontal offset within the cell) and a `CType`.
// - Wires (e.g., Gate::WIREA, Gate::WIREB) route signals. The parameter `v`
//   is the wire's horizontal displacement. `CellLibrary::WIRE_SIZES` lists
//   valid sizes, and `CellLibrary::Wire()` gets wire cells.
//
// Utilities from circuit.h:
// - `IsWire(Gate)` tells if a gate is a wire.
// - `GateArity(Gate)` returns a pair of (num_inputs, num_outputs).

// Generally, try to write this using helper functions that have
// independently meaningful semantics. Otherwise, this can get hairy
// quickly! For example, a function that attempts to iteratively
// resolve a requested displacement as it traces through a set of
// adjacent wires would make sense; it should clearly state its
// non-obvious preconditions and the guarantees it makes. If it makes
// sense to transform into intermediate representations so that the
// code becomes less fiddly, that is also a good idea.


static constexpr int VERBOSE = 0;

static CType GetWireType(Gate g) {
  if (g == Gate::WIRE0A || g == Gate::WIRE0B) return CType::ZERO;
  if (g == Gate::WIRE1A || g == Gate::WIRE1B) return CType::ONE;
  return CType::MIXED;
}

static std::vector<Cell> AllWires(CType type) {
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
  bool MoveCellsUpPass() {
    bool changed = false;
    for (size_t i = 0; i + 1 < layers.size(); ++i) {
      if (MoveCellsUp(i)) {
        changed = true;
      }
    }

    // Termination metric: we make definite progress when we strictly
    // decrease the absolute displacements of wires in the layout.
    if (changed) {
      improve_count++;
      return true;
    }

    return false;
  }

  // MoveCellsUp attempts to move a non-wire cell from layer i+1 up into
  // layer i.
  bool MoveCellsUp(int i) {
    auto GetXCoords = [&](int layer_idx) {
      std::vector<int> x_coords(layers[layer_idx].size());
      int curr_x = 0;
      for (size_t k = 0; k < layers[layer_idx].size(); k++) {
        curr_x += layers[layer_idx][k].left_space;
        x_coords[k] = curr_x;
        curr_x += library.GetInfo(layers[layer_idx][k].cell).block_width;
      }
      return x_coords;
    };

    std::vector<int> l_i_x = GetXCoords(i);
    std::vector<int> l_i_plus_1_x = GetXCoords(i + 1);

    for (int idx = 0; idx < (int)layers[i + 1].size(); idx++) {
      const Cell &c = layers[i + 1][idx].cell;
      if (IsWire(c.gate)) continue;

      CellLibrary::Info info_c = library.GetInfo(c);
      int num_inputs = info_c.inputs.size();
      if (num_inputs == 0) continue;

      // Calculate the starting index of the input pins for cell c in the
      // global layer input chute.
      int in_chute_start = 0;
      for (int k = 0; k < idx; k++) {
        in_chute_start += library.GetInfo(layers[i + 1][k].cell).inputs.size();
      }

      // Find the range of cells in layer i that provide the inputs to cell c.
      // Their outputs must perfectly match the inputs of c.
      int layer_i_start_idx = -1;
      int layer_i_end_idx = -1;
      int current_out_chute = 0;

      for (size_t k = 0; k < layers[i].size(); k++) {
        int cell_outs = library.GetInfo(layers[i][k].cell).outputs.size();
        if (current_out_chute <= in_chute_start &&
            current_out_chute + cell_outs > in_chute_start) {
          if (layer_i_start_idx == -1) layer_i_start_idx = k;
        }
        current_out_chute += cell_outs;
        if (current_out_chute >= in_chute_start + num_inputs) {
          if (layer_i_end_idx == -1) layer_i_end_idx = k;
          break;
        }
      }

      if (layer_i_start_idx == -1 || layer_i_end_idx == -1) continue;

      // We can only move c up into layer i if all the cells currently
      // providing its inputs are just wires, which can be pushed down to i+1.
      bool all_wires = true;
      for (int k = layer_i_start_idx; k <= layer_i_end_idx; k++) {
        if (!IsWire(layers[i][k].cell.gate)) {
          all_wires = false;
          break;
        }
      }
      if (!all_wires) continue;

      int c_x = l_i_plus_1_x[idx];

      // We need to replace c in layer i+1 with straight wires that route
      // its outputs. Check if these new wires fit in the available horizontal space.
      std::vector<NC> new_wires;
      bool fit_wires = true;
      int curr_left_limit = 0;
      if (idx > 0) {
        CellLibrary::Info prev_info =
            library.GetInfo(layers[i + 1][idx - 1].cell);
        curr_left_limit = l_i_plus_1_x[idx - 1] + prev_info.block_width;
      }

      for (size_t j = 0; j < info_c.outputs.size(); j++) {
        Cell w = CellLibrary::Wire(0, CellLibrary::Bias::RIGHT,
                                   info_c.outputs[j].type);
        CellLibrary::Info info_w = library.GetInfo(w);
        int w_x = c_x + info_c.outputs[j].xblock - info_w.inputs[0].xblock;
        if (w_x < curr_left_limit) {
          fit_wires = false;
          break;
        }

        NC nc_w{
          .left_space = w_x - curr_left_limit,
          .cell = w,
        };
        new_wires.push_back(nc_w);

        curr_left_limit = w_x + info_w.block_width;
      }

      int right_limit_i_plus_1 = 1000000000;
      if (idx + 1 < (int)layers[i + 1].size()) {
        right_limit_i_plus_1 = l_i_plus_1_x[idx + 1];
      }
      if (curr_left_limit > right_limit_i_plus_1) fit_wires = false;
      if (!fit_wires) continue;

      // Check if cell c fits in layer i within the space freed up by
      // removing the wires.
      int c_end = c_x + info_c.block_width;
      int left_limit_i = 0;
      if (layer_i_start_idx > 0) {
        CellLibrary::Info prev_info =
            library.GetInfo(layers[i][layer_i_start_idx - 1].cell);
        left_limit_i = l_i_x[layer_i_start_idx - 1] + prev_info.block_width;
      }
      if (c_x < left_limit_i) continue;

      int right_limit_i = 1000000000;
      if (layer_i_end_idx + 1 < (int)layers[i].size()) {
        right_limit_i = l_i_x[layer_i_end_idx + 1];
      }
      if (c_end > right_limit_i) continue;

      // Calculate the horizontal displacement required for each input connection.
      // Cell c replaces some wires in layer i. It will be placed at c_x.
      // Its inputs will be at new_in_x, but the layer above (i-1) currently
      // outputs at old_in_x (where the wires received their inputs).
      // So layer i-1 must shift its outputs by (new_in_x - old_in_x) to match.
      std::vector<int> deltas;
      int input_k = 0;
      for (int k = layer_i_start_idx; k <= layer_i_end_idx; k++) {
        CellLibrary::Info w_info = library.GetInfo(layers[i][k].cell);
        for (size_t w_in = 0; w_in < w_info.inputs.size(); w_in++) {
          int old_in_x = l_i_x[k] + w_info.inputs[w_in].xblock;
          int new_in_x = c_x + info_c.inputs[input_k].xblock;
          deltas.push_back(new_in_x - old_in_x);
          input_k++;
        }
      }

      // Attempt to resolve the displacements by propagating them upward
      // through the layers above i.
      std::vector<Layer> network;
      for (int l = 0; l < i; l++) {
        Layer lay;
        for (const NC &nc : layers[l]) {
          if (nc.left_space > 0)
            lay.push_back(Cell(Gate::SPACER, nc.left_space));
          lay.push_back(nc.cell);
        }
        network.push_back(lay);
      }

      int in_chute_start_layer_i = 0;
      for (int k = 0; k < layer_i_start_idx; k++) {
        in_chute_start_layer_i +=
          library.GetInfo(layers[i][k].cell).inputs.size();
      }

      if (!Optimization::ResolveDisplacementUpward(library, network,
                                                   in_chute_start_layer_i,
                                                   deltas)) {
        continue;
      }

      for (int l = 0; l < i; l++) {
        layers[l] = NormalizeLayer(network[l]);
      }

      NC new_c{
        .left_space = c_x - left_limit_i,
        .cell = c,
      };

      int next_left_space_i = 0;
      if (layer_i_end_idx + 1 < (int)layers[i].size()) {
        next_left_space_i = l_i_x[layer_i_end_idx + 1] - c_end;
      }

      std::vector<NC> new_layer_i;
      for (int k = 0; k < layer_i_start_idx; k++) {
        new_layer_i.push_back(layers[i][k]);
      }
      new_layer_i.push_back(new_c);
      for (size_t k = layer_i_end_idx + 1; k < layers[i].size(); k++) {
        NC nc = layers[i][k];
        if (k == layer_i_end_idx + 1) nc.left_space = next_left_space_i;
        new_layer_i.push_back(nc);
      }
      layers[i] = std::move(new_layer_i);

      int next_left_space_i_plus_1 = 0;
      if (idx + 1 < (int)layers[i + 1].size()) {
        next_left_space_i_plus_1 = l_i_plus_1_x[idx + 1] - curr_left_limit;
      }
      std::vector<NC> new_layer_i_plus_1;
      for (int k = 0; k < idx; k++) {
        new_layer_i_plus_1.push_back(layers[i + 1][k]);
      }
      for (const NC &nc : new_wires) {
        new_layer_i_plus_1.push_back(nc);
      }
      for (size_t k = idx + 1; k < layers[i + 1].size(); k++) {
        NC nc = layers[i + 1][k];
        if (k == idx + 1) nc.left_space = next_left_space_i_plus_1;
        new_layer_i_plus_1.push_back(nc);
      }
      layers[i + 1] = std::move(new_layer_i_plus_1);

      return true;
    }

    return false;
  }

  bool StraightenWiresPass() {
    bool changed = false;
    for (size_t i = 0; i < layers.size(); ) {
      if (StraightenWires(i)) {
        changed = true;
      } else {
        i++;
      }
    }

    if (changed) {
      improve_count++;
      return true;
    }

    return false;
  }

  bool StraightenWires(int i) {
    // Only attempt this on layers that are already all wires.
    for (const NC &nc : layers[i]) {
      if (!IsWire(nc.cell.gate)) return false;
    }

    std::vector<int> deltas;
    int curr_x = 0;
    bool all_straight = true;

    for (size_t k = 0; k < layers[i].size(); k++) {
      const Cell &c = layers[i][k].cell;
      CellLibrary::Info info = library.GetInfo(c);

      curr_x += layers[i][k].left_space;
      int in_x = curr_x + info.inputs[0].xblock;
      int out_x = curr_x + info.outputs[0].xblock;
      // We want to delete this layer, so layer i-1's output will directly feed
      // layer i+1's input. Layer i+1 currently expects its input at out_x,
      // but layer i-1 currently outputs at in_x. We ask layer i-1 to shift its
      // output horizontally by (out_x - in_x) to bridge the gap.
      deltas.push_back(out_x - in_x);

      if (c.v != 0) {
        all_straight = false;
      }

      curr_x += info.block_width;
    }

    if (all_straight) {
      layers.erase(layers.begin() + i);
      return true;
    }

    std::vector<Layer> network;
    for (int l = 0; l < i; l++) {
      Layer lay;
      for (const NC &nc : layers[l]) {
        if (nc.left_space > 0) {
          lay.push_back(Cell(Gate::SPACER, nc.left_space));
        }
        lay.push_back(nc.cell);
      }
      network.push_back(lay);
    }

    if (!Optimization::ResolveDisplacementUpward(library, network, 0, deltas)) {
      return false;
    }

    for (int l = 0; l < i; l++) {
      layers[l] = NormalizeLayer(network[l]);
    }

    layers.erase(layers.begin() + i);
    return true;
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

      // Keep moving gates up while it's possible to do so.
      while (MoveCellsUpPass()) {}

      // Try to straighten wires to reduce the overall absolute
      // displacement. If it creates a layer that is only straight
      // wires, it removes that layer.
      while (StraightenWiresPass()) {}

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


Layout Optimization::Optimize(const CellLibrary &library,
                              const Layout &layout) {
  Optimizer optimizer(library, layout);
  optimizer.Run();
  return optimizer.Get();
}

// OK for the top-layer inputs (i.e., for the entire circuit) to move.
bool Optimization::ResolveDisplacementUpward(
    const CellLibrary &library,
    std::span<Layer> network,
    int start_chute,
    std::span<const int> deltas) {

  if (network.empty()) return true;

  std::vector<Layer> new_network(network.size());
  int bottom_outputs = LayerArity(network.back()).second;

  std::vector<int> current_deltas(bottom_outputs, 0);
  for (size_t i = 0; i < deltas.size(); ++i) {
    int idx = start_chute + i;
    if (idx >= 0 && idx < bottom_outputs) {
      current_deltas[idx] = deltas[i];
    }
  }

  for (int layer_idx = (int)network.size() - 1; layer_idx >= 0; --layer_idx) {
    const Layer &layer = network[layer_idx];

    std::vector<Cell> orig_cells;
    std::vector<int> orig_x;
    int curr_x = 0;
    for (const Cell &c : layer) {
      if (c.gate == Gate::SPACER) {
        curr_x += c.v;
      } else {
        orig_cells.push_back(c);
        orig_x.push_back(curr_x);
        curr_x += library.GetInfo(c).block_width;
      }
    }

    int num_cells = orig_cells.size();
    if (num_cells == 0) {
      new_network[layer_idx] = layer;
      continue;
    }

    struct Config {
      Config() {}
      Cell cell = Cell(Gate::SPACER);
      int x_pos = 0;
      int right_edge = 0;
      std::vector<int> in_deltas;
      int prev_config_idx = -1;
      int64_t total_cost = 0;
    };

    std::vector<std::vector<Config>> dp(num_cells);
    int out_chute_idx = 0;

    for (int i = 0; i < num_cells; ++i) {
      const Cell &old_cell = orig_cells[i];
      CellLibrary::Info old_info = library.GetInfo(old_cell);
      int num_outputs = old_info.outputs.size();
      int num_inputs = old_info.inputs.size();

      std::vector<int> cell_out_deltas;
      for (int k = 0; k < num_outputs; ++k) {
        int idx = out_chute_idx + k;
        cell_out_deltas.push_back(idx < current_deltas.size() ?
                                  current_deltas[idx] : 0);
      }

      if (!IsWire(old_cell.gate)) {
        // For non-wires, we can only shift the entire cell rigidly.
        // This is only possible if all requested output displacements are identical.
        bool equal = true;
        int d = cell_out_deltas.empty() ? 0 : cell_out_deltas[0];
        for (int cd : cell_out_deltas) {
          if (cd != d) equal = false;
        }
        if (equal) {
          int x_pos = orig_x[i] + d;
          // Cells cannot be placed before the left edge of the board.
          // If x_pos < 0, this shift is physically impossible.
          if (x_pos >= 0) {
            Config cfg;
            cfg.cell = old_cell;
            cfg.x_pos = x_pos;
            cfg.right_edge = cfg.x_pos + old_info.block_width;
            // The entire cell shifts by d, so its inputs must also shift by d.
            cfg.in_deltas.assign(num_inputs, d);
            cfg.total_cost = 0;
            dp[i].push_back(cfg);
          }
        }

      } else {
        // Wires have exactly one output. We need a wire whose output matches
        // the requested absolute X position.
        int d = cell_out_deltas[0];
        int req_out_x = orig_x[i] + old_info.outputs[0].xblock + d;
        int old_in_x = orig_x[i] + old_info.inputs[0].xblock;

        CType type = GetWireType(old_cell.gate);
        // Try all wire shapes to see which ones can reach req_out_x.
        for (const Cell &w : AllWires(type)) {
          CellLibrary::Info w_info = library.GetInfo(w);
          int x_pos = req_out_x - w_info.outputs[0].xblock;

          // Reject wires that would protrude past the left edge.
          if (x_pos < 0) continue;

          int in_x = x_pos + w_info.inputs[0].xblock;
          // The input of this wire will be at in_x, but the layer above originally
          // provided the input at old_in_x. Thus, the layer above must shift its
          // output by in_delta to match.
          int in_delta = in_x - old_in_x;

          Config cfg;
          cfg.cell = w;
          cfg.x_pos = x_pos;
          cfg.right_edge = x_pos + w_info.block_width;
          cfg.in_deltas.assign(num_inputs, in_delta);
          // Prefer wires that demand smaller shifts from the layers above.
          cfg.total_cost = std::abs(in_delta);
          dp[i].push_back(cfg);
        }
      }

      if (dp[i].empty()) return false;

      if (i > 0) {
        std::vector<Config> filtered;
        for (Config &cfg : dp[i]) {
          int64_t best_prev_cost = -1;
          int best_prev_idx = -1;

          for (size_t prev_idx = 0; prev_idx < dp[i - 1].size(); ++prev_idx) {
            const Config &prev_cfg = dp[i - 1][prev_idx];
            if (prev_cfg.right_edge <= cfg.x_pos) {
              if (best_prev_idx == -1 || prev_cfg.total_cost < best_prev_cost) {
                best_prev_cost = prev_cfg.total_cost;
                best_prev_idx = prev_idx;
              }
            }
          }

          if (best_prev_idx != -1) {
            cfg.prev_config_idx = best_prev_idx;
            cfg.total_cost += best_prev_cost;
            filtered.push_back(cfg);
          }
        }
        dp[i] = std::move(filtered);
        if (dp[i].empty()) return false;
      }

      out_chute_idx += num_outputs;
    }

    int best_last_idx = -1;
    int64_t best_cost = -1;
    for (size_t idx = 0; idx < dp[num_cells - 1].size(); ++idx) {
      if (best_last_idx == -1 ||
          dp[num_cells - 1][idx].total_cost < best_cost) {
        best_cost = dp[num_cells - 1][idx].total_cost;
        best_last_idx = idx;
      }
    }

    if (best_last_idx == -1) return false;

    std::vector<Config> selected(num_cells);
    int curr_idx = best_last_idx;
    for (int i = num_cells - 1; i >= 0; --i) {
      selected[i] = dp[i][curr_idx];
      curr_idx = selected[i].prev_config_idx;
    }

    Layer new_layer;
    std::vector<int> next_deltas;
    int current_x = 0;
    for (int i = 0; i < num_cells; ++i) {
      const Config &cfg = selected[i];
      int space = cfg.x_pos - current_x;
      if (space > 0) {
        new_layer.push_back(Cell(Gate::SPACER, space));
      }
      new_layer.push_back(cfg.cell);
      current_x = cfg.right_edge;

      for (int d : cfg.in_deltas) {
        next_deltas.push_back(d);
      }
    }

    new_network[layer_idx] = std::move(new_layer);
    current_deltas = std::move(next_deltas);
  }

  for (size_t i = 0; i < network.size(); ++i) {
    network[i] = std::move(new_network[i]);
  }

  return true;
}


