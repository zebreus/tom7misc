#include "drc.h"

#include <algorithm>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cell-library.h"
#include "circuit.h"
#include "layout.h"
#include "level.h"
#include "prop.h"

using IO = CellLibrary::IO;

void DRC::CheckCircuit(const CellLibrary &library,
                       std::string_view error_context,
                       const Circuit &circuit) {
  struct DrcChute {
    int xpos;
    CType type;
    Cell cell;
  };

  std::vector<DrcChute> prev_outputs;

  for (size_t i = 0; i < circuit.layers.size(); i++) {
    const Layer &layer = circuit.layers[i];
    int current_x = 0;
    std::vector<DrcChute> current_inputs;
    std::vector<DrcChute> current_outputs;

    for (const Cell &cell : layer) {
      if (IsWire(cell.gate) && cell.v >= CellLibrary::SMALL_WIRE) {
        CHECK(cell.gate == Gate::WIREA || cell.gate == Gate::WIRE0A ||
              cell.gate == Gate::WIRE1A)
            << error_context << ": Large wire must use A shape in "
            << CellString(cell);
      }

      CellLibrary::Info info = library.GetInfo(cell);

      for (const IO &in : info.inputs) {
        CHECK(in.xblock >= 0 &&
              in.xblock + Levels::IN_WIDTH <= info.block_width)
            << error_context << ": Input out of bounds in "
            << CellString(cell);
        current_inputs.push_back({current_x + in.xblock, in.type, cell});
      }
      for (const IO &out : info.outputs) {
        CHECK(out.xblock >= 0 &&
              out.xblock + Levels::OUT_WIDTH <= info.block_width)
            << error_context << ": Output out of bounds in "
            << CellString(cell);
        current_outputs.push_back({current_x + out.xblock, out.type, cell});
      }

      current_x += info.block_width;
    }

    for (size_t j = 1; j < current_inputs.size(); j++) {
      CHECK(current_inputs[j - 1].xpos + Levels::IN_WIDTH <=
            current_inputs[j].xpos)
          << error_context << ": Overlapping or out-of-order inputs in layer "
          << i;
    }
    for (size_t j = 1; j < current_outputs.size(); j++) {
      CHECK(current_outputs[j - 1].xpos + Levels::OUT_WIDTH <=
            current_outputs[j].xpos)
          << error_context << ": Overlapping or out-of-order outputs in layer "
          << i;
    }

    if (i > 0) {
      size_t min_size = std::min(prev_outputs.size(), current_inputs.size());
      for (size_t j = 0; j < min_size; j++) {
        CHECK(prev_outputs[j].xpos == current_inputs[j].xpos)
            << error_context << ": Layer " << i << " input " << j
            << " xpos mismatch: expected " << prev_outputs[j].xpos << ", got "
            << current_inputs[j].xpos << " (between "
            << CellString(prev_outputs[j].cell) << " and "
            << CellString(current_inputs[j].cell) << ")";
        CHECK(prev_outputs[j].type == current_inputs[j].type)
            << error_context << ": Layer " << i << " input " << j
            << " type mismatch: expected " << TypeString(prev_outputs[j].type)
            << ", got " << TypeString(current_inputs[j].type) << " (between "
            << CellString(prev_outputs[j].cell) << " and "
            << CellString(current_inputs[j].cell) << ")";
      }

      if (prev_outputs.size() != current_inputs.size()) {
        std::string extra;
        if (prev_outputs.size() > min_size) {
          extra = std::format(" (extra output from {})",
                              CellString(prev_outputs[min_size].cell));
        } else {
          extra = std::format(" (extra input to {})",
                              CellString(current_inputs[min_size].cell));
        }
        CHECK(prev_outputs.size() == current_inputs.size())
            << error_context << ": Layer " << i
            << " input count mismatch: expected " << prev_outputs.size()
            << ", got " << current_inputs.size() << extra;
      }
    }

    prev_outputs = std::move(current_outputs);
  }
}

void DRC::CheckLayout(const CellLibrary &library,
                      std::string_view error_context,
                      const Layout &layout) {

  std::vector<CType> first_layer_inputs;
  if (!layout.circuit.layers.empty()) {
    for (const Cell &cell : layout.circuit.layers.front()) {
      CellLibrary::Info info = library.GetInfo(cell);
      for (const IO &in : info.inputs) {
        first_layer_inputs.push_back(in.type);
      }
    }
  }

  CHECK(layout.input_vars.size() == first_layer_inputs.size())
      << error_context << ": input count mismatch: layout has "
      << layout.input_vars.size() << " vars, but first layer has "
      << first_layer_inputs.size() << " inputs";

  for (size_t i = 0; i < layout.input_vars.size(); i++) {
    CHECK(layout.input_vars[i].second == first_layer_inputs[i])
        << error_context << ": input var " << i << " type mismatch: expected "
        << TypeString(layout.input_vars[i].second) << ", got "
        << TypeString(first_layer_inputs[i]);
  }

  CheckCircuit(library, error_context, layout.circuit);
}

void DRC::AssertEquivalentLayout(const CellLibrary &library,
                                 std::string_view error_context,
                                 const Layout &a, const Layout &b) {
  CheckCircuit(library, error_context, a.circuit);
  CheckCircuit(library, error_context, b.circuit);

  CHECK(a.input_vars.size() == b.input_vars.size())
      << error_context << ": input var count mismatch";

  for (size_t i = 0; i < a.input_vars.size(); i++) {
    CHECK(a.input_vars[i].first == b.input_vars[i].first)
        << error_context << ": input var " << i << " mismatch";
    CHECK(a.input_vars[i].second == b.input_vars[i].second)
        << error_context << ": input var " << i << " type mismatch";
  }

  std::vector<Func> funcs_a;
  funcs_a.reserve(a.input_vars.size());
  for (const auto &iv : a.input_vars) {
    funcs_a.push_back(
        Func{.prop = {.p = Var{.id = iv.first}}, .type = iv.second});
  }
  for (const Layer &layer : a.circuit.layers) {
    funcs_a = Transform(layer, funcs_a);
  }

  std::vector<Func> funcs_b;
  funcs_b.reserve(b.input_vars.size());
  for (const auto &iv : b.input_vars) {
    funcs_b.push_back(
        Func{.prop = {.p = Var{.id = iv.first}}, .type = iv.second});
  }
  for (const Layer &layer : b.circuit.layers) {
    funcs_b = Transform(layer, funcs_b);
  }

  CHECK(funcs_a.size() == funcs_b.size())
      << error_context << ": output count mismatch: " << funcs_a.size()
      << " vs " << funcs_b.size();

  for (size_t i = 0; i < funcs_a.size(); i++) {
    CHECK(funcs_a[i].type == funcs_b[i].type)
        << error_context << ": output " << i << " type mismatch";
    CHECK(funcs_a[i].prop == funcs_b[i].prop)
        << error_context << ": output " << i << " prop mismatch";
  }
}
