
#include "optimization.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>
#include <span>
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

#define TALLY_WIRES 1

#ifdef TALLY_WIRES
static std::mutex mu;
static std::array<int, 128> desired_wires = {};
#endif

static constexpr int VERBOSE = 0;

static CType GetWireType(Gate g) {
  if (g == Gate::WIRE0A || g == Gate::WIRE0B) return CType::ZERO;
  if (g == Gate::WIRE1A || g == Gate::WIRE1B) return CType::ONE;
  return CType::MIXED;
}

// A normalized cell. We remove all spacers, instead recording the
// non-negative space before a cell. The leftmost cell can have
// negative space (to allow us to expand beyond the left edge of the
// circuit if needed), since it never overlaps anything. We patch
// this up when exporting the layout.
struct NC {
  int left_space = 0;
  Cell cell;
};

static std::vector<NC> NormalizeLayer(const std::vector<Cell> &layer) {
  std::vector<NC> ret;
  int space = 0;
  for (const Cell &cell : layer) {
    if (cell.gate == Gate::SPACER) {
      space += cell.v;
    } else {
      ret.push_back(NC{
          .left_space = space,
          .cell = cell,
      });
      space = 0;
    }
  }
  // This deliberately ignores any trailing space.
  return ret;
}

static Layer DenormalizeLayer(const std::vector<NC> &nlayer, int offset = 0) {
  Layer layer;
  bool first = true;
  for (const NC &nc : nlayer) {
    int space = nc.left_space;
    if (first) {
      space += offset;
      first = false;
    }
    if (space > 0) {
      layer.push_back(Cell(Gate::SPACER, space));
    }
    layer.push_back(nc.cell);
  }
  return layer;
}

static bool ResolveDisplacementUpwardInternal(
    const CellLibrary &library,
    std::span<std::vector<NC>> network,
    int start_chute,
    std::span<const int> deltas,
    bool top_can_move);

struct Optimizer {
  static constexpr int MAX_PROPAGATE_DISTANCE = 20;
  // Need to be able to access the dimensions of cells so that
  // we know how they can be moved around.
  const CellLibrary &library;

  // We mostly work on the circuit, so expand the Layout object into
  // its fields (and use a more convenient representation).
  std::vector<std::pair<int, CType>> input_vars;
  std::vector<std::vector<NC>> layers;

  // We increment this whenever we make definite progress. This
  // implies that we have some well-founded order on circuits in mind.
  // This is a lexicographic ordering along the lines of (number of
  // layers, number of layers with only wires, global straightness of
  // wires). (Best if we can say precisely what it is!)
  int improve_count = 0;

  Optimizer(const CellLibrary &library,
            Layout original) : library(library),
                               input_vars(std::move(original.input_vars)),
                               layers(VectorMap(original.circuit.layers,
                                                NormalizeLayer)) {
  }

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

      int orig_c_x = l_i_plus_1_x[idx];
      std::vector<int> candidate_c_x = {orig_c_x};
      {
        CellLibrary::Info first_w_info =
            library.GetInfo(layers[i][layer_i_start_idx].cell);
        int first_w_in_x = l_i_x[layer_i_start_idx] +
                           first_w_info.inputs[0].xblock;
        int first_w_out_x = l_i_x[layer_i_start_idx] +
                            first_w_info.outputs[0].xblock;
        int wire_disp = first_w_out_x - first_w_in_x;
        if (wire_disp != 0) {
          candidate_c_x.push_back(orig_c_x - wire_disp);
        }
      }

