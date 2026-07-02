
#include "layout-canvas.h"

#include <cmath>
#include <string_view>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "cell-library.h"
#include "circuit.h"
#include "level.h"
#include "prop.h"

static void StartTest(std::string_view name) {
  Print("\n\n" ABGCOLOR(0, 0, 160, "== {} ==") "\n", name);
}

using Chute = LayoutCanvas::Chute;

static void TestCanPlaceCell(const CellLibrary &library) {
  StartTest("CanPlaceCell");
  static constexpr int INPUT_WIDTH = Levels::IN_WIDTH;

  // Layers can't be empty.
  const Chute DISTANT_CHUTE =
    Chute{
    .pos = 90000,
    .prop = True(),
    .type = CType::MIXED,
  };


  Cell const0(CONST0);
  int const0_w = library.GetInfo(const0).block_width;

  Cell and_cell(AND0110);
  CellLibrary::Info and_info = library.GetInfo(and_cell);
  int and_out_x = and_info.outputs[0].xblock;

  // Basic overlap with already placed cells.
  {
    LayoutCanvas canvas(library);
    canvas.Reset({DISTANT_CHUTE});
    canvas.next = {
      {.xpos = 100, .cell = const0},
    };

    // Left overlap
    CHECK(!canvas.CanPlaceCell(-1, const0, 100 - const0_w + 1));
    // Exact left
    CHECK(canvas.CanPlaceCell(-1, const0, 100 - const0_w));
    // Right overlap
    CHECK(!canvas.CanPlaceCell(-1, const0, 100 + const0_w - 1));
    // Exact right
    CHECK(canvas.CanPlaceCell(-1, const0, 100 + const0_w));
  }

  // Exact match of a cell's output to a single unassigned chute.
  {
    LayoutCanvas canvas(library);
    canvas.Reset({
      {.pos = 200,
       .prop = True() & True(),
       .type = CType::MIXED,
      }});

    int exact_x = 200 - and_out_x;
    CHECK(canvas.CanPlaceCell(-1, and_cell, exact_x));

    // If slightly misaligned, it will block the chute.
    CHECK(!canvas.CanPlaceCell(-1, and_cell, exact_x + 1));
    CHECK(!canvas.CanPlaceCell(-1, and_cell, exact_x - 1));
  }

  // Trapping an unassigned chute with a wide spacer.
  {
    LayoutCanvas canvas(library);
    canvas.Reset({
      {.pos = 300, .prop = True(), .type = CType::MIXED}
    });

    Cell wide_spacer = CellLibrary::Spacer(100);
    // Centering the spacer around the chute at 300 (xpos=250 to 350).
    CHECK(!canvas.CanPlaceCell(-1, wide_spacer, 250));

    // Placing the spacer far away should be fine.
    CHECK(canvas.CanPlaceCell(-1, wide_spacer, 500));
    CHECK(canvas.CanPlaceCell(-1, wide_spacer, -500));
  }

  // Assigned chutes do not need clearance and can be blocked.
  {
    LayoutCanvas canvas(library);
    canvas.Reset({
      {.pos = 400, .prop = True(), .type = CType::MIXED}
    });
    canvas.Assign(0);

    // Normally, running right up against a chute would fail because
    // it would prohibit putting something there. But if it is already
    // assigned, we have no obligation to stay clear of it.
    Cell wide_spacer = CellLibrary::Spacer(100);
    CHECK(canvas.CanPlaceCell(-1, wide_spacer, 300));
    CHECK(canvas.CanPlaceCell(-1, wide_spacer,
                           400 + INPUT_WIDTH));
  }

  // Chain of dependent chutes.
  {
    const int mcc = library.MinClearanceClose();
    const int mcf = library.MinClearanceFar();
    // Need something in between the two bounds: Too close for the
    // large side, but far enough for the small side. This means that
    // the wires can only fit when leaning to the right.
    const int mid = (mcc + mcf) / 2;
    CHECK(mcc < mid && mid < mcf) << mcc << " " << mid << " " << mcf;

    // Enough room for a series of wires.
    int stride = mcc + mcf + INPUT_WIDTH + 1;

    Print("{} < {} < {}. Stride: {}\n", mcc, mid, mcf, stride);

    LayoutCanvas canvas(library);
    canvas.Reset({
      {.pos = 500, .prop = True(), .type = CType::MIXED},
      {.pos = 500 + stride, .prop = True(), .type = CType::MIXED},
      {.pos = 500 + 2 * stride, .prop = True(), .type = CType::MIXED},
    });
    canvas.next = {
      // Left side is blocked.
      {.xpos = 400, .cell = CellLibrary::Spacer(100 - mid)},
    };

    // Try to place a spacer on the right side, squeezing the chutes.
    Cell block_right = CellLibrary::Spacer(100);
    CHECK(!canvas.CanPlaceCell(-1, block_right,
                            500 + 2 * stride + INPUT_WIDTH + mid));

    // Placing it farther right leaves enough room for all three to route
    // (to the right).
    CHECK(canvas.CanPlaceCell(-1, block_right,
                           500 + 3 * stride));

    // Or way to the left.
    CHECK(canvas.CanPlaceCell(-1, block_right, 0));
  }

  // Cell with multiple outputs matching multiple chutes.
  {
    Cell sep_cell(SEPARATOR01);
    CellLibrary::Info sep_info = library.GetInfo(sep_cell);
    int out0_x = sep_info.outputs[0].xblock;
    int out1_x = sep_info.outputs[1].xblock;

    LayoutCanvas canvas(library);
    canvas.Reset({
      {.pos = 600, .prop = True(), .type = sep_info.outputs[0].type},
      {.pos = 600 - out0_x + out1_x, .prop = True(),
       .type = sep_info.outputs[1].type},
    });

    int exact_x = 600 - out0_x;
    CHECK(canvas.CanPlaceCell(-1, sep_cell, exact_x));

    // A small offset means it no longer satisfies the chutes, and
    // since it's wide, it will overlap and trap them instead.
    CHECK(!canvas.CanPlaceCell(-1, sep_cell, exact_x + 1));
  }

  // Inputs of newly placed cells must have enough clearance from
  // inputs of already placed cells.
  {
    LayoutCanvas canvas(library);
    canvas.Reset({DISTANT_CHUTE});

    Cell cell(NOT0);
    CellLibrary::Info info = library.GetInfo(cell);

    canvas.next = {
      {.xpos = 1000, .cell = cell},
    };

    int min_dist =
      Levels::OUT_WIDTH + library.MinClearanceClose() +
      library.MinClearanceFar();
    int last_in1 = 1000 + info.inputs.back().xblock;

    int xpos_too_close = last_in1 + min_dist - 1 -
      info.inputs.front().xblock;
    int cell1_right = 1000 + info.block_width;

    if (xpos_too_close >= cell1_right) {
      CHECK(!canvas.CanPlaceCell(-1, cell, xpos_too_close));
      CHECK(canvas.CanPlaceCell(-1, cell, xpos_too_close + 1));
    }
  }
}

