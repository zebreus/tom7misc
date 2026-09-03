
#include "optimization.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ansi.h"
#include "base/print.h"
#include "base/stringprintf.h"
#include "cell-library.h"
#include "circuit.h"
#include "dense-int-set.h"
#include "drc.h"
#include "layout.h"
#include "periodically.h"
#include "status-bar.h"
#include "timer.h"
#include "vector-util.h"

#include "atomic-util.h"

DECLARE_COUNTERS(ctr_beamshiftdown,
                 ctr_beamshiftup,
                 ctr_resolve_up,
                 ctr_resolve_down,
                 ctr_move_up,
                 ctr_straighten_indiv,
                 ctr_dp_cells_up,
                 ctr_dp_cells_down);

static constexpr bool COUNT = true;

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

static constexpr bool SELF_CHECK = false;

#define TALLY_WIRES 1

#ifdef TALLY_WIRES
static std::mutex mu;
static std::array<int, 128> desired_wires = {};
#endif

static constexpr int VERBOSE = 0;

static CType GetWireType(Gate g) {
  if (g == Gate::WIRE0A || g == Gate::WIRE0B)
    return CType::ZERO;
  if (g == Gate::WIRE1A || g == Gate::WIRE1B)
    return CType::ONE;
  return CType::MIXED;
}

namespace {
// A normalized cell. We remove all spacers, instead recording the
// non-negative space before a cell. The leftmost cell can have
// negative space (to allow us to expand beyond the left edge of the
// circuit if needed), since it never overlaps anything. We patch
// this up when exporting the layout.
struct NC {
  int left_space = 0;
  Cell cell = Cell(Gate::CONST0);
};

// A local strand of a layer for optimization.
struct LayerStrand {
  // A contiguous sub-span of normalized cells from the layer.
  std::span<const NC> cells;
  // The maximum X coordinate of the right edge of any rigid cell to the left
  // of this strand. The leftmost cell in `cells` cannot be placed to the left
  // of this coordinate.
  int left_obstacle_x = -1000000000;
  // The minimum X coordinate of the left edge of any rigid cell to the right
  // of this strand. The rightmost cell in `cells` cannot be placed to the right
  // of this coordinate.
  int right_obstacle_x = 1000000000;
  // The original starting X coordinate of `cells[0]`.
  int start_x = 0;
  int in_chute_start = 0;
  int out_chute_start = 0;
};
} // namespace

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

static constexpr int WINDOW_EXTRA_CHUTES = 1;
static constexpr int MAX_WINDOW_HEIGHT = 20;

static std::vector<LayerStrand>
MakeWindowedStrandsUpward(const CellLibrary &library,
                          std::span<const std::vector<NC>> network,
                          int start_chute, int num_chutes) {
  std::vector<LayerStrand> strands;
  strands.reserve(network.size());

  int curr_start_chute = start_chute;
  int curr_num_chutes = num_chutes;

  for (int i = (int)network.size() - 1; i >= 0; --i) {
    const std::vector<NC> &layer = network[i];

    int target_start = std::max(0, curr_start_chute - WINDOW_EXTRA_CHUTES);
    int target_end = curr_start_chute + curr_num_chutes + WINDOW_EXTRA_CHUTES;

    int out_chute_idx = 0;
    int first_idx = -1;
    int last_idx = -1;
    int start_x = 0;
    int curr_x = 0;
    int out_chute_start = 0;

    for (size_t k = 0; k < layer.size(); ++k) {
      curr_x += layer[k].left_space;
      CellLibrary::Info info = library.GetInfo(layer[k].cell);
      int num_out = info.outputs.size();

      // If a cell has multiple outputs and overlaps the window boundary, the
      // entire cell is included in the strand, and the chute window expands
      // to cover all of its inputs for the next layer up. During resolution,
      // if the requested displacements across its outputs are non-uniform, the
      // DP will fail to shift it. Thus, it effectively acts as a fixed
      // obstacle that anchors the strand against such shifts.
      bool overlap = !(out_chute_idx + num_out <= target_start ||
                       out_chute_idx >= target_end);
      if (overlap) {
        if (first_idx == -1) {
          first_idx = k;
          start_x = curr_x;
          out_chute_start = out_chute_idx;
        }
        last_idx = k;
      }
      out_chute_idx += num_out;
      curr_x += info.block_width;
    }

    if (first_idx != -1) {
      int in_chute = 0;
      for (int k = 0; k < first_idx; ++k) {
        in_chute += library.GetInfo(layer[k].cell).inputs.size();
      }
      curr_start_chute = in_chute;

      int num_in = 0;
      for (int k = first_idx; k <= last_idx; ++k) {
        num_in += library.GetInfo(layer[k].cell).inputs.size();
      }
      curr_num_chutes = num_in;

      int left_obs = -1000000000;
      if (first_idx > 0) {
        int obs_x = 0;
        for (int k = 0; k < first_idx; ++k) {
          obs_x += layer[k].left_space;
          obs_x += library.GetWidth(layer[k].cell);
        }
        left_obs = obs_x;
      }

      int right_obs = 1000000000;
      if (last_idx + 1 < (int)layer.size()) {
        int obs_x = 0;
        for (int k = 0; k <= last_idx + 1; ++k) {
          obs_x += layer[k].left_space;
          if (k <= last_idx) {
            obs_x += library.GetWidth(layer[k].cell);
          }
        }
        right_obs = obs_x;
      }

      strands.push_back(LayerStrand{
          .cells = std::span<const NC>(layer.data() + first_idx,
                                       last_idx - first_idx + 1),
          .left_obstacle_x = left_obs,
          .right_obstacle_x = right_obs,
          .start_x = start_x,
          .in_chute_start = in_chute,
          .out_chute_start = out_chute_start,
      });
    } else {
      curr_start_chute = 0;
      curr_num_chutes = 0;
      strands.push_back(LayerStrand{
          .cells = std::span<const NC>(),
          .left_obstacle_x = -1000000000,
          .right_obstacle_x = 1000000000,
          .start_x = 0,
          .in_chute_start = 0,
          .out_chute_start = 0,
      });
    }
  }

  std::reverse(strands.begin(), strands.end());
  return strands;
}

