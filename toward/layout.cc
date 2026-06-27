
#include "layout.h"

#include <algorithm>
#include <compare>
#include <deque>
#include <functional>
#include <initializer_list>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "base/logging.h"
#include "cell-library.h"
#include "circuit.h"
#include "image.h"
#include "level.h"
#include "prop.h"
#include "span-util.h"
#include "vector-util.h"

// ----------------------------------------------------------------------
// Core concepts used in LayoutEngine:
//
// prop.h (Propositions):
//   Defines ASTs for boolean logic (Prop, Var, Value, AND, NOT, etc.).
//   The LayoutEngine's goal is to physically compute a set of target
//   propositions. It works bottom-up, starting with the desired output
//   propositions and decomposing them into simpler ones (e.g. splitting
//   A & B into A and B) layer by layer until only variables remain.
//
// circuit.h (Circuit):
//   Defines the abstract structure of a circuit as a sequence of Layers,
//   where each Layer contains Cells (instances of Gates like AND0110, NOT,
//   WIRE, SPACER). It defines chute types (MIXED, ZERO, ONE) representing
//   how boolean values are physically encoded (together or on separated
//   tracks). The LayoutEngine constructs this abstract Circuit.
//   Only certain gates have solutions, so this is a major source of
//   constraints. For example, AND0110 needs its input propositions to
//   be separated, but produces a mixed output.
//
// cell-library.h (Cell Library):
//   Bridges abstract Cells and their physical sizes. LayoutEngine queries
//   the CellLibrary to find a Cell's block width and the exact x-offsets
//   and types of its inputs/outputs. This is necessary to align the outputs
//   of one layer with the inputs of the next without overlapping components.
//
// level.h (Levels):
//   Defines the 2D physics geometry (Level, LevelBody, polygon meshes).
//   While LayoutEngine mainly works with abstract Cells, the CellLibrary
//   loads the underlying Level geometry from SVGs. The final Circuit is
//   ultimately composed into a large Level for the physics engine to
//   simulate the marbles (1s and 0s) rolling through it.
// ----------------------------------------------------------------------

// Layout cell is a working representation, where we have
// a Cell (or perhaps an abstract cell) and the vector of
// input propositions for it.
struct LC {
  std::vector<Prop> inprops;
  Cell cell;
};

// Returns the flattened vector of variable ids if all
// of the inputs are variables; nullopt otherwise.
static std::optional<std::vector<int>>
AllVars(std::span<const LC> lcs) {
  std::vector<int> vars;
  for (const LC &lc : lcs) {
    for (const Prop &p : lc.inprops) {
      if (const Var *v = std::get_if<Var>(&p.p)) {
        vars.push_back(v->id);
      } else {
        return std::nullopt;
      }
    }
  }
  return {vars};
}

struct LayoutEngine {
  const World &world;
  const CellLibrary &library;

  static constexpr int INPUT_WIDTH = Levels::IN_WIDTH;
  // The minimum clearance that we need to the left and right of an
  // input (not including the width of the input itself) in order to
  // ensure that we can at least propagate that input upward with a
  // wire. We use this to check that we don't completely block a
  // nearby input when we assign cells greedily.
  const int min_clearance = 0;

  // Compute the minimum clearance to guarantee we can attach a wire;
  // initializes min_clearance. (This could probably just look at the
  // small-valued wires, but we might as well just be comprehensive.)
  int ComputeMinClearance() const {
    int max_min_clearance = 0;
    for (CType type : {CType::MIXED, CType::ZERO, CType::ONE}) {
      int min_req = 1e9;
      for (int k : {0, 1, 2, 4, 8, 16, 32, 64}) {
        for (int w = 0; w < 2; w++) {
          for (bool flip : {false, true}) {
            Cell cell = (w == 0) ? CellLibrary::WireA(k, type)
                                 : CellLibrary::WireB(k, type);
            cell.flip = flip;
            CellLibrary::Info info = library.GetInfo(cell);
            CHECK(info.outputs.size() == 1);

            int out_x = info.outputs[0].xblock;
            int left_clearance = out_x;
            int right_clearance = info.block_width - out_x - Levels::OUT_WIDTH;
            int req = left_clearance > right_clearance ?
              left_clearance : right_clearance;

            if (req < min_req) {
              min_req = req;
            }
          }
        }
      }
      if (min_req > max_min_clearance) {
        max_min_clearance = min_req;
      }
    }
    return max_min_clearance;
  }

