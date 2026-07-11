
#include "optimization.h"

#include <algorithm>
#include <span>
#include <cmath>
#include <vector>
#include <utility>
#include <memory>

#include "cell-library.h"
#include "circuit.h"
#include "layout.h"
#include "vector-util.h"

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

  bool IsPassthrough(const std::vector<NC> &layer) {
    for (const NC &nc : layer) {
      if (!IsWire(nc.cell.gate) || nc.cell.v != 0) {
        return false;
      }
    }
    return true;
  }

  // Attempt to straighten out wires so that we can remove layers. In
  // RemovePassthroughLayers we remove only layers that have wires
  // (and spacers) exclusively. Therefore the goal is to put
  // displacement-0 wires on wire-only layers. All that matters about
  // a connected (vertical) series of wires is their net displacement,
  // so we can shift displacement into adjacent layers to accomplish
  // this. The wire cells have to fit, but we can choose variants (wire
  // A/B) and flip at will. It's also acceptable to move non-wire cells,
  // but we shouldn't change anything else about those.
  struct CellState {
    Cell cell;
    int cell_x;
  };

  // Identifies a specific input or output port on a cell in the circuit.
  struct PortLoc {
    int l, c;
    // Port number within this cell's inputs or outputs.
    int port;
  };

  // Adjacency lists for the circuit's connections.
  // prev_port[l][c][p] gives the PortLoc of the output port connected to
  // input port 'p' of cell 'c' on layer 'l'.
  std::vector<std::vector<std::vector<PortLoc>>> prev_port;

  // next_port[l][c][p] gives the PortLoc of the input port connected to
  // output port 'p' of cell 'c' on layer 'l'.
  std::vector<std::vector<std::vector<PortLoc>>> next_port;

  // A pending shift request to propagate a shift to a connected cell.
  struct Req {
    PortLoc loc;
    // True if the shift propagates to this cell via its input port,
    // false if it propagates via its output port.
    bool is_input;
  };

  std::vector<Cell> GetWireCandidates(Gate orig, int delta) {
    std::vector<Cell> ret;
    int offset = std::abs(delta);
    if (!CellLibrary::ValidWireSize(offset)) return ret;
    bool flip = (delta < 0);

    CType type = CType::MIXED;
    if (orig == Gate::WIRE0A || orig == Gate::WIRE0B) type = CType::ZERO;
    if (orig == Gate::WIRE1A || orig == Gate::WIRE1B) type = CType::ONE;

    Cell wa = CellLibrary::WireA(offset, type);
    wa.flip = flip;
    ret.push_back(wa);

    Cell wb = CellLibrary::WireB(offset, type);
    wb.flip = flip;
    ret.push_back(wb);

    return ret;
  }

  // Recursively apply a shift s to a connected component of cells.
  // We use backtracking to find a valid assignment that avoids overlaps.
  // If we encounter a wire that can absorb the shift by changing its delta,
  // we can stop propagating the shift along that path.
  //
  // Parameters:
  //  - circuit: The grid of cell states to be modified.
  //  - shifted: Bitmask indicating cells that have already been shifted in
  //    this operation, to avoid infinite loops and duplicate shifts.
  //  - s: The horizontal shift distance.
  //  - reqs: Pending shift requests to propagate the shift to connected cells.
  //  - is_all_wire: Flags indicating which layers are completely made of
  //    wires. Used to determine if a wire can absorb the shift.
  //
  // Returns:
  //  true if a valid, non-overlapping assignment was found; false if the
  //  shift resulted in overlaps or invalid wires, prompting backtracking.
  bool TryApply(std::vector<std::vector<CellState>> &circuit,
                std::vector<std::vector<bool>> &shifted, int s,
                std::vector<Req> reqs, const std::vector<bool> &is_all_wire) {
    // Base case: all shift requests fulfilled. Check for overlaps.
    if (reqs.empty()) {
      for (size_t lidx = 0; lidx < circuit.size(); lidx++) {
        const std::vector<CellState> &layer = circuit[lidx];
        int prev_right = 0;
        for (size_t c = 0; c < layer.size(); c++) {
          if (layer[c].cell_x < prev_right)
            return false;
          prev_right = layer[c].cell_x +
            library.GetInfo(layer[c].cell).block_width;
        }
      }
      return true;
    }

    Req req = reqs.back();
    reqs.pop_back();

    int lidx = req.loc.l, c = req.loc.c;

    std::vector<CellState> &layer = circuit[lidx];
    std::vector<bool> &shifted_layer = shifted[lidx];

    if (shifted_layer[c]) {
      return TryApply(circuit, shifted, s, reqs, is_all_wire);
    }

    if (IsWire(layer[c].cell.gate)) {
      if (!(is_all_wire[lidx] && layer[c].cell.v == 0)) {
        int old_in = layer[c].cell_x +
                     library.GetInfo(layer[c].cell).inputs[0].xblock;
        int old_out = layer[c].cell_x +
                      library.GetInfo(layer[c].cell).outputs[0].xblock;

        int req_in = old_in + (req.is_input ? s : 0);
        int req_out = old_out + (!req.is_input ? s : 0);
        int target_delta = req_out - req_in;

        std::vector<Cell> cands = GetWireCandidates(layer[c].cell.gate, target_delta);
        for (const Cell &cand : cands) {
          CellState backup = layer[c];
          layer[c].cell = cand;
          layer[c].cell_x = req_in - library.GetInfo(cand).inputs[0].xblock;

          bool old_shifted = shifted_layer[c];
          shifted_layer[c] = true;
          if (TryApply(circuit, shifted, s, reqs, is_all_wire)) return true;
          shifted_layer[c] = old_shifted;
          layer[c] = backup;
        }
      }
    }

    CellState backup = layer[c];
    layer[c].cell_x += s;
    shifted_layer[c] = true;

    std::vector<Req> next_reqs = reqs;

    int num_in = library.GetInfo(layer[c].cell).inputs.size();
    for (int i = 0; i < num_in; ++i) {
      if (req.is_input && i == req.loc.port) continue;
      auto prev = prev_port[lidx][c][i];
      if (prev.l != -1) next_reqs.push_back({prev, false});
    }

    int num_out = library.GetInfo(layer[c].cell).outputs.size();
    for (int i = 0; i < num_out; ++i) {
      if (!req.is_input && i == req.loc.port) continue;
      auto next = next_port[lidx][c][i];
      if (next.l != -1) next_reqs.push_back({next, true});
    }

    if (TryApply(circuit, shifted, s, next_reqs, is_all_wire)) return true;

    shifted_layer[c] = false;
    layer[c] = backup;

    return false;
  }

  // Try to straighten a wire cell to have 0 displacement.
  // We can fix its input in place and shift its output (and all downstream cells),
  // or fix its output and shift its input (and all upstream cells).
  bool TryFixWire(std::vector<std::vector<CellState>> &circuit,
                  int lidx, int c, const std::vector<bool> &is_all_wire) {
    std::vector<CellState> &layer = circuit[lidx];
    int old_in = layer[c].cell_x +
      library.GetInfo(layer[c].cell).inputs[0].xblock;
    int old_out = layer[c].cell_x +
      library.GetInfo(layer[c].cell).outputs[0].xblock;
    int old_delta = old_out - old_in;

    std::vector<Cell> cands = GetWireCandidates(layer[c].cell.gate, 0);

    // Strategy 1: Keep input fixed, shift output by -old_delta
    for (const Cell &cand : cands) {
      std::vector<std::vector<CellState>> newcircuit = circuit;
      std::vector<CellState> &copy_layer = newcircuit[lidx];
      copy_layer[c].cell = cand;
      copy_layer[c].cell_x = old_in - library.GetInfo(cand).inputs[0].xblock;

      int S = -old_delta;
      auto next = next_port[lidx][c][0];
      std::vector<std::vector<bool>> shifted(
          circuit.size(), std::vector<bool>(circuit[0].size(), false));
      shifted[lidx][c] = true;
      std::vector<Req> reqs;
      if (next.l != -1) reqs.push_back({next, true});

      if (TryApply(newcircuit, shifted, S, reqs, is_all_wire)) {
        circuit = std::move(newcircuit);
        return true;
      }
    }

    // Strategy 2: Keep output fixed, shift input by +old_delta
    for (const Cell &cand : cands) {
      std::vector<std::vector<CellState>> newcircuit = circuit;
      std::vector<CellState> &copy_layer = newcircuit[lidx];
      copy_layer[c].cell = cand;
      copy_layer[c].cell_x = old_out - library.GetInfo(cand).outputs[0].xblock;

      int S = old_delta;
      auto prev = prev_port[lidx][c][0];
      std::vector<std::vector<bool>> shifted(
          circuit.size(), std::vector<bool>(circuit[0].size(), false));
      shifted[lidx][c] = true;
      std::vector<Req> reqs;
      if (prev.l != -1) reqs.push_back({prev, false});

      if (TryApply(newcircuit, shifted, S, reqs, is_all_wire)) {
        circuit = std::move(newcircuit);
        return true;
      }
    }
    return false;
  }

  // Straightens out wires to enable layer removal.
  // We identify layers consisting entirely of wires, and try to change
  // those wires to have 0 displacement.
  void AlignLayers() {
    std::vector<std::vector<CellState>> circuit(layers.size());
    for (size_t lidx = 0; lidx < layers.size(); lidx++) {
      int x = 0;
      for (size_t c = 0; c < layers[lidx].size(); c++) {
        x += layers[lidx][c].left_space;
        circuit[lidx].push_back({layers[lidx][c].cell, x});
        x += library.GetInfo(layers[lidx][c].cell).block_width;
      }
    }

    prev_port.assign(circuit.size(), {});
    next_port.assign(circuit.size(), {});
    for (size_t lidx = 0; lidx < circuit.size(); lidx++) {
      const std::vector<CellState> &layer = circuit[lidx];
      prev_port[lidx].resize(layer.size());
      next_port[lidx].resize(layer.size());
      for (size_t c = 0; c < layer.size(); c++) {
        prev_port[lidx][c].assign(library.GetInfo(layer[c].cell).inputs.size(),
                               {-1, -1, -1});
        next_port[lidx][c].assign(library.GetInfo(layer[c].cell).outputs.size(),
                               {-1, -1, -1});
      }
    }

    for (size_t lidx = 0; lidx + 1 < circuit.size(); lidx++) {
      const std::vector<CellState> &layer = circuit[lidx];
      std::vector<std::pair<int, int>> outs;
      for (size_t c = 0; c < layer.size(); c++) {
        for (size_t p = 0;
             p < library.GetInfo(layer[c].cell).outputs.size();
             p++) {
          outs.push_back({(int)c, (int)p});
        }
      }

      const std::vector<CellState> &next_layer = circuit[lidx + 1];
      std::vector<std::pair<int, int>> ins;
      for (size_t c = 0; c < next_layer.size(); c++) {
        for (size_t p = 0;
             p < library.GetInfo(next_layer[c].cell).inputs.size();
             p++) {
          ins.push_back({(int)c, (int)p});
        }
      }

      size_t n = std::min(outs.size(), ins.size());
      for (size_t i = 0; i < n; ++i) {
        next_port[lidx][outs[i].first][outs[i].second] = {
            (int)lidx + 1, ins[i].first, ins[i].second};
        prev_port[lidx + 1][ins[i].first][ins[i].second] = {
            (int)lidx, outs[i].first, outs[i].second};
      }
    }

    bool changed = true;
    while (changed) {
      changed = false;
      std::vector<bool> is_all_wire(circuit.size(), true);
      for (size_t lidx = 0; lidx < circuit.size(); lidx++) {
        for (const auto &cs : circuit[lidx]) {
          if (!IsWire(cs.cell.gate)) {
            is_all_wire[lidx] = false;
            break;
          }
        }
      }

      for (size_t lidx = 0; lidx < circuit.size(); lidx++) {
        if (!is_all_wire[lidx]) continue;

        for (size_t c = 0; c < circuit[lidx].size(); c++) {
          if (circuit[lidx][c].cell.v != 0) {
            if (TryFixWire(circuit, lidx, c, is_all_wire)) {
              changed = true;
              break;
            }
          }
        }
        if (changed) break;
      }
    }

    for (size_t lidx = 0; lidx < circuit.size(); lidx++) {
      const std::vector<CellState> &layer = circuit[lidx];
      layers[lidx].clear();
      int x = 0;
      for (size_t c = 0; c < layer.size(); c++) {
        NC nc{
          .left_space = layer[c].cell_x - x,
          .cell = layer[c].cell,
        };
        layers[lidx].push_back(nc);
        x = layer[c].cell_x + library.GetInfo(nc.cell).block_width;
      }
    }
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

    do {
      // Incremented whenever we make definite progress.
      improve_count = 0;

      AlignLayers();
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