static std::vector<LayerStrand>
MakeWindowedStrandsDownward(const CellLibrary &library,
                            std::span<const std::vector<NC>> network,
                            int start_chute, int num_chutes) {
  std::vector<LayerStrand> strands;
  strands.reserve(network.size());

  int curr_start_chute = start_chute;
  int curr_num_chutes = num_chutes;

  for (int i = 0; i < (int)network.size(); ++i) {
    const std::vector<NC> &layer = network[i];

    int target_start = std::max(0, curr_start_chute - WINDOW_EXTRA_CHUTES);
    int target_end = curr_start_chute + curr_num_chutes + WINDOW_EXTRA_CHUTES;

    int in_chute_idx = 0;
    int first_idx = -1;
    int last_idx = -1;
    int start_x = 0;
    int curr_x = 0;
    int in_chute_start = 0;

    for (size_t k = 0; k < layer.size(); ++k) {
      curr_x += layer[k].left_space;
      CellLibrary::Info info = library.GetInfo(layer[k].cell);
      int num_in = info.inputs.size();

      bool overlap = !(in_chute_idx + num_in <= target_start ||
                       in_chute_idx >= target_end);
      if (overlap) {
        if (first_idx == -1) {
          first_idx = k;
          start_x = curr_x;
          in_chute_start = in_chute_idx;
        }
        last_idx = k;
      }
      in_chute_idx += num_in;
      curr_x += info.block_width;
    }

    if (first_idx != -1) {
      int out_chute = 0;
      for (int k = 0; k < first_idx; ++k) {
        out_chute += library.GetInfo(layer[k].cell).outputs.size();
      }
      curr_start_chute = out_chute;

      int num_out = 0;
      for (int k = first_idx; k <= last_idx; ++k) {
        num_out += library.GetInfo(layer[k].cell).outputs.size();
      }
      curr_num_chutes = num_out;

      int left_obs = -1000000000;
      if (first_idx > 0) {
        int obs_x = 0;
        for (int k = 0; k < first_idx; ++k) {
          obs_x += layer[k].left_space;
          obs_x += library.GetWidth(layer[k].cell);
        }
        left_obs = obs_x;
      }

      int right_obs = 1000000000;
      if (last_idx + 1 < (int)layer.size()) {
        int obs_x = 0;
        for (int k = 0; k <= last_idx + 1; ++k) {
          obs_x += layer[k].left_space;
          if (k <= last_idx) {
            obs_x += library.GetWidth(layer[k].cell);
          }
        }
        right_obs = obs_x;
      }

      strands.push_back(LayerStrand{
          .cells = std::span<const NC>(layer.data() + first_idx,
                                       last_idx - first_idx + 1),
          .left_obstacle_x = left_obs,
          .right_obstacle_x = right_obs,
          .start_x = start_x,
          .in_chute_start = in_chute_start,
          .out_chute_start = out_chute,
      });
    } else {
      curr_start_chute = 0;
      curr_num_chutes = 0;
      strands.push_back(LayerStrand{
          .cells = std::span<const NC>(),
          .left_obstacle_x = -1000000000,
          .right_obstacle_x = 1000000000,
          .start_x = 0,
          .in_chute_start = 0,
          .out_chute_start = 0,
      });
    }
  }

  return strands;
}

static void
ReintegrateWindowedStrands(const CellLibrary &library,
                           std::span<std::vector<NC>> network,
                           const std::vector<LayerStrand> &strands,
                           const std::vector<std::vector<NC>> &resolved) {
  for (size_t i = 0; i < network.size(); ++i) {
    if (strands[i].cells.empty())
      continue;

    int first_idx = strands[i].cells.data() - network[i].data();
    int last_idx = first_idx + strands[i].cells.size() - 1;

    std::vector<NC> new_layer;
    new_layer.reserve(network[i].size() - strands[i].cells.size() +
                      resolved[i].size());

    int curr_x = 0;
    for (int k = 0; k < first_idx; ++k) {
      new_layer.push_back(network[i][k]);
      curr_x += network[i][k].left_space;
      curr_x += library.GetWidth(network[i][k].cell);
    }

    for (size_t k = 0; k < resolved[i].size(); ++k) {
      NC nc = resolved[i][k];
      if (k == 0 && first_idx > 0) {
        nc.left_space -= curr_x;
      }
      new_layer.push_back(nc);
      curr_x += nc.left_space;
      curr_x += library.GetWidth(nc.cell);
    }

    if (last_idx + 1 < (int)network[i].size()) {
      NC next_nc = network[i][last_idx + 1];
      int orig_next_x = strands[i].right_obstacle_x;
      next_nc.left_space = orig_next_x - curr_x;
      new_layer.push_back(next_nc);

      for (size_t k = last_idx + 2; k < network[i].size(); ++k) {
        new_layer.push_back(network[i][k]);
      }
    }

    network[i] = std::move(new_layer);
  }
}

static std::optional<std::vector<std::vector<NC>>>
ResolveDisplacementUpwardStrand(const CellLibrary &library,
                                std::span<const LayerStrand> strand_network,
                                int start_chute_relative,
                                std::span<const int> deltas, bool top_can_move,
                                bool build_result = true);

static std::optional<std::vector<std::vector<NC>>>
ResolveDisplacementDownwardStrand(const CellLibrary &library,
                                  std::span<const LayerStrand> strand_network,
                                  int start_chute_relative,
                                  std::span<const int> deltas,
                                  bool bottom_can_move,
                                  bool build_result = true);

static DenseIntSet ResolveBeamShiftUpwardStrand(
    const CellLibrary &library, std::span<const LayerStrand> strand_network,
    int start_chute, std::span<const int> deltas, int min_shift,
    const DenseIntSet &shifts, bool top_can_move);

static DenseIntSet ResolveBeamShiftDownwardStrand(
    const CellLibrary &library, std::span<const LayerStrand> strand_network,
    int start_chute, std::span<const int> deltas, int min_shift,
    const DenseIntSet &shifts, bool bottom_can_move);

struct Optimizer {
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
  int64_t improve_count = 0;
  int run_iter = 0;
  int layers_removed = 0;
  int cells_moved = 0;

  Timer run_timer;
  Periodically status_per = Periodically(1.0);
  StatusBar *status = nullptr;

  static constexpr int max_wire =
    *std::max_element(CellLibrary::WIRE_SIZES.begin(),
                      CellLibrary::WIRE_SIZES.end());
  static constexpr int max_possible_shift = max_wire * MAX_WINDOW_HEIGHT;

  Optimizer(const CellLibrary &library, Layout original, StatusBar *status)
      : library(library), input_vars(std::move(original.input_vars)),
        layers(VectorMap(original.circuit.layers, NormalizeLayer)),
        status(status) {}

