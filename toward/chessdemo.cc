
#include <memory>

#include "ansi.h"
#include "base/print.h"
#include "cell-library.h"
#include "chessprop.h"
#include "image.h"
#include "layout.h"
#include "prop.h"
#include "render-circuit.h"
#include "span-util.h"

static void RenderOne() {
  CellLibrary library;

  World world;
  ChessProp::Board board = ChessProp::NewBoard(&world);

  Prop is_legal =
    ChessProp::IsLegal(board,
                       // b4
                       6, 1,
                       4, 1);

  size_t before = PropSize(is_legal);
  is_legal = SimplifyProp(is_legal);
  size_t after = PropSize(is_legal);

  Print("Simplified: {} -> {}\n", before, after);

  is_legal = SimplifyProp(BalanceProp(is_legal));

  size_t after2 = PropSize(is_legal);
  Print("Balanced: {} -> {}\n", after, after2);

  std::unique_ptr<LayoutEngine> le = LayoutEngine::Create(library, world);
  le->SetVerbose(1);
  Layout layout = le->DoLayout(Span{is_legal});
  Print("Got layout!\n");
  library.DRC(layout.circuit);
  Print("DRC ok!\n");

  ImageRGBA img = RenderCircuit(library, layout.circuit);
  img.Save("b4.png");
  Print("Wrote b4.png\n");
}

int main(int argc, char **argv) {
  ANSI::Init();

  RenderOne();

  return 0;
}