      bool placed = false;
      for (int c_x : candidate_c_x) {
        int target_disp = orig_c_x - c_x;
        int abs_disp = std::abs(target_disp);
        if (!CellLibrary::ValidWireSize(abs_disp)) continue;

        // We need to replace c in layer i+1 with wires that route its
        // outputs. Check if these new wires fit in the available space.
        std::vector<NC> new_wires;
        bool fit_wires = true;
        int curr_left_limit = -1000000000;
        if (idx > 0) {
          CellLibrary::Info prev_info =
              library.GetInfo(layers[i + 1][idx - 1].cell);
          curr_left_limit = l_i_plus_1_x[idx - 1] + prev_info.block_width;
        }

        for (size_t j = 0; j < info_c.outputs.size(); j++) {
          bool found_wire = false;
          std::vector<Cell> candidate_wires;
          candidate_wires.push_back(CellLibrary::Wire(
              abs_disp, CellLibrary::Bias::RIGHT, info_c.outputs[j].type));
          if (abs_disp < CellLibrary::SMALL_WIRE) {
            candidate_wires.push_back(CellLibrary::Wire(
                abs_disp, CellLibrary::Bias::LEFT, info_c.outputs[j].type));
          }

          for (Cell &w : candidate_wires) {
            if (target_disp < 0) w.flip = true;
            CellLibrary::Info info_w = library.GetInfo(w);
            int w_x = c_x + info_c.outputs[j].xblock - info_w.inputs[0].xblock;
            if (w_x >= curr_left_limit) {
              NC nc_w{
                .left_space = (idx == 0 && new_wires.empty())
                                  ? w_x
                                  : (w_x - curr_left_limit),
                .cell = w,
              };
              new_wires.push_back(nc_w);
              curr_left_limit = w_x + info_w.block_width;
              found_wire = true;
              break;
            }
          }
          if (!found_wire) {
            fit_wires = false;
            break;
          }
        }

        int right_limit_i_plus_1 = 1000000000;
        if (idx + 1 < (int)layers[i + 1].size()) {
          right_limit_i_plus_1 = l_i_plus_1_x[idx + 1];
        }
        if (idx > 0 || !new_wires.empty()) {
          if (curr_left_limit > right_limit_i_plus_1) fit_wires = false;
        }
        if (!fit_wires) continue;

        // Check if cell c fits in layer i within the space freed up by
        // removing the wires.
        int c_end = c_x + info_c.block_width;
        int left_limit_i = -1000000000;
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

        // Calculate the horizontal displacement required for each input
        // connection. Cell c replaces some wires in layer i. It will be
        // placed at c_x. Its inputs will be at new_in_x, but the layer
        // above (i-1) currently outputs at old_in_x (where the wires
        // received their inputs). So layer i-1 must shift its outputs
        // by (new_in_x - old_in_x) to match.
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
        int start_layer = std::max(0, i - MAX_PROPAGATE_DISTANCE);
        std::span<std::vector<NC>> network(layers.data() + start_layer,
                                           i - start_layer);

        int in_chute_start_layer_i = 0;
        for (int k = 0; k < layer_i_start_idx; k++) {
          in_chute_start_layer_i +=
            library.GetInfo(layers[i][k].cell).inputs.size();
        }

        if (!ResolveDisplacementUpwardInternal(library, network,
                                               in_chute_start_layer_i,
                                               deltas, start_layer == 0)) {
          continue;
        }

        NC new_c{
          .left_space = (layer_i_start_idx == 0) ? c_x : (c_x - left_limit_i),
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
          if (idx == 0 && new_wires.empty()) {
            next_left_space_i_plus_1 = l_i_plus_1_x[idx + 1];
          } else {
            next_left_space_i_plus_1 = l_i_plus_1_x[idx + 1] - curr_left_limit;
          }
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

        placed = true;
        break;
      }

      if (placed) return true;
    }

