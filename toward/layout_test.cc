
#include "circuit.h"
#include "layout.h"

#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "cell-library.h"
#include "prop.h"

static void Verify(const Layout &layout, const std::vector<Prop> &props) {
  std::vector<Func> funcs;
  for (int v : layout.input_vars) {
    funcs.push_back(Func{.prop = Prop{Var{.id = v}}, .type = CType::MIXED});
  }
  for (const Layer &layer : layout.circuit.layers) {
    funcs = Transform(layer, funcs);
  }
  CHECK(funcs.size() == props.size());
  for (size_t i = 0; i < funcs.size(); i++) {
    CHECK(funcs[i].type == CType::MIXED);
    CHECK(PropEq(funcs[i].prop, props[i]));
  }
}

static void Empty(const CellLibrary &library) {
  World empty;
  std::vector<Prop> nothing;

  Layout trivial = DoLayout(library, empty, nothing);
  CHECK(trivial.input_vars.empty());
  // ... should be one empty layer in this case? ...
  Verify(trivial, nothing);
}

static void Consts(const CellLibrary &library) {
  Print("Consts...\n");
  World world;
  std::vector<Prop> output = {True(), False()};
  Layout layout = DoLayout(library, world, output);
  library.DRC(layout.circuit);
  Verify(layout, output);
}

static void SingleVar(const CellLibrary &library) {
  Print("Single Var...\n");
  World world{.symbol_names = {"a"}};
  Prop a{Var{.id = 0}};

  std::vector<Prop> output = {a};
  Layout layout = DoLayout(library, world, output);
  library.DRC(layout.circuit);
  Verify(layout, output);
}

static void NotVar(const CellLibrary &library) {
  Print("Not Var ...\n");
  World world{.symbol_names = {"a"}};
  Prop a{Var{.id = 0}};

  std::vector<Prop> output = {-a};
  Layout layout = DoLayout(library, world, output);
  library.DRC(layout.circuit);
  Verify(layout, output);
}

static void AndVars(const CellLibrary &library) {
  Print("And Vars ...\n");
  World world{.symbol_names = {"a", "b", "c", "d"}};
  Prop a{Var{.id = 0}}, b{Var{.id = 1}}, c{Var{.id = 2}}, d{Var{.id = 3}};

  std::vector<Prop> output = {a & b};
  Layout layout = DoLayout(library, world, output);
  library.DRC(layout.circuit);
  Verify(layout, output);
}

static void OrVars(const CellLibrary &library) {
  Print("Or Vars ...\n");
  World world{.symbol_names = {"a", "b"}};
  Prop a{Var{.id = 0}}, b{Var{.id = 1}};

  std::vector<Prop> output = {a | b};
  Layout layout = DoLayout(library, world, output);
  library.DRC(layout.circuit);
  Verify(layout, output);
}

static void XorVars(const CellLibrary &library) {
  Print("Xor Vars ...\n");
  World world{.symbol_names = {"a", "b"}};
  Prop a{Var{.id = 0}}, b{Var{.id = 1}};

  std::vector<Prop> output = {a ^ b};
  Layout layout = DoLayout(library, world, output);
  library.DRC(layout.circuit);
  Verify(layout, output);
}

static void MultiOutput(const CellLibrary &library) {
  Print("Multi Output ...\n");
  World world{.symbol_names = {"a", "b", "c"}};
  Prop a{Var{.id = 0}}, b{Var{.id = 1}}, c{Var{.id = 2}};

  std::vector<Prop> output = {a & b, b | c, c ^ a, -a};
  Layout layout = DoLayout(library, world, output);
  library.DRC(layout.circuit);
  Verify(layout, output);
}

int main(int argc, char **argv) {
  ANSI::Init();

  CellLibrary library;

  Empty(library);
  Consts(library);
  SingleVar(library);
  NotVar(library);
  /*
  AndVars(library);
  OrVars(library);
  XorVars(library);
  MultiOutput(library);
  */
  Print("OK\n");
  return 0;
}