  int ItsOutputPos(const Cell &cell) const {
    const CellLibrary::Info info = library.GetInfo(cell);
    CHECK(info.outputs.size() == 1);
    return info.outputs[0].xblock;
  }

  // What we want to do with a chute. This is thinking about the
  // bottom-up direction; "permute left" means a wire would slope
  // like a backslash.
  enum DesireType {
    UNSPECIFIED,
    // Apply a gate to decompose the proposition.
    DECOMPOSE,
    // Apply a combiner so that we have separated inputs.
    UNCOMBINE,
    // Unduplicate adjacent identical propositions.
    UNDUP,
    // The chute is out of order and should swap to its left.
    EXCHANGE_LEFT,
    // ... or right.
    EXCHANGE_RIGHT,
    // The chute is in order, and should flow to a relative offset
    // of its current position (number of blocks, in desire_val).
    FLOW,
  };

  static std::string_view DesireTypeString(DesireType dt) {
    switch (dt) {
    case UNSPECIFIED: return "UNSPECIFIED";
    case DECOMPOSE: return "DECOMPOSE";
    case UNCOMBINE: return "UNCOMBINE";
    case UNDUP: return "UNDUP";
    case EXCHANGE_LEFT: return "EXCHANGE_LEFT";
    case EXCHANGE_RIGHT: return "EXCHANGE_RIGHT";
    case FLOW: return "FLOW";
    default: return "??BAD DESIRETYPE??";
    }
  }

  // Location and type of the transition between layers where
  // an input and output meet.
  struct Chute {
    int pos = 0;
    Prop prop = False();
    CType type = CType::MIXED;

    DesireType desire = DesireType::UNSPECIFIED;
    int desire_val = 0;
  };

  // A placed cell.
  struct PC {
    int xpos = 0;
    Cell cell;
    std::vector<Prop> inprops;
  };

  // Given the input chutes for the complete top layer,
  // and the in-progress next layer (next), is it possible
  // to place the cell in the next layer with its left edge
  // at xpos? Needs to check:
  //  - It does not overlap anything already in that layer
  //  - It does not block off any chutes on the top layer
  //    (this does not include the chutes that match up
  //    to the cell's output, though!)
  bool CanPlaceCell(std::span<const Chute> top,
                    std::span<const PC> next,
                    const Cell &cell,
                    int xpos) const {
    CellLibrary::Info info = library.GetInfo(cell);
    int cell_left = xpos;
    int cell_right = xpos + info.block_width;

    // Overlapping something already placed in the next layer?
    for (const PC &pc : next) {
      int pc_left = pc.xpos;
      int pc_right = pc_left + library.GetInfo(pc.cell).block_width;
      if (cell_left < pc_right && cell_right > pc_left) {
        return false;
      }
    }

    // Blocking a chute from the top layer?
    for (const Chute &chute : top) {
      bool matched = false;
      for (const CellLibrary::IO &out : info.outputs) {
        if (xpos + out.xblock == chute.pos) {
          matched = true;
          break;
        }
      }

      if (!matched) {
        int chute_left = chute.pos - min_clearance;
        int chute_right = chute.pos + INPUT_WIDTH + min_clearance;
        if (cell_left < chute_right && cell_right > chute_left) {
          return false;
        }
      }
    }

    return true;
  }

