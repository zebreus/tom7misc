
#include <algorithm>
#include <functional>
#include <memory>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "cell-library.h"
#include "circuit.h"
#include "drc.h"
#include "layout-reducer.h"
#include "layout.h"
#include "optimization.h"
#include "prop.h"
#include "util.h"

static constexpr int VERBOSE = 0;

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
  int current = 0;
  for (int i = 0; i < layout.circuit.layers.size(); i++) {
    if (IsAllWires(layout.circuit.layers[i])) {
      current++;
      max_consec = std::max(max_consec, current);
    } else {
      current = 0;
    }
  }

  return max_consec;
}

bool Optimizable(const CellLibrary &library, const Layout &layout) {
  if (VERBOSE > 1) Print("Check if optimizable...\n");
  Layout optimized = Optimization::Optimize(library, layout);
  if (VERBOSE > 1) Print("Optimized returned.\n");

  if (optimized.circuit.layers.size() < layout.circuit.layers.size()) {
    return true;
  }

  return CircuitSize(optimized.circuit) < CircuitSize(layout.circuit);
}

static Layout MakeStart(const CellLibrary &library) {
  World world{.symbol_names = {"a", "b", "c"}};
  std::unique_ptr<LayoutEngine> le = LayoutEngine::Create(library, world);
  Prop a{Var{.id = 0}}, b{Var{.id = 1}}, c{Var{.id = 2}},
    d{Var{.id = 3}}, e{Var{.id = 4}}, f{Var{.id = 5}},
    g{Var{.id = 6}}, h{Var{.id = 7}}, i{Var{.id = 8}},
    j{Var{.id = 9}};

  // std::vector<Prop> output = {(a ^ (b & -c)) | (a & c)};
  std::vector<Prop> output = {
    (a & (b & c)) |
    ((b & (d | e)) & ((f | g) | ((h | i) | (c | j)))),
  };

  Layout layout = le->DoLayout(output);
  Print("Got Layout! {}", LayoutEngine::LayoutInfo(layout));
  if (VERBOSE > 1) {
    Print("Initial layout:\n{}\n", LayoutEngine::ToString(layout));
  }
  Print("Try optimizing:\n");
  layout = Optimization::Optimize(library, layout);
  Print("\n\nOptimized! {}\n", LayoutEngine::LayoutInfo(layout));
  if (VERBOSE > 1) {
    Print("Optimized layout:\n{}\n", LayoutEngine::ToString(layout));
  }
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

  std::unique_ptr<Reducer> reducer = Reducer::Create(library);

  Layout result = reducer->ReduceWhile(layout, 100, Pred);
  Print("Consecutive wire layers: {}\n", ConsecutiveWireLayers(result));

  Util::WriteFileBytes("reduced.layout", LayoutEngine::Serialize(result));
  Print("Reduced:\n{}\n", LayoutEngine::ToString(result));
}



int main(int argc, char **argv) {
  ANSI::Init();

  ThreeLayers();

  return 0;
}
