
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ansi.h"
#include "base/print.h"
#include "cell-library.h"
#include "chessprop.h"
#include "circuit.h"
#include "drc.h"
#include "image.h"
#include "layout.h"
#include "optimization.h"
#include "prop.h"
#include "render-circuit.h"
#include "span-util.h"
#include "threadutil.h"
#include "util.h"

static constexpr bool OPTIMIZE = true;

static std::string Square(int row, int col) {
  return std::format("{:c}{:c}", 'a' + col, '1' + (7 - row));
}

static void RenderParallel() {
  CellLibrary library;

  World world;
  ChessProp::Board board = ChessProp::NewBoard(&world);

  ChessProp::Details details = ChessProp::KID_CHESS;

  int trivial = 0, normal = 0;
  std::vector<Prop> props;
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
            props.push_back(std::move(prop));
          }
        }
      }
    }
  }

  Print("Normal: {}\n"
        "Trivial: {}\n", normal, trivial);

  std::unique_ptr<LayoutEngine> le = LayoutEngine::Create(library, world);
  le->SetVerbose(1);
  le->SetWriteImages(false);
  Layout layout = le->DoLayout(props);
  Print("Got layout!\n");
  DRC::CheckLayout(library, "parallel", layout);
  Print("DRC ok!\n");

  ImageRGBA img = RenderCircuit(library, layout.circuit);
  img.Save("parallel.png");
}

static void RenderOne(ChessProp::Details details) {
  CellLibrary library;

  World world;
  ChessProp::Board board = ChessProp::NewBoard(&world);

  const int srcr = 6;
  const int srcc = 1;
  const int dstr = 4;
  const int dstc = 1;

  Prop prop =
    SimplifyProp(ChessProp::IsLegal(board, 6, 1, 4, 1, details));
  prop = BalanceProp(prop);
  prop = SimplifyProp(prop);
  int size = PropSize(prop);
  Print("Prop size: {}\n", size);
  Print("Prop:\n{}\n", PropString(world, prop));

  std::unique_ptr<LayoutEngine> le = LayoutEngine::Create(library, world);
  le->SetVerbose(1);
  le->SetWriteImages(false);

  std::string basename = std::format("one-legal-{}-{}",
                                     Square(srcr, srcc), Square(dstr, dstc));

  Layout layout = le->DoLayout(Span{prop});
  Print("Got layout! {}\n", LayoutEngine::LayoutInfo(layout));
  DRC::CheckLayout(library, basename, layout);
  Print("DRC ok!\n");

  if (OPTIMIZE) {
    Layout opt_layout = Optimization::Optimize(library, layout);
    Print("Optimized! {}\n", LayoutEngine::LayoutInfo(opt_layout));
    DRC::AssertEquivalentLayout(library, basename, layout, opt_layout);
    Print("Optimized DRC ok!\n");
    layout = std::move(opt_layout);
  }

  Util::WriteFile(std::format("{}.layout", basename),
                  LayoutEngine::Serialize(layout));

  ImageRGBA img = RenderCircuit(library, layout.circuit);
  img.Save(std::format("{}.png", basename));
}


struct ChessDemo {
  CellLibrary library;
  World world;
  ChessProp::Board board;

  static constexpr ChessProp::Details details = ChessProp::KID_CHESS;

  ChessDemo() {
    board = ChessProp::NewBoard(&world);
  }


  Layout RenderOne(std::string_view name, const Prop &prop_in) {
    size_t before = PropSize(prop_in);
    Prop prop = SimplifyProp(prop_in);
    size_t after = PropSize(prop);

    Print("[{}] Simplified: {} -> {}\n", name, before, after);

    prop = SimplifyProp(BalanceProp(prop));

    size_t after2 = PropSize(prop);
    Print("[{}] Balanced: {} -> {}\n", name, after, after2);

    Print("[{}] Prop:\n" AGREY("{}") "\n", name, PropString(world, prop));

    std::unique_ptr<LayoutEngine> le = LayoutEngine::Create(library, world);
    le->SetVerbose(0);
    le->SetWriteImages(false);
    Layout layout = le->DoLayout(Span{prop});
    Print("Got layout! {}\n", LayoutEngine::LayoutInfo(layout));
    DRC::CheckLayout(library, name, layout);
    Print("DRC OK\n");

    Layout opt_layout = Optimization::Optimize(library, layout);
    Print("Optimized! {}\n", LayoutEngine::LayoutInfo(opt_layout));
    DRC::AssertEquivalentLayout(library, name, layout, opt_layout);
    Print("Optimized DRC ok!\n");
    layout = std::move(opt_layout);

    return layout;
  }

  void RenderAll() {
    Asynchronously async(8);

    int trivial = 0, normal = 0;
    for (int srcr = 0; srcr < 8; srcr++) {
      for (int srcc = 0; srcc < 8; srcc++) {
        for (int dstr = 0; dstr < 8; dstr++) {
          for (int dstc = 0; dstc < 8; dstc++) {
            Prop prop =
              SimplifyProp(ChessProp::IsLegal(
                               board, srcr, srcc, dstr, dstc, details));
            int size = PropSize(prop);
            std::string move =
              std::format("{}-{}", Square(srcr, srcc), Square(dstr, dstc));
            if (size > 32) {
              Print("Large prop {}:\n{}\n",
                    move,
                    PropString(world, prop));
            }
            if (prop == False()) {
              trivial++;
            } else {
              normal++;

              Layout lay = RenderOne(move, prop);
              async.Run([this, lay = std::move(lay), move = std::move(move)]{
                  ImageRGBA img = RenderCircuit(library, lay.circuit);
                  img.Save(std::format("legal-{}.png", move));
                  Util::WriteFile(
                      std::format("legal-{}.layout", move),
                      LayoutEngine::Serialize(lay));
                });

            }
          }
        }
      }
    }

    Print("Did {} normal and skipped {} trivial.\n",
          normal, trivial);
  }
};




int main(int argc, char **argv) {
  ANSI::Init();

  ChessDemo demo;
  demo.RenderAll();

  // RenderParallel();

  // RenderOne(ChessProp::REAL_CHESS);
  // RenderOne(ChessProp::KID_CHESS);

  return 0;
}