static void TestSolveSprings(const CellLibrary &library) {
  StartTest("SolveSprings");

  // Easy case: already perfectly spaced, no tension.
  {
    LayoutCanvas canvas(library);
    int target_dist = 100;
    int spacing = target_dist + Levels::IN_WIDTH;
    canvas.Reset({
      {.pos = 0, .prop = True(), .type = CType::MIXED,
       .anchored = false},
      {.pos = spacing, .prop = True(), .type = CType::MIXED,
       .anchored = false},
      {.pos = 2 * spacing, .prop = True(), .type = CType::MIXED,
       .anchored = false},
    });

    canvas.springs[0] = {.target_dist = target_dist, .min_dist = 10};
    canvas.springs[1] = {.target_dist = target_dist, .min_dist = 10};

    std::vector<double> xpos = canvas.SolveSprings();
    CHECK(xpos.size() == 3);
    CHECK(std::abs(xpos[0] - 0.0) < 1e-4);
    CHECK(std::abs(xpos[1] - spacing) < 1e-4);
    CHECK(std::abs(xpos[2] - 2 * spacing) < 1e-4);
  }

  // Solves tension to a predictable equilibrium.
  {
    LayoutCanvas canvas(library);
    int target_dist = 100;
    int spacing = target_dist + Levels::IN_WIDTH;
    canvas.Reset({
      {.pos = 0, .prop = True(), .type = CType::MIXED, .anchored = true},
      // Displaced by 30 units
      {.pos = spacing + 30, .prop = True(), .type = CType::MIXED, .anchored = false},
      {.pos = 2 * spacing, .prop = True(), .type = CType::MIXED, .anchored = true},
    });

    canvas.springs[0] = {.target_dist = target_dist, .min_dist = 10};
    canvas.springs[1] = {.target_dist = target_dist, .min_dist = 10};

    std::vector<double> xpos = canvas.SolveSprings();
    CHECK(xpos.size() == 3);
    CHECK(xpos[0] == 0.0);
    // The center chute has 1.0 weight for its current position (spacing + 30),
    // 1.0 weight from left spring pushing to `spacing`,
    // 1.0 weight from right spring pushing to `spacing`.
    // It converges in 1 iteration to (spacing + 30 + 2*spacing) / 3 =
    // spacing + 10.
    CHECK(std::abs(xpos[1] - (spacing + 10.0)) < 1e-4);
    CHECK(xpos[2] == 2.0 * spacing);
  }

  // Overconstrained case: Anchored ends are too close for the min_distance.
  {
    LayoutCanvas canvas(library);
    canvas.Reset({
      {.pos = 0, .prop = True(), .type = CType::MIXED, .anchored = true},
      {.pos = 10, .prop = True(), .type = CType::MIXED, .anchored = false},
      {.pos = 100, .prop = True(), .type = CType::MIXED, .anchored = true},
    });

    // Min distance of 80 each means we need 160 space (plus 2x IN_WIDTH),
    // but we only have 100 between the anchored chutes.
    canvas.springs[0] = {.target_dist = 100, .min_dist = 80};
    canvas.springs[1] = {.target_dist = 100, .min_dist = 80};

    std::vector<double> xpos = canvas.SolveSprings();
    CHECK(xpos.size() == 3);
    CHECK(xpos[0] == 0.0);
    // Should be placed exactly at the midpoint to balance the impossible
    // min_distance constraints.
    CHECK(std::abs(xpos[1] - 50.0) < 1e-4);
    CHECK(xpos[2] == 100.0);
  }

  // Min distance enforcement pushing a chute.
  {
    LayoutCanvas canvas(library);
    canvas.Reset({
      {.pos = 0, .prop = True(), .type = CType::MIXED, .anchored = true},
      {.pos = 5, .prop = True(), .type = CType::MIXED, .anchored = false},
    });
    int min_dist = 50;
    // Target is smaller than min_dist, so spring alone would violate
    // it. The chute's original position (5) also pulls it to the left
    // of the min_dist.
    canvas.springs[0] = {.target_dist = 20, .min_dist = min_dist};

    std::vector<double> xpos = canvas.SolveSprings();
    CHECK(xpos.size() == 2);
    CHECK(xpos[0] == 0.0);
    // Forced exactly to the min distance.
    double expected = 0.0 + Levels::IN_WIDTH + min_dist;
    CHECK(std::abs(xpos[1] - expected) < 1e-4);
  }
}