  bool MoveCellsUpPass() {
    bool changed = false;
    for (size_t i = 0; i + 1 < layers.size(); ++i) {
      if (MoveCellsUp(i)) {
        changed = true;
      }

      MaybeStatus("up", i, layers.size());
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
    if (COUNT) ctr_move_up++;
    auto GetXCoords = [&](int layer_idx) {
      std::vector<int> x_coords(layers[layer_idx].size());
      int curr_x = 0;
      for (size_t k = 0; k < layers[layer_idx].size(); k++) {
        curr_x += layers[layer_idx][k].left_space;
        x_coords[k] = curr_x;
        curr_x += library.GetWidth(layers[layer_idx][k].cell);
      }
      return x_coords;
    };

    std::vector<int> l_i_x = GetXCoords(i);
    std::vector<int> l_i_plus_1_x = GetXCoords(i + 1);

    for (int idx = 0; idx < (int)layers[i + 1].size(); idx++) {
      const Cell &c = layers[i + 1][idx].cell;
      if (IsWire(c.gate))
        continue;

      CellLibrary::Info info_c = library.GetInfo(c);
      int num_inputs = info_c.inputs.size();
      if (num_inputs == 0)
        continue;

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
          if (layer_i_start_idx == -1)
            layer_i_start_idx = k;
        }
        current_out_chute += cell_outs;
        if (current_out_chute >= in_chute_start + num_inputs) {
          if (layer_i_end_idx == -1)
            layer_i_end_idx = k;
          break;
        }
      }

      if (layer_i_start_idx == -1 || layer_i_end_idx == -1)
        continue;

      // We can only move c up into layer i if all the cells currently
      // providing its inputs are just wires, which can be pushed down to i+1.
      bool all_wires = true;
      for (int k = layer_i_start_idx; k <= layer_i_end_idx; k++) {
        if (!IsWire(layers[i][k].cell.gate)) {
          all_wires = false;
          break;
        }
      }
      if (!all_wires)
        continue;

      int orig_c_x = l_i_plus_1_x[idx];
      std::vector<int> candidate_c_x = {orig_c_x};
      {
        CellLibrary::Info first_w_info =
            library.GetInfo(layers[i][layer_i_start_idx].cell);
        int first_w_in_x =
            l_i_x[layer_i_start_idx] + first_w_info.inputs[0].xblock;
        int first_w_out_x =
            l_i_x[layer_i_start_idx] + first_w_info.outputs[0].xblock;
        int wire_disp = first_w_out_x - first_w_in_x;
        if (wire_disp != 0) {
          candidate_c_x.push_back(orig_c_x - wire_disp);
        }
      }

      bool placed = false;
      for (int c_x : candidate_c_x) {
        int target_disp = orig_c_x - c_x;
        int abs_disp = std::abs(target_disp);
        if (!CellLibrary::ValidWireSize(abs_disp))
          continue;

        // We need to replace c in layer i+1 with wires that route its
        // outputs. Check if these new wires fit in the available space.
        std::vector<NC> new_wires;
        bool fit_wires = true;
        int curr_left_limit = -1000000000;
        if (idx > 0) {
          curr_left_limit = l_i_plus_1_x[idx - 1] +
                            library.GetWidth(layers[i + 1][idx - 1].cell);
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
            if (target_disp < 0)
              w.flip = true;
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
          if (curr_left_limit > right_limit_i_plus_1)
            fit_wires = false;
        }
        if (!fit_wires)
          continue;

        // Check if cell c fits in layer i within the space freed up by
        // removing the wires.
        int c_end = c_x + info_c.block_width;
        int left_limit_i = -1000000000;
        if (layer_i_start_idx > 0) {
          left_limit_i = l_i_x[layer_i_start_idx - 1] +
                         library.GetWidth(layers[i][layer_i_start_idx - 1].cell);
        }
        if (c_x < left_limit_i)
          continue;

        int right_limit_i = 1000000000;
        if (layer_i_end_idx + 1 < (int)layers[i].size()) {
          right_limit_i = l_i_x[layer_i_end_idx + 1];
        }
        if (c_end > right_limit_i)
          continue;

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
        int start_layer = std::max(0, i - MAX_WINDOW_HEIGHT);
        std::span<std::vector<NC>> network(layers.data() + start_layer,
                                           i - start_layer);

        int in_chute_start_layer_i = 0;
        for (int k = 0; k < layer_i_start_idx; k++) {
          in_chute_start_layer_i +=
              library.GetInfo(layers[i][k].cell).inputs.size();
        }

        std::vector<std::vector<NC>> saved_upward_layers;
        for (int k = start_layer; k < i; k++) {
          saved_upward_layers.push_back(layers[k]);
        }

        int min_shift = left_limit_i - c_x;
        int max_shift = right_limit_i - c_end;

        int end_layer = std::min((int)layers.size(), i + 1 + MAX_WINDOW_HEIGHT);

        // Clamp the shift range to prevent exploring an enormous search space.
        // We can tightly bound the search by the number of layers available
        // to absorb the required shifts.
        int max_shift_up = max_possible_shift;
        if (start_layer > 0) max_shift_up = max_wire * (i - start_layer);
        int max_shift_down = max_possible_shift;
        if (end_layer < (int)layers.size())
          max_shift_down = max_wire * (end_layer - (i + 1));

        int prune_min = -max_shift_down;
        int prune_max = max_shift_down;
        for (int d : deltas) {
          prune_min = std::max(prune_min, -max_shift_up - d);
          prune_max = std::min(prune_max, max_shift_up - d);
        }

        min_shift = std::max(min_shift, prune_min);
        max_shift = std::min(max_shift, prune_max);

        if (min_shift > max_shift)
          continue;

        // We don't want to try thousands of shifts; this is heuristic
        // to try to cover the space sparsely.
        DenseIntSet shifts_to_try(max_shift - min_shift + 1);
        auto TryAdd = [&](int s) {
            if (s >= min_shift && s <= max_shift) {
              shifts_to_try.Add(s - min_shift);
            }
          };
        TryAdd(-1);
        TryAdd(0);
        TryAdd(1);
        TryAdd(left_limit_i - c_x);
        TryAdd(right_limit_i - c_end);
        TryAdd(min_shift);
        TryAdd(max_shift);
        for (int s = 0; s <= max_shift; s += max_wire) TryAdd(s);
        for (int s = 0; s >= min_shift; s -= max_wire) TryAdd(s);

        std::vector<LayerStrand> strands = MakeWindowedStrandsUpward(
            library, network, in_chute_start_layer_i, deltas.size());
        DenseIntSet valid_shifts = ResolveBeamShiftUpwardStrand(
            library, strands, in_chute_start_layer_i, deltas, min_shift,
            shifts_to_try, start_layer == 0);

        std::span<std::vector<NC>> down_network(layers.data() + i + 1,
                                                end_layer - (i + 1));
        std::vector<int> down_deltas_base(info_c.inputs.size(), 0);

        std::vector<LayerStrand> down_strands = MakeWindowedStrandsDownward(
            library, down_network, in_chute_start, down_deltas_base.size());
        valid_shifts = ResolveBeamShiftDownwardStrand(
            library, down_strands, in_chute_start, down_deltas_base, min_shift,
            valid_shifts, end_layer == (int)layers.size());

        if (valid_shifts.Empty()) {
          continue;
        }

        // Pick the shift with the smallest absolute value.
        // (Note: network was not modified by the upward pass since it now only
        // returns a set; we now apply the chosen shift upward, and then
        // downward.)
        int s = 0;
        int zero_bit = -min_shift;
        if (zero_bit >= 0 && zero_bit < (int)valid_shifts.Radix() &&
            valid_shifts.Contains(zero_bit)) {
          s = 0;
        } else {
          int best_abs = 1000000000;
          for (int bit : valid_shifts) {
            int val = min_shift + bit;
            if (std::abs(val) < best_abs) {
              best_abs = std::abs(val);
              s = val;
            }
          }
        }

        std::vector<int> final_up_deltas = deltas;
        for (int &d : final_up_deltas)
          d += s;

        std::vector<LayerStrand> strands2 = MakeWindowedStrandsUpward(
            library, network, in_chute_start_layer_i, final_up_deltas.size());
        auto resolved = ResolveDisplacementUpwardStrand(
            library, strands2, in_chute_start_layer_i, final_up_deltas,
            start_layer == 0);
        if (!resolved.has_value()) {
          continue;
        }
        ReintegrateWindowedStrands(library, network, strands2,
                                   resolved.value());

        NC new_c{
            .left_space = (layer_i_start_idx == 0) ? (c_x + s)
                                                   : ((c_x + s) - left_limit_i),
            .cell = c,
        };

        int next_left_space_i = 0;
        if (layer_i_end_idx + 1 < (int)layers[i].size()) {
          next_left_space_i = l_i_x[layer_i_end_idx + 1] - (c_end + s);
        }

        std::vector<NC> new_layer_i;
        for (int k = 0; k < layer_i_start_idx; k++) {
          new_layer_i.push_back(layers[i][k]);
        }
        new_layer_i.push_back(new_c);
        for (size_t k = layer_i_end_idx + 1; k < layers[i].size(); k++) {
          NC nc = layers[i][k];
          if (k == layer_i_end_idx + 1)
            nc.left_space = next_left_space_i;
          new_layer_i.push_back(nc);
        }

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
          if (k == idx + 1)
            nc.left_space = next_left_space_i_plus_1;
          new_layer_i_plus_1.push_back(nc);
        }

        std::vector<std::vector<NC>> saved_downward_layers;
        for (size_t k = i; k < layers.size(); k++) {
          saved_downward_layers.push_back(layers[k]);
        }

        layers[i] = std::move(new_layer_i);
        layers[i + 1] = std::move(new_layer_i_plus_1);

        bool downward_success = true;
        if (s != 0) {
          int end_layer =
              std::min((int)layers.size(), i + 1 + MAX_WINDOW_HEIGHT);
          std::span<std::vector<NC>> down_network(layers.data() + i + 1,
                                                  end_layer - (i + 1));
          std::vector<int> down_deltas(info_c.outputs.size(), s);

          std::vector<LayerStrand> down_strands2 = MakeWindowedStrandsDownward(
              library, down_network, in_chute_start, down_deltas.size());
          auto down_resolved = ResolveDisplacementDownwardStrand(
              library, down_strands2, in_chute_start, down_deltas,
              end_layer == (int)layers.size());
          if (down_resolved.has_value()) {
            ReintegrateWindowedStrands(library, down_network, down_strands2,
                                       down_resolved.value());
            downward_success = true;
          } else {
            downward_success = false;
          }
        }

        if (!downward_success) {
          for (int k = start_layer; k < i; k++) {
            layers[k] = std::move(saved_upward_layers[k - start_layer]);
          }
          for (size_t k = i; k < layers.size(); k++) {
            layers[k] = std::move(saved_downward_layers[k - i]);
          }
          continue;
        }

        placed = true;
        cells_moved++;
        break;
      }