  // Flatten the inputs. Desires are not yet specified.
  std::vector<Chute> FlattenInputs(std::span<const LC> top) {
    std::vector<Chute> chutes;
    // Current position (left edge of the next cell in the top layer,
    // in blocks).
    int pos = 0;
    for (const LC &lc : top) {
      CellLibrary::Info info = library.GetInfo(lc.cell);
      CHECK(info.inputs.size() == lc.inprops.size());

      for (int i = 0; i < lc.inprops.size(); i++) {
        const Prop &prop = lc.inprops[i];
        const CellLibrary::IO &io = info.inputs[i];

        int input_pos = pos + io.xblock;
        chutes.emplace_back(Chute{
            .pos = input_pos,
            .prop = prop,
            .type = io.type,
            .desire = DesireType::UNSPECIFIED,
            .desire_val = 0,
          });
      }
      pos += info.block_width;
    }

    return chutes;
  }

  // Given a top layer (annotated with the propositions it takes as
  // inputs), create a new layer that produces those layers and is
  // simpler. (Simpler as in some unspecified well-founded ordering
  // so that this process terminates.) The input and output layers
  // should start at x=0.
  std::pair<std::vector<LC>, int> AddLayer(std::span<const LC> top) {
    CHECK(!top.empty()) << "Precondition.";

    // The hard part is the physical routing. Some gates like AND0110
    // need separated inputs in a specific form. Since we are working
    // bottom-up, the AND gate itself is not bad (we just place it
    // wherever we need a mixed A & B prop). But satisfying the
    // separated 0 A, 1 A, 1 B, and 0 B inputs of that then becomes
    // tricky.

    // |0A| |1A|  |1B| |0B|
    //   \           /
    //    \         /
    //     \       /
    //       |A&B|

    // It's possible to work directly with separated wires: We have
    // NOT0, NOT1 and these are simple and efficient. We could also
    // create a separated gate like AND0110_0, but we almost always
    // need a matching AND0110_1 (e.g. to satisfy the 0A and 1A inputs
    // of an AND gate itself), and so we would end up duplicating
    // input propositions.

    // So, a high priority is to put pairs of separated wires
    // next to each other so that we can join them with a separator.
    // (But we only need to do this when they represent binary
    // propositions; we can directly strip ¬,create constants,
    // and postpone variables until the end.) The tricky thing about
    // *this* is that we can only reorder inputs when they are separated.

    // First we flatten all of the inputs we need to satisfy on the
    // top layer, with their positions. These are just fixed and
    // independent since we aren't going to try to move them around.

    std::vector<Chute> chutes = FlattenInputs(top);
    CHECK(!chutes.empty());

    // Now set the desires for each.
    // The first thing we need to do is deal with the order of the
    // chutes.

    // Exterior chutes that hold variables are done.
    std::vector<bool> done(chutes.size(), false);

    for (int c = 0; c < chutes.size(); c++) {
      Chute &chute = chutes[c];
      if (std::holds_alternative<Var>(chute.prop.p) &&
          chute.type == CType::MIXED) {
        // Still on the exterior.
        done[c] = true;
        chute.desire = DesireType::FLOW;
        // TODO: Adjust this later down once we know better how much
        // space we need.
        chute.desire_val = -8;
      } else {
        break;
      }
    }

    CHECK(!done.back()) << "Precondition: The top layer is already "
      "complete!";

    // Also the right side.
    for (int c = (int)chutes.size() - 1; c >= 0; c--) {
      Chute &chute = chutes[c];
      if (std::holds_alternative<Var>(chute.prop.p) &&
          chute.type == CType::MIXED) {
        // Still on the exterior.
        done[c] = true;
        chute.desire = DesireType::FLOW;
        chute.desire_val = +8;
      } else {
        break;
      }
    }

    // TODO: exterior combined pairs of variables should get
    // unseparated!

    // Now any internal mixed variable is going to be problematic,
    // because we will need to cross over it to attain the order
    // we want. So unseparate those.

    for (int c = 0; c < chutes.size(); c++) {
      Chute &chute = chutes[c];
      if (!done[c] &&
          chute.type == CType::MIXED &&
          std::holds_alternative<Var>(chute.prop.p)) {
        CHECK(chute.desire == DesireType::UNSPECIFIED);
        chute.desire = UNCOMBINE;
      }
    }

    // chute indices (the first one) that we want to
    // undup.
    std::vector<int> undup_pairs;

    // TODO: We should UNDUP propositions that are equal and
    // already next to one another. We want to do this before
    // crossing over, because reducing the number of total
    // chutes is a big efficiency win.
    for (int c = 0; c < (int)chutes.size() - 1; c++) {
      Chute &chute1 = chutes[c];
      Chute &chute2 = chutes[c + 1];
      // When we have two separated inputs for the same
      // proposition in a row, we should UNDUP them.
      if (!done[c] &&
          // Not if it already has a desire.
          chute1.desire == UNSPECIFIED &&
          chute2.desire == UNSPECIFIED &&
          // Must be same separated type.
          chute1.type != CType::MIXED &&
          chute1.type == chute2.type &&
          // And syntactically the same proposition.
          chute1.prop == chute2.prop) {
        chute1.desire = UNDUP;
        chute2.desire = UNDUP;
        // Note that if we have an odd number of equal
        // propositions, the last one will be passed
        // through and be in the correct position to
        // undup on the next layer.
        undup_pairs.push_back(c);
      }
    }


    // Similarly, decomposing a mixed binary proposition gets us
    // separated inputs (which we want) as well as simplifying.
    for (int c = 0; c < chutes.size(); c++) {
      Chute &chute = chutes[c];
      if (!done[c] &&
          chute.type == CType::MIXED &&
          std::holds_alternative<Binop>(chute.prop.p)) {
        CHECK(chute.desire == DesireType::UNSPECIFIED);
        chute.desire = DECOMPOSE;
      }

      // TODO: We could use a mixed NOT here if we were
      // confident in it.
    }

    // Then we need to uncombine remaining mixed inputs, since we
    // don't have a way of dealing with them.
    for (int c = 0; c < chutes.size(); c++) {
      Chute &chute = chutes[c];
      if (!done[c] &&
          chute.type == CType::MIXED &&
          chute.desire == DesireType::UNSPECIFIED) {
        chute.desire = UNCOMBINE;
      }
    }

    // Strip NOT from separated propositions, which is easy
    // to do locally. It would often be better to swap
    // and dedup first if possible (because when we swap
    // with some non-negated proposition, we are at least
    // helping that other one get to the right place!). But
    // these are basically just wires so they don't
    // particularly add complexity.
    for (int c = 0; c < chutes.size(); c++) {
      Chute &chute = chutes[c];
      if (!done[c] &&
          chute.type != CType::MIXED &&
          chute.desire == DesireType::UNSPECIFIED &&
          std::holds_alternative<Unop>(chute.prop.p)) {
        chute.desire = DECOMPOSE;
      }
    }

    // Now if an input doesn't have a desire, attempt to
    // put it in the global order.
    for (int c = 0; c < (int)chutes.size() - 1; c++) {
      Chute &chute1 = chutes[c];
      Chute &chute2 = chutes[c + 1];
      if (!done[c] &&
          chute1.desire == DesireType::UNSPECIFIED &&
          chute2.desire == DesireType::UNSPECIFIED) {
        // We should have dealt with this above!
        CHECK(chute1.type != CType::MIXED);
        CHECK(chute2.type != CType::MIXED);

        // Just do local swaps if out of order. We could be smarter
        // about this (e.g. by inspecting propositions that we know we
        // are decomposing and will need to deal with further up) but
        // it would probably be better to just make a better ordering.
        // (We don't even care what the ordering is, as long as it
        // puts equal props together so that we can dedup or uncombine
        // them).
        auto ord = chute1.prop <=> chute2.prop;

        if (// Props out of order?
            ord == std::strong_ordering::greater ||
            // Same prop but abnormal bit order?
            (ord == std::strong_ordering::equal &&
             chute1.type == CType::ONE &&
             chute2.type == CType::ZERO)) {
          chute1.desire = DesireType::EXCHANGE_RIGHT;
          chute2.desire = DesireType::EXCHANGE_LEFT;
        }
      }
    }

    // If no desire yet (e.g. already in order), just stay put.
    for (int c = 0; c < chutes.size(); c++) {
      Chute &chute = chutes[c];
      if (done[c]) {
        CHECK(chute.desire != DesireType::UNSPECIFIED);
      } else if (chute.desire == DesireType::UNSPECIFIED) {
        chute.desire = DesireType::FLOW;
      }
    }

    // PERF: Adjust the amount of flow for the exterior, with the
    // delta according to some estimate of how much space we need in
    // the middle (could be negative even). For now they just flow
    // outward, making more and more space (which is probably fine
    // but untidy).

    // Now greedily place, without blocking anything off.

    std::vector<PC> next_cells;
    std::vector<bool> assigned(chutes.size(), false);

    // Try to place the specified gate so its single output aligns
    // with this chute (also trying its flipped version). The
    // inprops should reflect the input propositions for that cell
    // in its unflipped orientation.
    // Updates next_cells and assigned if successful and returns
    // true. Otherwise, makes no change and returns false.
    auto PlaceAlignedUnary =
      [&](int chute_idx,
          Gate g,
          std::span<const Prop> inprops,
          int cell_val = 0) -> bool {
        CHECK(!assigned[chute_idx]);
        Chute &chute = chutes[chute_idx];

        for (bool flip : {false, true}) {
          Cell cell(g, cell_val, flip);
          int xout = ItsOutputPos(cell);

          int cell_pos = chute.pos - xout;
          if (CanPlaceCell(chutes, next_cells, cell, cell_pos)) {
            assigned[chute_idx] = true;
            std::vector<Prop> ip(inprops.begin(), inprops.end());
            if (flip) {
              VectorReverse(&ip);
            }
            next_cells.push_back(PC{
                .xpos = cell_pos,
                .cell = cell,
                .inprops = std::move(ip),
              });
            return true;
          }
        }

        return false;
      };

    // Try placing a cell with two outputs atop chute_idx and chute_idx+1.
    // Since when we flip a binary gate we may modify its meaning, we
    // take a gate and a flipped_gate.
    //
    // If neither the gate or its flipped version fit, then we modify
    // the desire field for the chutes to be FLOW, and pick relative
    // offsets that would align the chutes with this gate's output.
    // The DoFlow pass will then use wires. (There are multiple
    // choices for offsets here. Target the unflipped gate, and use a
    // 0 offset for the left chute.)
    auto PlaceBinaryOrFallback = [&](int chute_idx,
                                     Gate gate, Gate flipped_gate) {
        CHECK(chute_idx < chutes.size() - 1);
        CHECK(!assigned[chute_idx]);
        CHECK(!assigned[chute_idx + 1]);

        Chute &chute1 = chutes[chute_idx];
        Chute &chute2 = chutes[chute_idx + 1];

        for (bool flip : {false, true}) {
          Gate g = flip ? flipped_gate : gate;
          Cell cell(g, 0, flip);
          CellLibrary::Info info = library.GetInfo(cell);
          CHECK(info.outputs.size() == 2);

          int out0 = info.outputs[0].xblock;
          int out1 = info.outputs[1].xblock;

          int cell_pos = chute1.pos - out0;
          if (cell_pos + out1 == chute2.pos) {
            if (CanPlaceCell(chutes, next_cells, cell, cell_pos)) {
              assigned[chute_idx] = true;
              assigned[chute_idx + 1] = true;

              // XXX Maybe would be cleaner to pass this?
              std::vector<Prop> inprops;
              if (g == DUP0 || g == DUP1 || g == SEPARATOR) {
                inprops = {chute1.prop};
              } else {
                CHECK(g == SELFXCHG01 ||
                      g == SELFXCHG10 ||
                      g == XCHG00 ||
                      g == XCHG01 ||
                      g == XCHG10 ||
                      g == XCHG11);

                // All exchange gates swap their inputs.
                inprops = {chute2.prop, chute1.prop};
              }
              next_cells.push_back(PC{
                  .xpos = cell_pos,
                  .cell = cell,
                  .inprops = std::move(inprops),
                });
              return;
            }
          }
        }

        Cell cell_unflipped(gate, 0, false);
        CellLibrary::Info info = library.GetInfo(cell_unflipped);
        CHECK(info.outputs.size() == 2);

        chute1.desire = DesireType::FLOW;
        chute1.desire_val = 0;

        chute2.desire = DesireType::FLOW;
        chute2.desire_val = (chute1.pos - info.outputs[0].xblock +
                             info.outputs[1].xblock) - chute2.pos;
      };


    auto DoUndup = [&](int c) {
        // On the left one of a pair.
        if (c + 1 >= chutes.size())
          return;
        if (assigned[c] || assigned[c + 1]) return;

        Chute &chute1 = chutes[c];
        Chute &chute2 = chutes[c + 1];

        if (chute1.desire != DesireType::UNDUP ||
            chute2.desire != DesireType::UNDUP)
          return;

        if (chute1.type == CType::ZERO) {
          PlaceBinaryOrFallback(c, DUP0, DUP0);
        } else {
          CHECK(chute1.type == CType::ONE);
          PlaceBinaryOrFallback(c, DUP1, DUP1);
        }
      };

    auto DoDecompose = [&](int c) {
        Chute &chute = chutes[c];
        if (chute.desire != DesireType::DECOMPOSE) {
          return;
        }

        if (chute.type == CType::MIXED) {
          const Binop* b = std::get_if<Binop>(&chute.prop.p);
          CHECK(b && b->op == BinopOp::AND);
          if (PlaceAlignedUnary(c, AND0110,
                                Span{*b->a, *b->a, *b->b, *b->b})) {
            return;
          }

        } else {
          // NOT0 takes a separated 0 as input, and outputs a separated 1.
          // So note we are switching on the output type here.
          Gate g = chute.type == CType::ZERO ? NOT1 : NOT0;
          const Unop* u = std::get_if<Unop>(&chute.prop.p);
          CHECK(u && u->op == UnopOp::NOT);
          if (PlaceAlignedUnary(c, g, Span{*u->a})) {
            return;
          }
        }

        chute.desire = DesireType::FLOW;
        chute.desire_val = 0;
      };

    auto DoUncombine = [&](int c) {
        Chute &chute = chutes[c];
        if (chute.desire != DesireType::UNCOMBINE) {
          return;
        }

        if (PlaceAlignedUnary(c, COMBINE01, Span{chute.prop, chute.prop}) ||
            PlaceAlignedUnary(c, COMBINE10, Span{chute.prop, chute.prop})) {
          return;
        }

        chute.desire = DesireType::FLOW;
        chute.desire_val = 0;

      };

    auto DoExchange = [&](int c) {
        // On the left one of a pair.
        if (c + 1 >= chutes.size())
          return;
        if (assigned[c] || assigned[c + 1]) return;

        Chute &chute1 = chutes[c];
        Chute &chute2 = chutes[c + 1];

        if (chute1.desire != DesireType::EXCHANGE_RIGHT ||
            chute2.desire != DesireType::EXCHANGE_LEFT)
          return;

        // All gates swap their two inputs, which must be separated.
        CHECK(chute1.type != CType::MIXED &&
              chute2.type != CType::MIXED);

        // The 0 and 1 in the name indicates the left and right input,
        // respectively.

        if (chute1.prop == chute2.prop) {
          // Prefer self-exchange, which is a simpler gate
          // because we know there will be exactly one object.
          CHECK(chute1.type != chute2.type) << "We could do it, "
            "but it doesn't make much sense to swap two identical "
            "chutes!";
          Gate g = SELFXCHG01, ginv = SELFXCHG10;
          // Remember, the numbers in the name are the input types.
          if (chute1.type == CType::ZERO)
            std::swap(g, ginv);

          PlaceBinaryOrFallback(c, g, ginv);
        } else {
          // Otherwise, use one of the four general-purpose exchange
          // gates as appropriate.

          if (chute1.type == chute2.type) {
            if (chute1.type == CType::ZERO) {
              PlaceBinaryOrFallback(c, XCHG00, XCHG00);
            } else {
              CHECK(chute1.type == CType::ONE);
              PlaceBinaryOrFallback(c, XCHG11, XCHG11);
            }
          } else {
            if (chute1.type == CType::ZERO) {
              CHECK(chute2.type == CType::ONE);
              PlaceBinaryOrFallback(c, XCHG10, XCHG01);
            } else {
              CHECK(chute1.type == CType::ONE);
              CHECK(chute2.type == CType::ZERO);
              PlaceBinaryOrFallback(c, XCHG01, XCHG10);
            }
          }
        }
      };

    auto DoFlow = [&](int c) {
        Chute &chute = chutes[c];

        CHECK(chute.desire == DesireType::FLOW) << "Bug: " <<
          DesireTypeString(chute.desire) << " should be handled "
          "above, perhaps by turning it into FLOW!";

        if (chute.desire_val > 0) {
          int displacement = chute.desire_val;
          // A gates have their output to the left of their input,
          // so they are positive when working bottom-up.
          Gate g = chute.type == CType::MIXED ? WIREA :
            chute.type == CType::ZERO ? WIRE0A : WIRE1A;

          for (int exponent = CellLibrary::MAX_WIRE_EXP;
               exponent >= 0;
               exponent--) {
            int amount = 1 << exponent;
            if (amount <= displacement) {
              if (PlaceAlignedUnary(c, g, Span{chute.prop}, -amount)) {
                return;
              }
            }
          }

        } else if (chute.desire_val < 0) {
          int displacement = -chute.desire_val;

          Gate g = chute.type == CType::MIXED ? WIREB :
            chute.type == CType::ZERO ? WIRE0B : WIRE1B;
          for (int exponent = CellLibrary::MAX_WIRE_EXP;
               exponent >= 0;
               exponent--) {
            int amount = 1 << exponent;
            if (amount <= displacement) {
              if (PlaceAlignedUnary(c, g, Span{chute.prop}, amount)) {
                return;
              }
            }
          }
        }

        // Fall through so that we try all wire types
        // for zero displacement.
        Gate ga = chute.type == CType::MIXED ? WIREA :
            chute.type == CType::ZERO ? WIRE0A : WIRE1A;
        Gate gb = chute.type == CType::MIXED ? WIREB :
          chute.type == CType::ZERO ? WIRE0B : WIRE1B;

        CHECK(chute.desire_val == 0);
        if (PlaceAlignedUnary(c, ga, Span{chute.prop}, 0))
          return;
        CHECK(PlaceAlignedUnary(c, gb, Span{chute.prop}, 0)) <<
          "We should always have space remaining to place a "
          "0-displacement wire.";
      };

    // Now do the passes above in priority order.

    auto ForAllRemaining = [&](auto f) {
        for (int c = 0; c < chutes.size(); c++)
          if (!assigned[c])
            f(c);
      };

    // These make clear progress to to the final state.
    ForAllRemaining(DoUndup);
    ForAllRemaining(DoDecompose);

    // Changes desires locally by introducing new separated
    // chutes.
    ForAllRemaining(DoUncombine);
    // Progress towards the above possibilities.
    ForAllRemaining(DoExchange);
    // Fallback, or at best setting up the positions for one
    // of the above. This needs to be last since earlier
    // passes will change the desire into flow.
    ForAllRemaining(DoFlow);

    // Now convert the placed cells into a flattened vector
    // of LC and a starting offset (might be negative).
    std::sort(next_cells.begin(), next_cells.end(),
              [](const PC &a, const PC &b) {
                return a.xpos < b.xpos;
              });

    std::vector<LC> next_layer;
    int start_pos = next_cells.empty() ? 0 : next_cells[0].xpos;
    int current_x = start_pos;

    for (PC &pc : next_cells) {
      if (pc.xpos > current_x) {
        next_layer.push_back(LC{
            .inprops = {},
            .cell = CellLibrary::Spacer(pc.xpos - current_x),
        });
      }
      current_x = pc.xpos + library.GetInfo(pc.cell).block_width;
      next_layer.push_back(LC{
          .inprops = std::move(pc.inprops),
          .cell = pc.cell,
      });
    }

    return {std::move(next_layer), start_pos};
  }