    return false;
  }

  enum class StraightenResult {
    NOTHING,
    MODIFIED,
    DELETED,
  };

  bool StraightenWiresPass() {
    bool changed = false;
    for (size_t i = 0; i < layers.size(); ) {
      switch (StraightenWires(i)) {
      case StraightenResult::NOTHING:
        i++;
        break;
      case StraightenResult::MODIFIED:
        i++;
        changed = true;
        break;
      case StraightenResult::DELETED:
        changed = true;
        break;
      }

    }

    if (changed) {
      improve_count++;
      return true;
    }

    return false;
  }

  StraightenResult StraightenIndividualWires(int i) {
    bool changed = false;
    bool local_changed;
    do {
      local_changed = false;
      int current_in_chute = 0;
      int curr_x = 0;

      std::vector<int> x_coords(layers[i].size());
      for (size_t k = 0; k < layers[i].size(); k++) {
        curr_x += layers[i][k].left_space;
        x_coords[k] = curr_x;
        curr_x += library.GetInfo(layers[i][k].cell).block_width;
      }

      // Try to straighten all non-straight wires simultaneously.
      std::vector<int> new_x = x_coords;
      std::vector<Cell> new_cells;
      new_cells.reserve(layers[i].size());
      for (size_t k = 0; k < layers[i].size(); k++) {
        new_cells.push_back(layers[i][k].cell);
      }

      bool any_wants_straighten = false;
      std::vector<int> all_deltas;

      for (size_t k = 0; k < layers[i].size(); k++) {
        const NC &nc = layers[i][k];
        const Cell &c = nc.cell;
        CellLibrary::Info info = library.GetInfo(c);

        if (IsWire(c.gate) && c.v != 0) {
          int in_x = x_coords[k] + info.inputs[0].xblock;
          int out_x = x_coords[k] + info.outputs[0].xblock;

          Cell straight_wire = CellLibrary::Wire(0, CellLibrary::Bias::RIGHT,
                                                 GetWireType(c.gate));
          CellLibrary::Info straight_info = library.GetInfo(straight_wire);

          int new_c_x = out_x - straight_info.outputs[0].xblock;
          int new_in_x = new_c_x + straight_info.inputs[0].xblock;
          int delta = new_in_x - in_x;

          new_x[k] = new_c_x;
          new_cells[k] = straight_wire;
          all_deltas.push_back(delta);
          any_wants_straighten = true;
        } else {
          for (size_t j = 0; j < info.inputs.size(); j++) {
            all_deltas.push_back(0);
          }
        }
      }

      if (any_wants_straighten) {
        bool overlap = false;
        for (size_t k = 0; k + 1 < layers[i].size(); k++) {
          int right_edge = new_x[k] + library.GetInfo(new_cells[k]).block_width;
          if (right_edge > new_x[k + 1]) {
            overlap = true;
            break;
          }
        }
        if (!overlap) {
          int start_layer = std::max(0, i - MAX_PROPAGATE_DISTANCE);
          std::span<std::vector<NC>> network(layers.data() + start_layer, i - start_layer);
          if (ResolveDisplacementUpwardInternal(
                  library, network, 0, all_deltas, start_layer == 0)) {
            std::vector<NC> new_layer_i;
            new_layer_i.reserve(layers[i].size());
            for (size_t k = 0; k < layers[i].size(); k++) {
              int left_space = 0;
              if (k == 0) {
                left_space = new_x[k];
              } else {
                int prev_right = new_x[k - 1] +
                                 library.GetInfo(new_cells[k - 1]).block_width;
                left_space = new_x[k] - prev_right;
              }
              new_layer_i.push_back(NC{
                  .left_space = left_space,
                  .cell = new_cells[k],
                });
            }
            layers[i] = std::move(new_layer_i);

            local_changed = true;
            changed = true;
            continue;
          }
        }
      }

      current_in_chute = 0;
      for (size_t k = 0; k < layers[i].size(); k++) {
        const NC &nc = layers[i][k];
        const Cell &c = nc.cell;
        CellLibrary::Info info = library.GetInfo(c);

        if (IsWire(c.gate) && c.v != 0) {
          int in_x = x_coords[k] + info.inputs[0].xblock;
          int out_x = x_coords[k] + info.outputs[0].xblock;
          int target_disp = out_x - in_x;

          bool found_better = false;
          for (int try_v = 0; try_v < c.v; try_v++) {
            if (!CellLibrary::ValidWireSize(try_v)) continue;

            std::vector<Cell> candidate_wires;
            candidate_wires.push_back(CellLibrary::Wire(
                try_v, CellLibrary::Bias::RIGHT, GetWireType(c.gate)));
            if (try_v < CellLibrary::SMALL_WIRE) {
              candidate_wires.push_back(CellLibrary::Wire(
                try_v, CellLibrary::Bias::LEFT, GetWireType(c.gate)));
            }

            for (Cell &straight_wire : candidate_wires) {
              if (target_disp < 0) straight_wire.flip = true;
              CellLibrary::Info straight_info = library.GetInfo(straight_wire);

              int new_c_x = out_x - straight_info.outputs[0].xblock;
              int new_in_x = new_c_x + straight_info.inputs[0].xblock;
              int delta = new_in_x - in_x;

              int left_limit = -1000000000;
              if (k > 0) {
                CellLibrary::Info prev_info =
                    library.GetInfo(layers[i][k - 1].cell);
                left_limit = x_coords[k - 1] + prev_info.block_width;
              }

              int right_limit = 1000000000;
              if (k + 1 < layers[i].size()) {
                right_limit = x_coords[k + 1];
              }

              if (new_c_x >= left_limit &&
                  new_c_x + straight_info.block_width <= right_limit) {
                int start_layer = std::max(0, i - MAX_PROPAGATE_DISTANCE);
                std::span<std::vector<NC>> network(layers.data() + start_layer, i - start_layer);

                std::vector<int> deltas = {delta};
                if (ResolveDisplacementUpwardInternal(
                        library, network, current_in_chute, deltas,
                        start_layer == 0)) {
                  std::vector<NC> new_layer_i;
                  for (size_t j = 0; j < k; j++) {
                    new_layer_i.push_back(layers[i][j]);
                  }

                  NC new_nc{
                    .left_space = (k == 0) ? new_c_x : (new_c_x - left_limit),
                    .cell = straight_wire,
                  };
                  new_layer_i.push_back(new_nc);

                  if (k + 1 < layers[i].size()) {
                    NC next_nc = layers[i][k + 1];
                    next_nc.left_space =
                        right_limit - (new_c_x + straight_info.block_width);
                    new_layer_i.push_back(next_nc);
                    for (size_t j = k + 2; j < layers[i].size(); j++) {
                      new_layer_i.push_back(layers[i][j]);
                    }
                  }

                  layers[i] = std::move(new_layer_i);
                  local_changed = true;
                  changed = true;
                  found_better = true;
                  break;
                }
              }
            }
            if (found_better) break;
          }
          if (found_better) break;
        }

        current_in_chute += info.inputs.size();
      }
    } while (local_changed);

    return changed ? StraightenResult::MODIFIED : StraightenResult::NOTHING;
  }

  StraightenResult StraightenWires(int i) {
    // Only attempt this on layers that are already all wires.
    for (const NC &nc : layers[i]) {
      if (!IsWire(nc.cell.gate)) {
        return StraightenIndividualWires(i);
      }
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
      return StraightenResult::DELETED;
    }

    int start_layer = std::max(0, i - MAX_PROPAGATE_DISTANCE);
    std::span<std::vector<NC>> network(layers.data() + start_layer, i - start_layer);

    if (!ResolveDisplacementUpwardInternal(library, network, 0, deltas,
                                           start_layer == 0)) {
      return StraightenIndividualWires(i);
    }

    layers.erase(layers.begin() + i);
    return StraightenResult::DELETED;
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
    int min_x = 0;
    for (const std::vector<NC> &nlayer : layers) {
      int curr_x = 0;
      for (const NC &nc : nlayer) {
        curr_x += nc.left_space;
        if (curr_x < min_x) {
          min_x = curr_x;
        }
        curr_x += library.GetInfo(nc.cell).block_width;
      }
    }

    std::vector<Layer> ret_layers;
    ret_layers.reserve(layers.size());
    for (const std::vector<NC> &nlayer : layers) {
      ret_layers.push_back(DenormalizeLayer(nlayer, -min_x));
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
  Layout result = optimizer.Get();

#ifdef TALLY_WIRES
  {
    std::lock_guard<std::mutex> lock(mu);
    for (size_t i = 0; i < desired_wires.size(); ++i) {
      if (desired_wires[i] > 0) {
        Print("Desired wire size {}: {}\n", i, desired_wires[i]);
      }
    }
  }
#endif

  return result;
}

static bool ResolveDisplacementUpwardInternal(
    const CellLibrary &library,
    std::span<std::vector<NC>> network,
    int start_chute,
    std::span<const int> deltas,
    bool top_can_move) {

  if (network.empty()) return true;

  std::vector<std::vector<NC>> new_network(network.size());

  int bottom_outputs = 0;
  for (const NC &nc : network.back()) {
    bottom_outputs += library.GetInfo(nc.cell).outputs.size();
  }

  std::vector<int> current_deltas(bottom_outputs, 0);
  for (size_t i = 0; i < deltas.size(); ++i) {
    int idx = start_chute + i;
    if (idx >= 0 && idx < bottom_outputs) {
      current_deltas[idx] = deltas[i];
    }
  }

  for (int layer_idx = (int)network.size() - 1; layer_idx >= 0; --layer_idx) {
    const std::vector<NC> &layer = network[layer_idx];

    std::vector<Cell> orig_cells;
    std::vector<int> orig_x;
    int curr_x = 0;
    for (const NC &nc : layer) {
      curr_x += nc.left_space;
      orig_cells.push_back(nc.cell);
      orig_x.push_back(curr_x);
      curr_x += library.GetInfo(nc.cell).block_width;
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
        // This is only possible if all requested output displacements
        // are identical.
        bool equal = true;
        int d = cell_out_deltas.empty() ? 0 : cell_out_deltas[0];
        for (int cd : cell_out_deltas) {
          if (cd != d) equal = false;
        }
        if (equal) {
          int x_pos = orig_x[i] + d;
          Config cfg;
          cfg.cell = old_cell;
          cfg.x_pos = x_pos;
          cfg.right_edge = cfg.x_pos + old_info.block_width;
          // The entire cell shifts by d, so its inputs must also shift by d.
          cfg.in_deltas.assign(num_inputs, d);
          cfg.total_cost = 0;
          dp[i].push_back(cfg);
        }

      } else {
        // Wires have exactly one output. We need a wire whose output matches
        // the requested absolute X position.
        int d = cell_out_deltas[0];
        int req_out_x = orig_x[i] + old_info.outputs[0].xblock + d;
        int old_in_x = orig_x[i] + old_info.inputs[0].xblock;

        CType type = GetWireType(old_cell.gate);
        int target_disp = req_out_x - old_in_x;
        int abs_disp = std::abs(target_disp);

        std::vector<Cell> candidate_wires;
        // 1. Wire(s) that perfectly absorb the displacement.
        if (CellLibrary::ValidWireSize(abs_disp)) {
          bool flip = (target_disp < 0);
          Cell w = CellLibrary::Wire(abs_disp, CellLibrary::Bias::RIGHT,
                                     type);
          w.flip = flip;
          candidate_wires.push_back(w);
          if (abs_disp < CellLibrary::SMALL_WIRE) {
            Cell w2 = CellLibrary::Wire(abs_disp, CellLibrary::Bias::LEFT,
                                        type);
            w2.flip = flip;
            candidate_wires.push_back(w2);
          }
        } else {
#ifdef TALLY_WIRES
          if (abs_disp < (int)desired_wires.size()) {
            std::lock_guard<std::mutex> lock(mu);
            desired_wires[abs_disp]++;
          }
#endif
        }

        // 2. The old wire, which rigidly shifts and passes
        // displacement upward.
        bool has_old = false;
        for (const Cell &w : candidate_wires) {
          if (w == old_cell) {
            has_old = true;
            break;
          }
        }
        if (!has_old) {
          candidate_wires.push_back(old_cell);
        }

        for (const Cell &w : candidate_wires) {
          CellLibrary::Info w_info = library.GetInfo(w);
          int x_pos = req_out_x - w_info.outputs[0].xblock;

          int in_x = x_pos + w_info.inputs[0].xblock;
          // The input of this wire will be at in_x, but the layer
          // above originally provided the input at old_in_x. Thus,
          // the layer above must shift its output by in_delta to
          // match.
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

    std::vector<NC> new_layer;
    std::vector<int> next_deltas;
    int current_x = 0;
    for (int i = 0; i < num_cells; ++i) {
      const Config &cfg = selected[i];
      int space = cfg.x_pos - current_x;
      new_layer.push_back(NC{
          .left_space = space,
          .cell = cfg.cell,
      });
      current_x = cfg.right_edge;

      for (int d : cfg.in_deltas) {
        next_deltas.push_back(d);
      }
    }

    new_network[layer_idx] = std::move(new_layer);
    current_deltas = std::move(next_deltas);
  }

  if (!top_can_move) {
    for (int d : current_deltas) {
      if (d != 0) return false;
    }
  }

  for (size_t i = 0; i < network.size(); ++i) {
    network[i] = std::move(new_network[i]);
  }

  return true;
}

// OK for the top-layer inputs (i.e., for the entire circuit) to move.
bool Optimization::ResolveDisplacementUpward(
    const CellLibrary &library,
    std::span<Layer> network,
    int start_chute,
    std::span<const int> deltas) {
  std::vector<std::vector<NC>> nc_network;
  nc_network.reserve(network.size());
  for (const Layer &layer : network) {
    nc_network.push_back(NormalizeLayer(layer));
  }

  bool success = ResolveDisplacementUpwardInternal(
      library, nc_network, start_chute, deltas, true);

  if (success) {
    int min_x = 0;
    for (const std::vector<NC> &nlayer : nc_network) {
      int curr_x = 0;
      for (const NC &nc : nlayer) {
        curr_x += nc.left_space;
        if (curr_x < min_x) {
          min_x = curr_x;
        }
        curr_x += library.GetInfo(nc.cell).block_width;
      }
    }
    for (size_t i = 0; i < network.size(); ++i) {
      network[i] = DenormalizeLayer(nc_network[i], -min_x);
    }
  }

  return success;
}