      if (placed)
        return true;
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
    for (size_t i = 0; i < layers.size();) {
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
        layers_removed++;
        break;
      }

      MaybeStatus("str", i, layers.size());
    }

    if (changed) {
      improve_count++;
      return true;
    }

    return false;
  }

  StraightenResult StraightenIndividualWires(int i) {
    if (COUNT) ctr_straighten_indiv++;
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
        curr_x += library.GetWidth(layers[i][k].cell);
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
          int right_edge = new_x[k] + library.GetWidth(new_cells[k]);
          if (right_edge > new_x[k + 1]) {
            overlap = true;
            break;
          }
        }
        if (!overlap) {
          int start_layer = std::max(0, i - MAX_WINDOW_HEIGHT);
          std::span<std::vector<NC>> network(layers.data() + start_layer,
                                             i - start_layer);

          std::vector<LayerStrand> strands =
              MakeWindowedStrandsUpward(library, network, 0, all_deltas.size());
          auto resolved = ResolveDisplacementUpwardStrand(
              library, strands, 0, all_deltas, start_layer == 0);
          if (resolved.has_value()) {
            ReintegrateWindowedStrands(library, network, strands,
                                       resolved.value());
            std::vector<NC> new_layer_i;
            new_layer_i.reserve(layers[i].size());
            for (size_t k = 0; k < layers[i].size(); k++) {
              int left_space = 0;
              if (k == 0) {
                left_space = new_x[k];
              } else {
                int prev_right = new_x[k - 1] +
                                 library.GetWidth(new_cells[k - 1]);
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
            if (!CellLibrary::ValidWireSize(try_v))
              continue;

            std::vector<Cell> candidate_wires;
            candidate_wires.push_back(CellLibrary::Wire(
                try_v, CellLibrary::Bias::RIGHT, GetWireType(c.gate)));
            if (try_v < CellLibrary::SMALL_WIRE) {
              candidate_wires.push_back(CellLibrary::Wire(
                  try_v, CellLibrary::Bias::LEFT, GetWireType(c.gate)));
            }

            for (Cell &straight_wire : candidate_wires) {
              if (target_disp < 0)
                straight_wire.flip = true;
              CellLibrary::Info straight_info = library.GetInfo(straight_wire);

              int new_c_x = out_x - straight_info.outputs[0].xblock;
              int new_in_x = new_c_x + straight_info.inputs[0].xblock;
              int delta = new_in_x - in_x;

              int left_limit = -1000000000;
              if (k > 0) {
                left_limit = x_coords[k - 1] +
                             library.GetWidth(layers[i][k - 1].cell);
              }

              int right_limit = 1000000000;
              if (k + 1 < layers[i].size()) {
                right_limit = x_coords[k + 1];
              }

              if (new_c_x >= left_limit &&
                  new_c_x + straight_info.block_width <= right_limit) {
                int start_layer = std::max(0, i - MAX_WINDOW_HEIGHT);
                std::span<std::vector<NC>> network(layers.data() + start_layer,
                                                   i - start_layer);

                std::vector<int> deltas = {delta};

                std::vector<LayerStrand> strands = MakeWindowedStrandsUpward(
                    library, network, current_in_chute, deltas.size());
                auto resolved = ResolveDisplacementUpwardStrand(
                    library, strands, current_in_chute, deltas,
                    start_layer == 0);
                if (resolved.has_value()) {
                  ReintegrateWindowedStrands(library, network, strands,
                                             resolved.value());
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
            if (found_better)
              break;
          }
          if (found_better)
            break;
        }

        current_in_chute += info.inputs.size();
      }
    } while (local_changed);

    return changed ? StraightenResult::MODIFIED : StraightenResult::NOTHING;
  }

  StraightenResult StraightenWires(int i) {
    // We cannot delete the last remaining layer of the circuit.
    if (layers.size() <= 1) {
      return StraightenIndividualWires(i);
    }

    // Only attempt this on layers that are already all wires.
    for (const NC &nc : layers[i]) {
      if (!IsWire(nc.cell.gate)) {
        return StraightenIndividualWires(i);
      }
    }

    std::vector<int> deltas;
    int curr_x = 0;

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

      curr_x += info.block_width;
    }

    bool all_zero = true;
    for (int d : deltas) {
      if (d != 0) {
        all_zero = false;
        break;
      }
    }

    if (all_zero) {
      layers.erase(layers.begin() + i);
      return StraightenResult::DELETED;
    }

    int start_layer = std::max(0, i - MAX_WINDOW_HEIGHT);
    std::span<std::vector<NC>> network(layers.data() + start_layer,
                                       i - start_layer);

    std::vector<LayerStrand> strands =
        MakeWindowedStrandsUpward(library, network, 0, deltas.size());
    auto resolved = ResolveDisplacementUpwardStrand(library, strands, 0, deltas,
                                                    start_layer == 0);
    if (!resolved.has_value()) {
      return StraightenIndividualWires(i);
    }
    ReintegrateWindowedStrands(library, network, strands, resolved.value());

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

    for (size_t i = 0; i < layers.size(); ++i) {
      // Leave at least one layer if the entire circuit is passthrough.
      if (IsPassthrough(layers[i]) &&
          (trimmed.size() > 0 || i + 1 < layers.size())) {
        improve_count++;
      } else {
        trimmed.emplace_back(std::move(layers[i]));
      }
    }

    layers = std::move(trimmed);
  }

  void MaybeStatus(std::string_view pass, int64_t numer, int64_t denom) {
    if (status != nullptr && status_per.ShouldRun()) {
      status->Status("{} it "
                     "[{}/{} " ABLUE("{}") " +{}] | " AGREEN(
                         "-{} ≣") ", " APURPLE("{} ⍏") " | {}\n",
                     run_iter, numer, denom, pass, improve_count,
                     layers_removed, cells_moved,
                     ANSI::Time(run_timer.Seconds()));
    }
  }  void Run() {
    do {
      run_iter++;
      if (VERBOSE > 0)
        Print("Run pass {}\n", run_iter);
      if (run_iter > 100) {
        if (VERBOSE > 0)
          Print("Bailing out of Run loop!\n");
        break;
      }
      // Incremented whenever we make definite progress.
      improve_count = 0;

      // Keep moving gates up while it's possible to do so.
      MoveCellsUpPass();

      if (SELF_CHECK) {
        Layout l = Get();
        std::optional<std::string> err =
          DRC::GetLayoutError(library, "MCUP", l);
        CHECK(!err.has_value()) << "Failed on run_iter " << run_iter <<
          " after MoveCellsUpPass:\n" << err.value();
      }

      // Try to straighten wires to reduce the overall absolute
      // displacement. If it creates a layer that is only straight
      // wires, it removes that layer.
      StraightenWiresPass();

      if (SELF_CHECK) {
        Layout l = Get();
        std::optional<std::string> err =
          DRC::GetLayoutError(library, "SWP", l);
        if (err.has_value()) {
          if (CircuitSize(l.circuit) < 128) {
            Print("Layout:\n{}\n\n", LayoutEngine::ToString(l));
          }
          LOG(FATAL) << "Failed on run_iter " << run_iter <<
            "after StraightenWiresPass:\n" << err.value();
        }
      }

    } while (improve_count > 0);

    CompactHorizontal();

    if (SELF_CHECK) {
      Layout l = Get();
      DRC::CheckLayout(library, "After CompactHorizontal", l);
    }
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
        curr_x += library.GetWidth(nc.cell);
      }
    }

    std::vector<Layer> ret_layers;
    ret_layers.reserve(layers.size());
    for (const std::vector<NC> &nlayer : layers) {
      ret_layers.push_back(DenormalizeLayer(nlayer, -min_x));
    }

    return Layout{
        .input_vars = input_vars,
        .circuit = Circuit{.layers = std::move(ret_layers)},
    };
  }
};

