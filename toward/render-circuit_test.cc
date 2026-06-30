
#include "render-circuit.h"

#include <memory>
#include <vector>

#include "ansi.h"
#include "base/print.h"
#include "cell-library.h"
#include "image.h"
#include "layout.h"
#include "prop.h"

static void RenderAnd(const CellLibrary &library) {
  World world{.symbol_names = {"a", "b", "c", "d"}};
  std::unique_ptr<LayoutEngine> le = LayoutEngine::Create(library, world);
  Prop a{Var{.id = 0}}, b{Var{.id = 1}}, c{Var{.id = 2}}, d{Var{.id = 3}};

  std::vector<Prop> output = {a & b};
  Layout layout = le->DoLayout(output);

  ImageRGBA img = RenderCircuit(library, layout.circuit);
  img.Save("and.png");
  library.DRC(layout.circuit);
}

static void RenderMulti(const CellLibrary &library) {
  World world{.symbol_names = {"a", "b", "c"}};
  Prop a{Var{.id = 0}}, b{Var{.id = 1}}, c{Var{.id = 2}};

  std::vector<Prop> output = {a & b, b | c, c ^ a, -a};
  std::unique_ptr<LayoutEngine> le = LayoutEngine::Create(library, world);
  Layout layout = le->DoLayout(output);
  ImageRGBA img = RenderCircuit(library, layout.circuit);
  img.Save("multi.png");

  library.DRC(layout.circuit);
}


int main(int argc, char **argv) {
  ANSI::Init();

  CellLibrary library;

  RenderAnd(library);
  RenderMulti(library);

  Print("OK\n");
  return 0;
}
