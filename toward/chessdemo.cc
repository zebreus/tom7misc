
#include <format>
#include <memory>
#include <string>

#include "ansi.h"
#include "base/print.h"
#include "cell-library.h"
#include "chessprop.h"
#include "image.h"
#include "layout.h"
#include "prop.h"
#include "render-circuit.h"
#include "span-util.h"

static std::string Square(int row, int col) {
  return std::format("{:c}{:c}", 'a' + row, '1' + (7 - col));
}

static void RenderOne() {
  CellLibrary library;

  World world;
  ChessProp::Board board = ChessProp::NewBoard(&world);

  #if 0
  // Too big :(
  Prop is_legal =
    ChessProp::IsLegal(board,
                       // b4
                       6, 1,
                       4, 1);
  #endif

  Prop is_legal = False();

  ChessProp::Details details = ChessProp::KID_CHESS;

  int trivial = 0, normal = 0;
  for (int srcr = 0; srcr < 8; srcr++) {
    for (int srcc = 0; srcc < 8; srcc++) {
      for (int dstr = 0; dstr < 8; dstr++) {
        for (int dstc = 0; dstc < 8; dstc++) {
          Prop prop =
            SimplifyProp(ChessProp::IsLegal(
                             board, srcr, srcc, dstr, dstc, details));
          int size = PropSize(prop);
          if (size > 256) {
            Print("Large prop {}->{}:\n{}\n",
                  Square(srcr, srcc), Square(dstr, dstc),
                  PropString(world, prop));
          }
          if (prop == False()) {
            trivial++;
          } else {
            normal++;
            is_legal = is_legal | prop;
          }
        }
      }
    }
  }

  Print("Normal: {}\n"
        "Trivial: {}\n", normal, trivial);

  size_t before = PropSize(is_legal);
  is_legal = SimplifyProp(is_legal);
  size_t after = PropSize(is_legal);

  Print("Simplified: {} -> {}\n", before, after);

  is_legal = SimplifyProp(BalanceProp(is_legal));

  size_t after2 = PropSize(is_legal);
  Print("Balanced: {} -> {}\n", after, after2);

  Print("Prop:\n{}\n", PropString(world, is_legal));

  std::unique_ptr<LayoutEngine> le = LayoutEngine::Create(library, world);
  le->SetVerbose(1);
  le->SetWriteImages(false);
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