Layout Optimization::Optimize(const CellLibrary &library, const Layout &layout,
                              StatusBar *status) {

#define RESET(ctr) (ctr).Reset()
  RESET(ctr_beamshiftdown);
  RESET(ctr_beamshiftup);
  RESET(ctr_resolve_up);
  RESET(ctr_resolve_down);
  RESET(ctr_move_up);
  RESET(ctr_straighten_indiv);
  RESET(ctr_dp_cells_up);
  RESET(ctr_dp_cells_down);
#undef RESET

  Optimizer optimizer(library, layout, status);
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

// Checks if a specific displacement can be resolved upward through
// the strand. If successful, returns a valid configuration of the
// strand cells for each layer. Returns std::nullopt if no valid
// configuration is found.
//
// Efficiency note: This function uses dynamic programming. Even though
// there could be combinatorially many valid configurations across the
// layer, the state space is not exponential. For each cell, we only
// generate a small, bounded number of candidate placements (e.g. rigid
// shifts or wire variants). Because a cell's validity only depends on
// not overlapping the right edge of the preceding cell, we only need
// to retain the minimum-cost valid configuration for each candidate
// placement. This collapses the state space, making the DP scale
// linearly with the number of cells in the strand.
static std::optional<std::vector<std::vector<NC>>>
ResolveDisplacementUpwardStrand(
    const CellLibrary &library,
    std::span<const LayerStrand> strand_network,
    int start_chute_relative,
    std::span<const int> deltas,
    bool top_can_move,
    bool build_result) {

  if (COUNT) ctr_resolve_up++;

  if (strand_network.empty())
    return std::vector<std::vector<NC>>();

  std::vector<std::vector<NC>> new_network;
  if (build_result) new_network.resize(strand_network.size());

  int current_deltas_start = start_chute_relative;
  std::vector<int> current_deltas(deltas.begin(), deltas.end());

  static constexpr int max_wire =
      *std::max_element(CellLibrary::WIRE_SIZES.begin(),
                        CellLibrary::WIRE_SIZES.end());

  for (int layer_idx = (int)strand_network.size() - 1; layer_idx >= 0;
       layer_idx--) {
    const LayerStrand &strand = strand_network[layer_idx];

    int num_cells = strand.cells.size();
    if (num_cells == 0) {
      if (build_result) new_network[layer_idx] = {};
      continue;
    }

    // DP state table to find valid placements.
    struct Config {
      Cell cell = Cell(Gate::CONST0);
      int x_pos = 0;      // Absolute X coordinate.
      int right_edge = 0; // Absolute X coordinate of the right edge.
      // Displacements required from the inputs feeding this cell.
      int in_delta = 0;
      // Index of the best valid config for the previous (leftwards) cell.
      int prev_config_idx = -1;
      int64_t total_cost = 0;
    };

    std::vector<std::vector<Config>> dp(num_cells);
    int out_chute_idx = 0;
    int curr_orig_x = strand.start_x;

    for (int i = 0; i < num_cells; ++i) {
      if (i > 0) curr_orig_x += strand.cells[i].left_space;
      const Cell &old_cell = strand.cells[i].cell;
      CellLibrary::Info old_info = library.GetInfo(old_cell);
      int num_outputs = old_info.outputs.size();

      bool equal = true;
      int first_d = 0;
      for (int k = 0; k < num_outputs; ++k) {
        int global_idx = strand.out_chute_start + out_chute_idx + k;
        int d = 0;
        if (global_idx >= current_deltas_start &&
            global_idx < current_deltas_start + (int)current_deltas.size()) {
          d = current_deltas[global_idx - current_deltas_start];
        }
        if (k == 0) {
          first_d = d;
        } else if (d != first_d) {
          equal = false;
        }
      }

      auto AddConfig = [&](const Cell &w, int x_pos,
                           int in_delta, int64_t cost) {
        if (!top_can_move && std::abs(in_delta) > max_wire * layer_idx)
          return;
        Config cfg;
        cfg.cell = w;
        cfg.x_pos = x_pos;
        cfg.right_edge = x_pos + library.GetWidth(w);
        cfg.in_delta = in_delta;
        cfg.total_cost = cost;
        if (i == 0 && cfg.x_pos < strand.left_obstacle_x)
          return;
        if (i == num_cells - 1 && cfg.right_edge > strand.right_obstacle_x)
          return;
        dp[i].push_back(cfg);
      };

      if (!IsWire(old_cell.gate)) {
        if (equal) {
          int x_pos = curr_orig_x + first_d;
          AddConfig(old_cell, x_pos, first_d, 0);
        }
      } else {
        int d = first_d;
        int req_out_x = curr_orig_x + old_info.outputs[0].xblock + d;
        int old_in_x = curr_orig_x + old_info.inputs[0].xblock;

        CType type = GetWireType(old_cell.gate);
        int target_disp = req_out_x - old_in_x;
        int abs_disp = std::abs(target_disp);

        std::vector<Cell> candidate_wires;
        if (CellLibrary::ValidWireSize(abs_disp)) {
          bool flip = (target_disp < 0);
          Cell w = CellLibrary::Wire(abs_disp, CellLibrary::Bias::RIGHT, type);
          w.flip = flip;
          candidate_wires.push_back(w);
          if (abs_disp < CellLibrary::SMALL_WIRE) {
            Cell w2 =
                CellLibrary::Wire(abs_disp, CellLibrary::Bias::LEFT, type);
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

        bool has_old = false;
        for (const Cell &w : candidate_wires) {
          if (w == old_cell)
            has_old = true;
        }
        if (!has_old)
          candidate_wires.push_back(old_cell);

        for (const Cell &w : candidate_wires) {
          CellLibrary::Info w_info = library.GetInfo(w);
          int x_pos = req_out_x - w_info.outputs[0].xblock;
          int in_x = x_pos + w_info.inputs[0].xblock;
          int in_delta = in_x - old_in_x;
          AddConfig(w, x_pos, in_delta, std::abs(in_delta));
        }
      }

      out_chute_idx += num_outputs;
      curr_orig_x += old_info.block_width;

      if (COUNT) ctr_dp_cells_up += dp[i].size();

      if (dp[i].empty())
        return std::nullopt;

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
        if (dp[i].empty())
          return std::nullopt;
      }
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

    if (best_last_idx == -1)
      return std::nullopt;

    std::vector<Config> selected(num_cells);
    int curr_idx = best_last_idx;
    for (int i = num_cells - 1; i >= 0; --i) {
      selected[i] = dp[i][curr_idx];
      curr_idx = selected[i].prev_config_idx;
    }

    std::vector<NC> new_layer;
    if (build_result) new_layer.reserve(num_cells);
    std::vector<int> next_deltas;
    int current_x = 0;
    for (int i = 0; i < num_cells; ++i) {
      const Config &cfg = selected[i];
      if (build_result) {
        int space = cfg.x_pos - current_x;
        new_layer.push_back(NC{
            .left_space = space,
            .cell = cfg.cell,
        });
      }
      current_x = cfg.right_edge;

      int num_inputs = library.GetInfo(cfg.cell).inputs.size();
      for (int k = 0; k < num_inputs; ++k) {
        next_deltas.push_back(cfg.in_delta);
      }
    }

    if (build_result) new_network[layer_idx] = std::move(new_layer);
    current_deltas = std::move(next_deltas);
    current_deltas_start = strand.in_chute_start;
  }

  if (!top_can_move) {
    for (int d : current_deltas) {
      if (d != 0)
        return std::nullopt;
    }
  }

  return new_network;
}

// Attempts to resolve displacement downward.
// Similar to the upward variant, this DP is highly efficient (linear time).
// The state at each cell only tracks the best valid placement of the cell
// immediately to its left, preventing an exponential blowup in the search.
static std::optional<std::vector<std::vector<NC>>>
ResolveDisplacementDownwardStrand(const CellLibrary &library,
                                  std::span<const LayerStrand> strand_network,
                                  int start_chute_relative,
                                  std::span<const int> deltas,
                                  bool bottom_can_move,
                                  bool build_result) {

  if (COUNT) ctr_resolve_down++;

  if (strand_network.empty())
    return std::vector<std::vector<NC>>();

  std::vector<std::vector<NC>> new_network;
  if (build_result) new_network.resize(strand_network.size());

  int current_deltas_start = start_chute_relative;
  std::vector<int> current_deltas(deltas.begin(), deltas.end());

  static constexpr int max_wire =
      *std::max_element(CellLibrary::WIRE_SIZES.begin(),
                        CellLibrary::WIRE_SIZES.end());

  for (int layer_idx = 0; layer_idx < (int)strand_network.size(); ++layer_idx) {
    const LayerStrand &strand = strand_network[layer_idx];

    int num_cells = strand.cells.size();
    if (num_cells == 0) {
      if (build_result) new_network[layer_idx] = {};
      continue;
    }

    struct Config {
      Cell cell = Cell(Gate::CONST0);
      int x_pos = 0;
      int right_edge = 0;
      int out_delta = 0;
      int prev_config_idx = -1;
      int64_t total_cost = 0;
    };

    std::vector<std::vector<Config>> dp(num_cells);
    int in_chute_idx = 0;
    int curr_orig_x = strand.start_x;

    for (int i = 0; i < num_cells; ++i) {
      if (i > 0) curr_orig_x += strand.cells[i].left_space;
      const Cell &old_cell = strand.cells[i].cell;
      CellLibrary::Info old_info = library.GetInfo(old_cell);
      int num_inputs = old_info.inputs.size();

      bool equal = true;
      int first_d = 0;
      for (int k = 0; k < num_inputs; ++k) {
        int global_idx = strand.in_chute_start + in_chute_idx + k;
        int d = 0;
        if (global_idx >= current_deltas_start &&
            global_idx < current_deltas_start + (int)current_deltas.size()) {
          d = current_deltas[global_idx - current_deltas_start];
        }
        if (k == 0) {
          first_d = d;
        } else if (d != first_d) {
          equal = false;
        }
      }

      auto AddConfig = [&](const Cell &w, int x_pos,
                           int out_delta, int64_t cost) {
        if (!bottom_can_move) {
          int layers_left = (int)strand_network.size() - 1 - layer_idx;
          if (std::abs(out_delta) > max_wire * layers_left)
            return;
        }
        Config cfg;
        cfg.cell = w;
        cfg.x_pos = x_pos;
        cfg.right_edge = x_pos + library.GetWidth(w);
        cfg.out_delta = out_delta;
        cfg.total_cost = cost;
        if (i == 0 && cfg.x_pos < strand.left_obstacle_x)
          return;
        if (i == num_cells - 1 && cfg.right_edge > strand.right_obstacle_x)
          return;
        dp[i].push_back(cfg);
      };

      if (!IsWire(old_cell.gate)) {
        if (equal) {
          int x_pos = curr_orig_x + first_d;
          AddConfig(old_cell, x_pos, first_d, 0);
        }
      } else {
        int d = first_d;
        int req_in_x = curr_orig_x + old_info.inputs[0].xblock + d;
        int old_out_x = curr_orig_x + old_info.outputs[0].xblock;

        CType type = GetWireType(old_cell.gate);
        int target_disp = old_out_x - req_in_x;
        int abs_disp = std::abs(target_disp);

        std::vector<Cell> candidate_wires;
        if (CellLibrary::ValidWireSize(abs_disp)) {
          bool flip = (target_disp < 0);
          Cell w = CellLibrary::Wire(abs_disp, CellLibrary::Bias::RIGHT, type);
          w.flip = flip;
          candidate_wires.push_back(w);
          if (abs_disp < CellLibrary::SMALL_WIRE) {
            Cell w2 =
                CellLibrary::Wire(abs_disp, CellLibrary::Bias::LEFT, type);
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

        bool has_old = false;
        for (const Cell &w : candidate_wires) {
          if (w == old_cell)
            has_old = true;
        }
        if (!has_old)
          candidate_wires.push_back(old_cell);

        for (const Cell &w : candidate_wires) {
          CellLibrary::Info w_info = library.GetInfo(w);
          int x_pos = req_in_x - w_info.inputs[0].xblock;
          int out_x = x_pos + w_info.outputs[0].xblock;
          int out_delta = out_x - old_out_x;
          AddConfig(w, x_pos, out_delta, std::abs(out_delta));
        }
      }

      in_chute_idx += num_inputs;
      curr_orig_x += old_info.block_width;

      if (COUNT) ctr_dp_cells_down += dp[i].size();

      if (dp[i].empty())
        return std::nullopt;

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
        if (dp[i].empty())
          return std::nullopt;
      }
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

    if (best_last_idx == -1)
      return std::nullopt;

    std::vector<Config> selected(num_cells);
    int curr_idx = best_last_idx;
    for (int i = num_cells - 1; i >= 0; --i) {
      selected[i] = dp[i][curr_idx];
      curr_idx = selected[i].prev_config_idx;
    }

    std::vector<NC> new_layer;
    if (build_result) new_layer.reserve(num_cells);
    std::vector<int> next_deltas;
    int current_x = 0;
    for (int i = 0; i < num_cells; ++i) {
      const Config &cfg = selected[i];
      if (build_result) {
        int space = cfg.x_pos - current_x;
        new_layer.push_back(NC{
            .left_space = space,
            .cell = cfg.cell,
        });
      }
      current_x = cfg.right_edge;

      int num_outputs = library.GetInfo(cfg.cell).outputs.size();
      for (int k = 0; k < num_outputs; ++k) {
        next_deltas.push_back(cfg.out_delta);
      }
    }

    if (build_result) new_network[layer_idx] = std::move(new_layer);
    current_deltas = std::move(next_deltas);
    current_deltas_start = strand.out_chute_start;
  }

  if (!bottom_can_move) {
    for (int d : current_deltas) {
      if (d != 0)
        return std::nullopt;
    }
  }

  return new_network;
}

// Evaluates a beam of chutes across a range of rigid horizontal shifts,
// returning the subset of shifts that result in a valid upward configuration.
// Although it iterates over every candidate shift in the set, it is efficient
// in practice because the input `shifts` set is tightly clamped, and the
// underlying DP (ResolveDisplacementUpwardStrand) scales linearly.
static DenseIntSet ResolveBeamShiftUpwardStrand(
    const CellLibrary &library, std::span<const LayerStrand> strand_network,
    int start_chute, std::span<const int> deltas, int min_shift,
    const DenseIntSet &shifts, bool top_can_move) {

  DenseIntSet valid_shifts(shifts.Radix());
  static constexpr int max_wire =
      *std::max_element(CellLibrary::WIRE_SIZES.begin(),
                        CellLibrary::WIRE_SIZES.end());
  int max_start_delta = max_wire * (int)strand_network.size();

  for (int bit : shifts) {
    if (COUNT) ctr_beamshiftup++;
    int s = min_shift + bit;

    bool possible = true;
    if (!top_can_move) {
      for (int d : deltas) {
        if (std::abs(d + s) > max_start_delta) {
          possible = false;
          break;
        }
      }
    }
    if (!possible) continue;

    std::vector<int> current_deltas(deltas.begin(), deltas.end());
    for (int &d : current_deltas) {
      d += s;
    }

    auto resolved =
        ResolveDisplacementUpwardStrand(library, strand_network, start_chute,
                                        current_deltas, top_can_move, false);

    if (resolved.has_value()) {
      valid_shifts.Add(bit);
    }
  }

  return valid_shifts;
}

static DenseIntSet ResolveBeamShiftDownwardStrand(
    const CellLibrary &library, std::span<const LayerStrand> strand_network,
    int start_chute, std::span<const int> deltas, int min_shift,
    const DenseIntSet &shifts, bool bottom_can_move) {

  DenseIntSet valid_shifts(shifts.Radix());
  static constexpr int max_wire =
      *std::max_element(CellLibrary::WIRE_SIZES.begin(),
                        CellLibrary::WIRE_SIZES.end());
  int max_start_delta = max_wire * (int)strand_network.size();

  for (int bit : shifts) {
    if (COUNT) {
      ctr_beamshiftdown++;
    }
    int s = min_shift + bit;

    bool possible = true;
    if (!bottom_can_move) {
      for (int d : deltas) {
        if (std::abs(d + s) > max_start_delta) {
          possible = false;
          break;
        }
      }
    }
    if (!possible) continue;

    std::vector<int> current_deltas(deltas.begin(), deltas.end());
    for (int &d : current_deltas) {
      d += s;
    }

    auto resolved = ResolveDisplacementDownwardStrand(
        library, strand_network, start_chute, current_deltas, bottom_can_move,
        false);

    if (resolved.has_value()) {
      valid_shifts.Add(bit);
    }
  }

  return valid_shifts;
}

DenseIntSet Optimization::ResolveBeamShiftUpward(
    const CellLibrary &library, std::span<Layer> network, int start_chute,
    std::span<const int> deltas, int min_shift, const DenseIntSet &shifts) {
  int num_layers = network.size();
  int start_layer = std::max(0, num_layers - MAX_WINDOW_HEIGHT);
  std::span<Layer> sub_network = network.subspan(start_layer);

  std::vector<std::vector<NC>> nc_network;
  nc_network.reserve(sub_network.size());
  for (const Layer &layer : sub_network) {
    nc_network.push_back(NormalizeLayer(layer));
  }

  std::vector<LayerStrand> strands = MakeWindowedStrandsUpward(
      library, nc_network, start_chute, deltas.size());
  return ResolveBeamShiftUpwardStrand(library, strands, start_chute, deltas,
                                      min_shift, shifts, start_layer == 0);
}

// OK for the top-layer inputs (i.e., for the entire circuit) to move.
bool Optimization::ResolveDisplacementUpward(const CellLibrary &library,
                                             std::span<Layer> network,
                                             int start_chute,
                                             std::span<const int> deltas) {
  int num_layers = network.size();
  int start_layer = std::max(0, num_layers - MAX_WINDOW_HEIGHT);
  std::span<Layer> sub_network = network.subspan(start_layer);

  std::vector<std::vector<NC>> nc_network;
  nc_network.reserve(sub_network.size());
  for (const Layer &layer : sub_network) {
    nc_network.push_back(NormalizeLayer(layer));
  }

  std::vector<LayerStrand> strands = MakeWindowedStrandsUpward(
      library, nc_network, start_chute, deltas.size());
  auto resolved = ResolveDisplacementUpwardStrand(library, strands, start_chute,
                                                  deltas, start_layer == 0);

  bool success = resolved.has_value();
  if (success) {
    ReintegrateWindowedStrands(library, nc_network, strands, resolved.value());
    int min_x = 0;
    for (const std::vector<NC> &nlayer : nc_network) {
      int curr_x = 0;
      for (const NC &nc : nlayer) {
        curr_x += nc.left_space;
        if (curr_x < min_x) {
          min_x = curr_x;
        }
        curr_x += library.GetWidth(nc.cell);
      }
    }
    for (size_t i = 0; i < sub_network.size(); ++i) {
      sub_network[i] = DenormalizeLayer(nc_network[i], -min_x);
    }
    if (min_x < 0) {
      for (size_t i = 0; i < (size_t)start_layer; ++i) {
        if (!network[i].empty()) {
          if (network[i][0].gate == Gate::SPACER) {
            network[i][0].v += -min_x;
          } else {
            network[i].insert(network[i].begin(), Cell(Gate::SPACER, -min_x));
          }
        }
      }
    }
  }

  return success;
}

// OK for the bottom-layer outputs to move.
bool Optimization::ResolveDisplacementDownward(const CellLibrary &library,
                                               std::span<Layer> network,
                                               int start_chute,
                                               std::span<const int> deltas) {
  int num_layers = network.size();
  int end_layer = std::min(num_layers, MAX_WINDOW_HEIGHT);
  std::span<Layer> sub_network = network.subspan(0, end_layer);

  std::vector<std::vector<NC>> nc_network;
  nc_network.reserve(sub_network.size());
  for (const Layer &layer : sub_network) {
    nc_network.push_back(NormalizeLayer(layer));
  }

  std::vector<LayerStrand> strands = MakeWindowedStrandsDownward(
      library, nc_network, start_chute, deltas.size());
  auto resolved = ResolveDisplacementDownwardStrand(
      library, strands, start_chute, deltas, end_layer == num_layers);

  bool success = resolved.has_value();
  if (success) {
    ReintegrateWindowedStrands(library, nc_network, strands, resolved.value());
    int min_x = 0;
    for (const std::vector<NC> &nlayer : nc_network) {
      int curr_x = 0;
      for (const NC &nc : nlayer) {
        curr_x += nc.left_space;
        if (curr_x < min_x) {
          min_x = curr_x;
        }
        curr_x += library.GetWidth(nc.cell);
      }
    }
    for (size_t i = 0; i < sub_network.size(); ++i) {
      sub_network[i] = DenormalizeLayer(nc_network[i], -min_x);
    }
    if (min_x < 0) {
      for (size_t i = end_layer; i < network.size(); ++i) {
        if (!network[i].empty()) {
          if (network[i][0].gate == Gate::SPACER) {
            network[i][0].v += -min_x;
          } else {
            network[i].insert(network[i].begin(), Cell(Gate::SPACER, -min_x));
          }
        }
      }
    }
  }

  return success;
}


std::string Optimization::DebugCounters() {
  std::string ret = "Counters:\n";
  #define OUT(ctr) AppendFormat(&ret, #ctr ": {}\n", (ctr).Read())

  OUT(ctr_beamshiftdown);
  OUT(ctr_beamshiftup);
  OUT(ctr_resolve_up);
  OUT(ctr_resolve_down);
  OUT(ctr_move_up);
  OUT(ctr_straighten_indiv);
  OUT(ctr_dp_cells_up);
  OUT(ctr_dp_cells_down);

  #undef OUT

  return ret;
}