static void TestFlattenInputs(const CellLibrary &library) {
  StartTest("FlattenInputs");

  const Chute DISTANT_CHUTE =
    Chute{
    .pos = 90000,
    .prop = True(),
    .type = CType::MIXED,
  };

  LayoutCanvas canvas(library);
  canvas.Reset({DISTANT_CHUTE});

  Cell and_cell(AND0110);
  Cell const_cell(CONST0);
  Cell not_cell(NOT0);

  CellLibrary::Info and_info = library.GetInfo(and_cell);
  CellLibrary::Info const_info = library.GetInfo(const_cell);
  CellLibrary::Info not_info = library.GetInfo(not_cell);

  std::vector<LayoutCanvas::LC> top_layer = {
    {.inprops = {True(), True(), False(), False()}, .cell = and_cell},
    {.inprops = {}, .cell = const_cell},
    {.inprops = {True()}, .cell = not_cell},
  };

  std::vector<LayoutCanvas::Chute> chutes = canvas.FlattenInputs(top_layer);

  CHECK(chutes.size() == 5);

  CHECK(chutes[0].pos == and_info.inputs[0].xblock);
  CHECK(chutes[0].prop == True());
  CHECK(chutes[0].type == and_info.inputs[0].type);

  CHECK(chutes[1].pos == and_info.inputs[1].xblock);
  CHECK(chutes[1].prop == True());
  CHECK(chutes[1].type == and_info.inputs[1].type);

  CHECK(chutes[2].pos == and_info.inputs[2].xblock);
  CHECK(chutes[2].prop == False());
  CHECK(chutes[2].type == and_info.inputs[2].type);

  CHECK(chutes[3].pos == and_info.inputs[3].xblock);
  CHECK(chutes[3].prop == False());
  CHECK(chutes[3].type == and_info.inputs[3].type);

  int not_start = and_info.block_width + const_info.block_width;
  CHECK(chutes[4].pos == not_start + not_info.inputs[0].xblock);
  CHECK(chutes[4].prop == True());
  CHECK(chutes[4].type == not_info.inputs[0].type);
  CHECK(chutes[4].desire == LayoutCanvas::UNSPECIFIED);
}

int main(int argc, char **argv) {
  ANSI::Init();

  CellLibrary library;

  TestCanPlaceCell(library);
  TestFlattenInputs(library);
  TestSolveSprings(library);

  Print("OK\n");
  return 0;
}
