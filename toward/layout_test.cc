
#include "circuit.h"
#include "layout.h"

#include <deque>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "cell-library.h"
#include "level.h"
#include "prop.h"

static void StartTest(std::string_view name) {
  Print("\n\n" ABGCOLOR(0, 0, 160, "== {} ==") "\n", name);
}

static void Verify(const Layout &layout, const std::vector<Prop> &props) {
  std::vector<Func> funcs;
  for (auto [v, t] : layout.input_vars) {
    funcs.push_back(Func{.prop = Prop{Var{.id = v}}, .type = t});
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
  World world;
  std::unique_ptr<LayoutEngine> le = LayoutEngine::Create(library, world);
  std::vector<Prop> nothing;
  Layout trivial = le->DoLayout(nothing);
  CHECK(trivial.input_vars.empty());
  // ... should be one empty layer in this case? ...
  Verify(trivial, nothing);
}

static void Consts(const CellLibrary &library) {
  StartTest("Consts");
  World world;
  std::unique_ptr<LayoutEngine> le = LayoutEngine::Create(library, world);
  std::vector<Prop> output = {True(), False()};
  Layout layout = le->DoLayout(output);
  library.DRC(layout.circuit);
  Verify(layout, output);
}

static void SingleVar(const CellLibrary &library) {
  StartTest("Single Var");
  World world{.symbol_names = {"a"}};
  std::unique_ptr<LayoutEngine> le = LayoutEngine::Create(library, world);
  Prop a{Var{.id = 0}};

  std::vector<Prop> output = {a};
  Layout layout = le->DoLayout(output);
  library.DRC(layout.circuit);
  Verify(layout, output);
}

static void NotVar(const CellLibrary &library) {
  StartTest("Not Var ");
  World world{.symbol_names = {"a"}};
  std::unique_ptr<LayoutEngine> le = LayoutEngine::Create(library, world);
  Prop a{Var{.id = 0}};

  std::vector<Prop> output = {-a};
  Layout layout = le->DoLayout(output);
  library.DRC(layout.circuit);
  Verify(layout, output);
}

static void AndVars(const CellLibrary &library) {
  StartTest("And Vars ");
  World world{.symbol_names = {"a", "b", "c", "d"}};
  std::unique_ptr<LayoutEngine> le = LayoutEngine::Create(library, world);
  Prop a{Var{.id = 0}}, b{Var{.id = 1}}, c{Var{.id = 2}}, d{Var{.id = 3}};

  std::vector<Prop> output = {a & b};
  Layout layout = le->DoLayout(output);
  library.DRC(layout.circuit);
  Verify(layout, output);
}

static void OrVars(const CellLibrary &library) {
  StartTest("Or Vars");
  World world{.symbol_names = {"a", "b"}};
  std::unique_ptr<LayoutEngine> le = LayoutEngine::Create(library, world);
  Prop a{Var{.id = 0}}, b{Var{.id = 1}};

  std::vector<Prop> output = {a | b};
  Layout layout = le->DoLayout(output);
  library.DRC(layout.circuit);
  Verify(layout, output);
}

static void XorVars(const CellLibrary &library) {
  StartTest("Xor Vars");
  World world{.symbol_names = {"a", "b"}};
  std::unique_ptr<LayoutEngine> le = LayoutEngine::Create(library, world);
  Prop a{Var{.id = 0}}, b{Var{.id = 1}};

  std::vector<Prop> output = {a ^ b};
  Layout layout = le->DoLayout(output);
  library.DRC(layout.circuit);
  Verify(layout, output);
}

static void MultiOutput(const CellLibrary &library) {
  StartTest("Multi Output");
  World world{.symbol_names = {"a", "b", "c"}};
  Prop a{Var{.id = 0}}, b{Var{.id = 1}}, c{Var{.id = 2}};

  std::vector<Prop> output = {a & b, b | c, c ^ a, -a};
  std::unique_ptr<LayoutEngine> le = LayoutEngine::Create(library, world);
  le->SetVerbose(2);
  Layout layout = le->DoLayout(output);
  library.DRC(layout.circuit);
  Verify(layout, output);
}

static void TestCanPlaceCell(const CellLibrary &library) {
  StartTest("CanPlaceCell");
  static constexpr int INPUT_WIDTH = Levels::IN_WIDTH;

  World world;
  std::unique_ptr<LayoutEngine> le = LayoutEngine::Create(library, world);

  Cell const0(CONST0);
  int const0_w = library.GetInfo(const0).block_width;

  Cell and_cell(AND0110);
  CellLibrary::Info and_info = library.GetInfo(and_cell);
  int and_out_x = and_info.outputs[0].xblock;

  // Basic overlap with already placed cells.
  {
    // Empty chutes.
    std::vector<LayoutEngine::Chute> top;
    std::vector<bool> assigned;
    // But a placed cell.
    std::vector<LayoutEngine::PC> next = {
      {.xpos = 100, .cell = const0},
    };

    // Left overlap
    CHECK(!le->CanPlaceCell(-1, top, assigned, next,
                            const0, 100 - const0_w + 1));
    // Exact left
    CHECK(le->CanPlaceCell(-1, top, assigned, next,
                           const0, 100 - const0_w));
    // Right overlap
    CHECK(!le->CanPlaceCell(-1, top, assigned, next,
                            const0, 100 + const0_w - 1));
    // Exact right
    CHECK(le->CanPlaceCell(-1, top, assigned, next,
                           const0, 100 + const0_w));
  }

  // Exact match of a cell's output to a single unassigned chute.
  {
    std::vector<LayoutEngine::Chute> top;
    top.push_back({.pos = 200,
        .prop = True() & True(),
        .type = CType::MIXED,
      });
    std::vector<bool> assigned = {false};
    std::vector<LayoutEngine::PC> next;

    int exact_x = 200 - and_out_x;
    CHECK(le->CanPlaceCell(-1, top, assigned, next,
                           and_cell, exact_x));

    // If slightly misaligned, it will block the chute.
    CHECK(!le->CanPlaceCell(-1, top, assigned, next, and_cell, exact_x + 1));
    CHECK(!le->CanPlaceCell(-1, top, assigned, next, and_cell, exact_x - 1));
  }

  // Trapping an unassigned chute with a wide spacer.
  {
    std::vector<LayoutEngine::Chute> top;
    top.push_back({.pos = 300, .prop = True(), .type = CType::MIXED});
    std::vector<bool> assigned = {false};
    std::vector<LayoutEngine::PC> next;

    Cell wide_spacer = CellLibrary::Spacer(100);
    // Centering the spacer around the chute at 300 (xpos=250 to 350).
    CHECK(!le->CanPlaceCell(-1, top, assigned, next, wide_spacer, 250));

    // Placing the spacer far away should be fine.
    CHECK(le->CanPlaceCell(-1, top, assigned, next, wide_spacer, 500));
    CHECK(le->CanPlaceCell(-1, top, assigned, next, wide_spacer, -500));
  }

  // Assigned chutes do not need clearance and can be blocked.
  {
    std::vector<LayoutEngine::Chute> top;
    top.push_back({.pos = 400, .prop = True(), .type = CType::MIXED});
    std::vector<bool> assigned = {true};
    std::vector<LayoutEngine::PC> next;

    // Normally, running right up against a chute would fail because
    // it would prohibit putting something there. But if it is already
    // assigned, we have no obligation to stay clear of it.
    Cell wide_spacer = CellLibrary::Spacer(100);
    CHECK(le->CanPlaceCell(-1, top, assigned, next, wide_spacer, 300));
    CHECK(le->CanPlaceCell(-1, top, assigned, next, wide_spacer,
                           400 + INPUT_WIDTH));
  }

  // Chain of dependent chutes.
  {
    const int mcc = le->MinClearanceClose();
    const int mcf = le->MinClearanceFar();
    // Need something in between the two bounds: Too close for the
    // large side, but far enough for the small side. This means that
    // the wires can only fit when leaning to the right.
    const int mid = (mcc + mcf) / 2;
    CHECK(mcc < mid && mid < mcf) << mcc << " " << mid << " " << mcf;

    // Enough room for a series of wires.
    int stride = mcc + mcf + INPUT_WIDTH + 1;

    Print("{} < {} < {}. Stride: {}\n", mcc, mid, mcf, stride);

    std::vector<LayoutEngine::Chute> top = {
      {.pos = 500, .prop = True(), .type = CType::MIXED},
      {.pos = 500 + stride, .prop = True(), .type = CType::MIXED},
      {.pos = 500 + 2 * stride, .prop = True(), .type = CType::MIXED},
    };
    std::vector<bool> assigned = {false, false, false};
    std::vector<LayoutEngine::PC> next = {
      // Left side is blocked.
      {.xpos = 400, .cell = CellLibrary::Spacer(100 - mid)},
    };

    // Try to place a spacer on the right side, squeezing the chutes.
    Cell block_right = CellLibrary::Spacer(100);
    CHECK(!le->CanPlaceCell(-1, top, assigned, next, block_right,
                            500 + 2 * stride + INPUT_WIDTH + mid));

    // Placing it farther right leaves enough room for all three to route
    // (to the right).
    CHECK(le->CanPlaceCell(-1, top, assigned, next, block_right,
                           500 + 3 * stride));

    // Or way to the left.
    CHECK(le->CanPlaceCell(-1, top, assigned, next, block_right, 0));
  }

  // Cell with multiple outputs matching multiple chutes.
  {
    Cell sep_cell(SEPARATOR01);
    CellLibrary::Info sep_info = library.GetInfo(sep_cell);
    int out0_x = sep_info.outputs[0].xblock;
    int out1_x = sep_info.outputs[1].xblock;

    std::vector<LayoutEngine::Chute> top {
      {.pos = 600, .prop = True(), .type = sep_info.outputs[0].type},
      {.pos = 600 - out0_x + out1_x, .prop = True(),
       .type = sep_info.outputs[1].type},
    };
    std::vector<bool> assigned = {false, false};
    std::vector<LayoutEngine::PC> next;

    int exact_x = 600 - out0_x;
    CHECK(le->CanPlaceCell(-1, top, assigned, next, sep_cell, exact_x));

    // A small offset means it no longer satisfies the chutes, and
    // since it's wide, it will overlap and trap them instead.
    CHECK(!le->CanPlaceCell(-1, top, assigned, next, sep_cell, exact_x + 1));
  }

  // Inputs of newly placed cells must have enough clearance from
  // inputs of already placed cells.
  {
    std::vector<LayoutEngine::Chute> top;
    std::vector<bool> assigned;

    Cell cell(NOT0);
    CellLibrary::Info info = library.GetInfo(cell);

    std::vector<LayoutEngine::PC> next = {
      {.xpos = 1000, .cell = cell},
    };

    int min_dist =
      Levels::OUT_WIDTH + le->MinClearanceClose() + le->MinClearanceFar();
    int last_in1 = 1000 + info.inputs.back().xblock;

    int xpos_too_close = last_in1 + min_dist - 1 -
      info.inputs.front().xblock;
    int cell1_right = 1000 + info.block_width;

    if (xpos_too_close >= cell1_right) {
      CHECK(!le->CanPlaceCell(-1, top, assigned, next,
                              cell, xpos_too_close));
      CHECK(le->CanPlaceCell(-1, top, assigned, next,
                             cell, xpos_too_close + 1));
    }
  }
}

static void TestLoop1() {
  CellLibrary library;
  World world;
  std::unique_ptr<LayoutEngine> le = LayoutEngine::Create(library, world);
  le->SetVerbose(2);

  /*
 [40] Chute(pos=1542, prop=v733, type=ONE)
 [41] Chute(pos=1563, prop=v733, type=ZERO)
 [42] Chute(pos=3070, prop=¬v629 ⋀ ¬v655, type=ONE)
 [43] Chute(pos=3091, prop=¬v629 ⋀ ¬v655, type=ZERO)
 [44] Chute(pos=3105, prop=v752, type=ZERO)
 [45] Chute(pos=5935, prop=v746, type=ONE)
 [46] Chute(pos=5969, prop=v752, type=ONE)
  */

  Prop v629{Var{.id = 629}};
  Prop v655{Var{.id = 655}};
  Prop v733{Var{.id = 733}};
  Prop v746{Var{.id = 746}};
  Prop v752{Var{.id = 752}};

  struct ChuteDesc {
    int pos;
    Prop prop;
    CType type;
  };

  std::vector<ChuteDesc> chutes = {
      {1542, v733, CType::ONE},
      {1563, v733, CType::ZERO},
      {3070, -v629 & -v655, CType::ONE},
      {3091, -v629 & -v655, CType::ZERO},
      {3105, v752, CType::ZERO},
      {5935, v746, CType::ONE},
      {5969, v752, CType::ONE},
  };

  std::vector<LayoutEngine::LC> top_layer;
  int current_x = 0;

  for (const ChuteDesc &desc : chutes) {
    Cell cell = CellLibrary::WireA(0, desc.type);
    int in_x = library.GetInfo(cell).inputs[0].xblock;
    int cell_x = desc.pos - in_x;

    if (cell_x > current_x) {
      top_layer.push_back(LayoutEngine::LC{
          .inprops = {},
          .cell = CellLibrary::Spacer(cell_x - current_x),
      });
      current_x = cell_x;
    }
    CHECK(cell_x == current_x) << "Overlap in test case construction!";
    top_layer.push_back(LayoutEngine::LC{
        .inprops = {desc.prop},
        .cell = cell,
    });
    current_x += library.GetInfo(cell).block_width;
  }

  std::deque<std::vector<LayoutEngine::LC>> layers;
  layers.push_back(std::move(top_layer));

  for (int i = 0; i < 16; i++) {
    CHECK(!layers.empty());
    if (le->AllVars(layers.front()).has_value()) {
      Print("Got all vars!\n");
      break;
    }
    le->DoAddLayer(&layers);
  }

}

int main(int argc, char **argv) {
  ANSI::Init();

  CellLibrary library;

  TestLoop1();

  // Tests of helpers.
  TestCanPlaceCell(library);

  // Tests of the full layout algorithm.
  Empty(library);
  Consts(library);
  SingleVar(library);
  NotVar(library);
  AndVars(library);
  OrVars(library);
  XorVars(library);
  MultiOutput(library);

  Print("OK\n");
  return 0;
}
