
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ansi.h"
#include "auto-histo.h"
#include "base/print.h"
#include "cell-library.h"
#include "chess.h"
#include "chessprop.h"
#include "circuit.h"
#include "drc.h"
#include "image.h"
#include "layout.h"
#include "optimization.h"
#include "prop.h"
#include "render-circuit.h"
#include "simplification.h"
#include "span-util.h"
#include "threadutil.h"
#include "timer.h"
#include "util.h"

static constexpr bool OPTIMIZE = true;
static constexpr int MAX_RENDER_LAYERS = 1000;

static const Simplification &SimSingleton() {
  static const Simplification *s = new Simplification;
  return *s;
}

[[maybe_unused]]
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
                  ChessProp::Square(srcr, srcc),
                  ChessProp::Square(dstr, dstc),
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

  ImageRGBA img = RenderLayout(library, layout);
  img.Save("parallel.png");
}

static void RenderOneProp(const CellLibrary &library,
                          const World &world,
                          const Position::Move &move,
                          const Prop &prop) {
  Print(AYELLOW("== LAYOUT ==") "\n"
        "Prop size: {} ({})\n", PropSize(prop), SharedPropSize(prop));

  std::unique_ptr<LayoutEngine> le = LayoutEngine::Create(library, world);
  le->SetVerbose(1);
  le->SetWriteImages(false);

  std::string basename = std::format(
      "one-legal-{}-{}",
      ChessProp::Square(move.src_row, move.src_col),
      ChessProp::Square(move.dst_row, move.dst_col));

  Layout layout = le->DoLayout(Span{prop});
  Print("Got layout! {}\n", LayoutEngine::LayoutInfo(layout));
  DRC::CheckLayout(library, basename, layout);
  Print("DRC ok!\n");

  {
    std::string filename = std::format("{}-unopt.layout", basename);
    Util::WriteFile(filename,
                    LayoutEngine::Serialize(layout));

  }

  if (OPTIMIZE) {
    Timer timer;
    Layout opt_layout = Optimization::Optimize(library, layout);
    Print("Optimized in {}!\n{}\n",
          ANSI::Time(timer.Seconds()),
          LayoutEngine::LayoutInfo(opt_layout));
    DRC::AssertEquivalentLayout(library, basename, layout, opt_layout);
    Print("Optimized DRC ok!\n");
    layout = std::move(opt_layout);
  }

  {
    std::string filename = std::format("{}.layout", basename);
    Util::WriteFile(filename,
                    LayoutEngine::Serialize(layout));
    Print("Wrote {}.\n", filename);
  }

  Circuit trunc = TruncateCircuit(std::move(layout.circuit),
                                  MAX_RENDER_LAYERS);

  ImageRGBA img = RenderCircuitMini(library, trunc);
  img.Save(std::format("{}.png", basename));
}

static void RenderOne(ChessProp::Details details) {
  CellLibrary library;

  World world;
  ChessProp::Board board = ChessProp::NewBoard(&world);

  Position::Move move = {
    .src_row = 6,
    .src_col = 1,
    .dst_row = 4,
    .dst_col = 1,
  };

  Prop prop =
    SimplifyProp(ChessProp::IsLegal(
                     board,
                     move.src_row, move.src_col, move.dst_row, move.dst_col,
                     details));
  prop = BalanceProp(prop);
  prop = SimplifyProp(prop);
  int size1 = PropSize(prop);
  int sh1 = SharedPropSize(prop);
  prop = SimSingleton().Simplify(prop);
  int size2 = PropSize(prop);
  int sh2 = SharedPropSize(prop);
  Print("Prop size: {} ({}) -> {} ({})\n", size1, sh1, size2, sh2);
  Print("Prop:\n{}\n", PropString(world, prop));

  RenderOneProp(library, world, move, prop);
}

static void RenderFrom(std::string_view prop_dir,
                       const Position::Move &m) {
  CellLibrary library;

  // Get variable names for pretty-printing.
  // We assume that these indices haven't changed compared to the serialized
  // proposition, which just uses numbers!
  World world;
  (void)ChessProp::NewBoard(&world);

  std::string file = std::format("{}/islegal-{}-{}.prop", prop_dir,
                                 ChessProp::Square(m.src_row, m.src_col),
                                 ChessProp::Square(m.dst_row, m.dst_col));

  std::optional<Prop> oprop = ParseProp(Util::ReadFile(file));
  CHECK(oprop.has_value()) << file;

  RenderOneProp(library, world, m, oprop.value());
}

struct ChessDemo {
  CellLibrary library;
  World world;
  ChessProp::Board board;

  static constexpr ChessProp::Details details = ChessProp::KID_CHESS;

  ChessDemo() {
    board = ChessProp::NewBoard(&world);
  }

  // stats across nontrivial circuits
  AutoHisto layer_histo = AutoHisto(10000);
  AutoHisto cell_histo = AutoHisto(10000);
  int64_t total_cells = 0;
  int64_t total_layers = 0;
  int64_t total_redundant = 0;
  int64_t max_layers = 0;
  std::string max_layers_name;

  Layout RenderOne(std::string_view name, const Prop &prop_in) {
    size_t before = PropSize(prop_in);
    Prop prop = SimplifyProp(prop_in);
    prop = SimSingleton().Simplify(prop);
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

    if (OPTIMIZE) {
      Layout opt_layout = Optimization::Optimize(library, layout);
      Print("Optimized! {}\n", LayoutEngine::LayoutInfo(opt_layout));
      DRC::AssertEquivalentLayout(library, name, layout, opt_layout);
      Print("Optimized DRC ok!\n");
      layout = std::move(opt_layout);
    }

    int64_t layers = layout.circuit.layers.size();
    int64_t cells = CircuitSize(layout.circuit);
    layer_histo.Observe(layers);
    cell_histo.Observe(cells);
    total_layers += layers;
    total_cells += cells;
    total_redundant += LayoutEngine::RedundantInputs(layout);
    if (layers > max_layers) {
      max_layers = layers;
      max_layers_name = name;
    }

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
              std::format("{}-{}",
                          ChessProp::Square(srcr, srcc),
                          ChessProp::Square(dstr, dstc));
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
                  ImageRGBA img = RenderLayout(library, lay);
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

    Print("Cells per circuit:\n{}"
          "Layers per circuit:\n{}",
          cell_histo.SimpleANSI(20),
          layer_histo.SimpleANSI(20));

    Print("\nTotal cells: {}\n"
          "Total layers: {}\n"
          "Total redundant inputs: {}\n"
          "Largest circuit: {} ({} layers)\n"
          "\n",
          total_cells,
          total_layers,
          total_redundant,
          max_layers_name,
          max_layers);
  }
};


int main(int argc, char **argv) {
  ANSI::Init();

  /*
  ChessDemo demo;
  demo.RenderAll();
  */

  // RenderParallel();

  // RenderOne(ChessProp::REAL_CHESS);
  // RenderOne(ChessProp::KID_CHESS);

  const Position::Move b2b4 = {
    .src_row = 6,
    .src_col = 1,
    .dst_row = 4,
    .dst_col = 1,
  };

  RenderFrom("chess", b2b4);

  return 0;
}