  #if 0
  // TODO: Make some kind of
  ImageRGBA DebugRender() {

  }
  #endif

  // We work bottom-up. The goal is to add layers so that we simplify
  // the inputs, until they're all variables.
  Layout Run(std::span<const Prop> props_in) {
    // I only support the AND binary gate today, so normalize to
    // a form that removes OR, XOR, etc.
    std::vector<Prop> props = VectorMap(props_in, NormalizeToAnd);

    // All the layers, annotated with props. We'll add to the front
    // of this.
    std::deque<std::vector<LC>> layers;

    // First create a layer with just wires to get us started. This is
    // probably not necessary, but it makes it easier to reason about
    // the steady state and makes sure that we don't get an empty
    // circuit.

    {
      std::vector<LC> final_layer;
      for (int i = 0; i < props.size(); i++) {
        LC lc{
          .inprops = {props[i]},
          .cell = CellLibrary::WireB(0),
        };
        final_layer.push_back(lc);
      }
      layers.push_front(std::move(final_layer));
    }

    // HERE.
    // Repeatedly take the front of the layers, and simplify.
    for (;;) {
      CHECK(!layers.empty());
      const std::vector<LC> &last = layers.front();

      // If it's all variables, then we are done!
      if (std::optional<std::vector<int>> ovars = AllVars(last)) {
        Layout ret;
        ret.input_vars = std::move(ovars.value());
        ret.circuit.layers.reserve(layers.size());
        for (std::vector<LC> &layout_layer : layers) {
          std::vector<Cell> layer;
          layer.reserve(layout_layer.size());
          for (LC &lc : layout_layer) {
            layer.push_back(std::move(lc.cell));
          }
          ret.circuit.layers.push_back(std::move(layer));
        }
        return ret;
      }

      // Otherwise, compute a new top layer.
      auto [next, start_pos] = AddLayer(last);

      // We might need to shift over this layer, or
      // shift over all the remaining ones, to align.
      if (start_pos > 0) {
        next.insert(next.begin(), LC{
            .inprops = {},
            .cell = CellLibrary::Spacer(start_pos),
        });
      } else if (start_pos < 0) {
        int pad = -start_pos;
        for (std::vector<LC> &layer : layers) {
          layer.insert(layer.begin(), LC{
              .inprops = {},
              .cell = CellLibrary::Spacer(pad),
          });
        }
      }
      layers.push_front(std::move(next));
    }
  }

  // Args must outlast the engine.
  LayoutEngine(const World &world, const CellLibrary &library) :
    world(world), library(library), min_clearance(ComputeMinClearance()) {}
};

Layout DoLayout(const CellLibrary &library,
                const World &world,
                std::span<const Prop> props) {
  LayoutEngine engine(world, library);
  return engine.Run(props);
}

