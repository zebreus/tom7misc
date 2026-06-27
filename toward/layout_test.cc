
#include "layout.h"

#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "cell-library.h"
#include "prop.h"

static void Empty(const CellLibrary &library) {
  World empty;
  std::vector<Prop> nothing;

  Layout trivial = DoLayout(library, empty, nothing);
  CHECK(trivial.input_vars.empty());
  // ... should be one empty layer in this case? ...
}

static void AndVars(const CellLibrary &library) {
  World world{.symbol_names = {"a", "b", "c", "d"}};
  Prop a{Var{.id = 0}}, b{Var{.id = 1}}, c{Var{.id = 2}}, d{Var{.id = 3}};

  std::vector<Prop> output = {a & b};
  Layout andvars = DoLayout(library, world, output);
  library.DRC(andvars.circuit);
}

int main(int argc, char **argv) {
  ANSI::Init();

  CellLibrary library;

  Empty(library);
  AndVars(library);

  Print("OK\n");
  return 0;
}
