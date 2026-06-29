
#include "layout.h"

#include <algorithm>
#include <compare>
#include <cstdlib>
#include <deque>
#include <format>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "base/stringprintf.h"
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

using LC = LayoutEngine::LC;
using PC = LayoutEngine::PC;
using Chute = LayoutEngine::Chute;
using DesireType = LayoutEngine::DesireType;
using enum LayoutEngine::DesireType;

std::string_view LayoutEngine::DesireTypeString(DesireType dt) {
  switch (dt) {
  case UNSPECIFIED: return "UNSPECIFIED";
  case DECOMPOSE: return "DECOMPOSE";
  case UNCOMBINE: return "UNCOMBINE";
  case UNDUP: return "UNDUP";
  case UNSEPARATE: return "UNSEPARATE";
  case EXCHANGE_LEFT: return "EXCHANGE_LEFT";
  case EXCHANGE_RIGHT: return "EXCHANGE_RIGHT";
  case FLOW: return "FLOW";
  case QUIESCE: return "QUIESCE";
  default: return "??BAD DESIRETYPE??";
  }
}

std::string LayoutEngine::ChuteString(const Chute &chute) {
  std::string s;
  AppendFormat(&s, "Chute(pos={}, prop={}, type={}, desire={}, val={})",
               chute.pos,
               PropString(chute.prop),
               TypeString(chute.type),
               DesireTypeString(chute.desire),
               chute.desire_val);
  return s;
}



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

namespace {
struct LayoutEngineImpl : public LayoutEngine {
  const World &world;
  const CellLibrary &library;

  // Wires are asymmetric (even "vertical" wires have internal slopes
  // to prevent the objects from getting too fast). This is the very
  // minimum "close side" and "far side" clearance that we need in
  // order to guarantee that we can place some wire on an input.
  // Clearance does not include the width of the input itself. We use
  // this to check that we don't completely block a nearby input when
  // we place a cell. (We want to at least be able to propagate the
  // input upward with a wire.)
  int min_clearance_close = 0;
  int min_clearance_far = 0;

  int MinClearanceClose() const override { return min_clearance_close; }
  int MinClearanceFar() const override { return min_clearance_far; }

  // Returns the flattened vector of variable ids if all
  // of the inputs are variables; nullopt otherwise.
  std::optional<std::vector<int>>
  AllVars(std::span<const LC> lcs) {
    std::vector<int> vars;
    for (const LC &lc : lcs) {
      CellLibrary::Info info = library.GetInfo(lc.cell);
      for (size_t i = 0; i < lc.inprops.size(); i++) {
        if (info.inputs[i].type != CType::MIXED) {
          return std::nullopt;
        }
        const Prop &p = lc.inprops[i];
        if (const Var *v = std::get_if<Var>(&p.p)) {
          vars.push_back(v->id);
        } else {
          return std::nullopt;
        }
      }
    }
    return vars;
  }


  // Compute the minimum clearance to guarantee we can attach a wire;
  // initializes the close and far min_clearance values. (This could
  // probably just look at the small-valued wires, but we might as
  // well just be comprehensive.)
  void ComputeMinClearance() {
    int max_close = 0;
    int max_far = 0;

    // XXX Wire geometry doesn't really differ by type. We should
    // probably check this though?
    for (CType type : {CType::MIXED, CType::ZERO, CType::ONE}) {
      int best_close = 1e9;
      int best_far = 1e9;
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

            int close = std::min(left_clearance, right_clearance);
            int far = std::max(left_clearance, right_clearance);

            // Find the wire requiring the smallest close clearance.
            // If tied, pick the one with the smallest far clearance.
            if (close < best_close || (close == best_close && far < best_far)) {
              best_close = close;
              best_far = far;
            }
          }
        }
      }
      if (best_close > max_close) {
        max_close = best_close;
      }
      if (best_far > max_far) {
        max_far = best_far;
      }
    }

    min_clearance_close = max_close;
    min_clearance_far = max_far;
  }

  int ItsOutputPos(const Cell &cell) const {
    const CellLibrary::Info info = library.GetInfo(cell);
    CHECK(info.outputs.size() == 1);
    return info.outputs[0].xblock;
  }

  std::string DebugLayerState(
      std::optional<int> anchor,
      std::span<const Chute> chutes,
      std::span<const PC> next_cells) const {
    std::string s;
    AppendFormat(&s, "--- Chutes ---\n");
    for (int i = 0; i < (int)chutes.size(); i++) {
      std::string_view anch = (anchor.has_value() && anchor.value() == i) ?
        " (ANCHOR)" : "";
      AppendFormat(&s, " [{}] {}{}\n", i,
                   LayoutEngine::ChuteString(chutes[i]), anch);
    }
    AppendFormat(&s, "--- Next Cells ---\n");
    for (int i = 0; i < (int)next_cells.size(); i++) {
      AppendFormat(&s, " [{}] xpos={} cell={}\n", i, next_cells[i].xpos,
                   CellString(next_cells[i].cell));
    }
    return s;
  }

  // Given the input chutes for the complete top layer,
  // and the in-progress next layer (next), is it possible
  // to place the cell in the next layer with its left edge
  // at xpos? Needs to check:
  //  - It does not overlap anything already in that layer
  //  - It does not block off any chutes on the top layer
  //    (this does not include the chutes that match up
  //    to the cell's output, though!). Being blocked off
  //    is a non-trivial property: We can get close
  //    to an input as long as we have a lot of space on
  //    the other side, and that space can be populated
  //    without blocking further cells!
  bool CanPlaceCell(std::span<const Chute> top,
                    const std::vector<bool> &assigned,
                    std::span<const PC> next,
                    const Cell &cell,
                    int xpos) const override {
    CellLibrary::Info info = library.GetInfo(cell);
    int cell_left = xpos;
    int cell_right = xpos + info.block_width;

    // Overlapping something already placed in the next layer?
    for (const PC &pc : next) {
      int pc_left = pc.xpos;
      int pc_right = pc_left + library.GetInfo(pc.cell).block_width;
      if (cell_left < pc_right && cell_right > pc_left) {
        Print("Can't place {} at {}: Would overlap cell at x={}\n",
              CellString(cell), xpos, pc.xpos);
        return false;
      }
    }

    // Check if the inputs of the hypothetical cell are too close to the
    // inputs of already placed cells. If so, they would become stuck chutes
    // on the next layer.
    int min_input_dist = Levels::OUT_WIDTH + 2 * min_clearance_close;
    for (const CellLibrary::IO &in : info.inputs) {
      int in_pos = xpos + in.xblock;
      for (const PC &pc : next) {
        CellLibrary::Info pc_info = library.GetInfo(pc.cell);
        for (const CellLibrary::IO &pc_in : pc_info.inputs) {
          int pc_in_pos = pc.xpos + pc_in.xblock;
          if (std::abs(in_pos - pc_in_pos) < min_input_dist) {
            Print("Can't place {} at {}: "
                  "Input at {} is too close to already placed input at {}.\n",
                  CellString(cell), xpos, in_pos, pc_in_pos);
            return false;
          }
        }
      }
    }

    // Did we consume this chute with the hypothetical cell?
    // If so we don't need to check that it's blocked below.
    auto MatchedHere = [&](const Chute &chute) {
        for (const CellLibrary::IO &out : info.outputs) {
          if (xpos + out.xblock == chute.pos) {
            return true;
          }
        }
        return false;
      };

    // Memo tables for below. Is it known that we can place on this
    // smallest wire that's biased to the left?
    std::vector<std::optional<bool>> ok_left(top.size(), std::nullopt);
    // And symmetrically for the right.
    std::vector<std::optional<bool>> ok_right(top.size(), std::nullopt);

    // Check whether the chute still has space for a left-biased or
    // right-biased wire (assuming the hypothetical cell placed).
    std::function<bool(int, bool)> ChuteStillHasSpace =
      [&](int cidx, bool look_left) -> bool {
          std::optional<bool> &memo =
            look_left ? ok_left[cidx] : ok_right[cidx];
          if (memo.has_value()) return memo.value();

          // Break cycles by assuming true while evaluating. This is sound
          // because a cycle indicates self-consistent constraints, bounded
          // eventually by the fixed obstacles checked below.
          memo = true;

          int req_left = look_left ? min_clearance_close : min_clearance_far;
          int req_right = look_left ? min_clearance_far : min_clearance_close;
          const Chute &chute = top[cidx];

          int c_left = chute.pos - req_left;
          int c_right = chute.pos + Levels::OUT_WIDTH + req_right;

          // Check the hypothetical cell.
          if (cell_left < c_right && cell_right > c_left) {
            memo = {false};
            return false;
          }

          // Check already placed cells.
          for (const PC &pc : next) {
            int pc_left = pc.xpos;
            int pc_right = pc.xpos + library.GetInfo(pc.cell).block_width;
            if (pc_left < c_right && pc_right > c_left) {
              memo = {false};
              return false;
            }
          }

          {
            // Check left neighbor.
            int lidx = cidx - 1;
            if (lidx >= 0 && !assigned[lidx] && !MatchedHere(top[lidx])) {
              const Chute &p_chute = top[lidx];
              int dist = chute.pos - (p_chute.pos + Levels::OUT_WIDTH);
              bool left_safe = (dist >= req_left + min_clearance_far) ||
                ((dist >= req_left + min_clearance_close) &&
                 ChuteStillHasSpace(lidx, false));
              if (!left_safe) {
                memo = {false};
                return false;
              }
            }
          }

          {
            // Check right neighbor.
            int ridx = cidx + 1;
            if (ridx < (int)top.size() &&
                !assigned[ridx] &&
                !MatchedHere(top[ridx])) {
              const Chute &n_chute = top[ridx];
              int dist = n_chute.pos - (chute.pos + Levels::OUT_WIDTH);
              bool right_safe = (dist >= req_right + min_clearance_far) ||
                ((dist >= req_right + min_clearance_close) &&
                 ChuteStillHasSpace(ridx, true));
              if (!right_safe) {
                memo = {false};
                return false;
              }
            }
          }

          memo = {true};
          return true;
        };

    // Are we blocking a chute from the top layer?
    for (size_t i = 0; i < top.size(); i++) {
      if (!assigned[i]) {
        const Chute &chute = top[i];
        if (MatchedHere(chute))
          continue;

        if (!ChuteStillHasSpace(i, true) &&
            !ChuteStillHasSpace(i, false)) {
          Print("Can't place {} at {}: "
                "Cell {} would be blocked.\n",
                CellString(cell), xpos, i);
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

  // Generate the desired action for each chute by updating its
  // desire (and desire_val) fields in place.
  void SetChuteDesires(std::vector<Chute> &chutes) {

    // We often will need to reorder chutes. But we only need
    // to do this within the "interior," because contiguous
    // sequenes of mixed variables on the left and right side
    // are already done (the input layer can take variables in
    // any order). So start by identifying and marking chutes
    // that are done.

    // Left side.
    for (int c = 0; c < chutes.size(); c++) {
      Chute &chute = chutes[c];
      if (std::holds_alternative<Var>(chute.prop.p) &&
          chute.type == CType::MIXED) {
        // Still on the exterior.
        chute.done = true;
        chute.desire = DesireType::QUIESCE;
        // TODO: Adjust this later down once we know better how much
        // space we need.
        chute.desire_val = -8;
      } else {
        break;
      }
    }

    CHECK(!chutes.back().done) << "Precondition: The top layer is already "
      "complete!";

    // Also the right side.
    for (int c = (int)chutes.size() - 1; c >= 0; c--) {
      Chute &chute = chutes[c];
      if (std::holds_alternative<Var>(chute.prop.p) &&
          chute.type == CType::MIXED) {
        // Still on the exterior.
        chute.done = true;
        chute.desire = DesireType::QUIESCE;
        chute.desire_val = +8;
      } else {
        break;
      }
    }

    // Now a mixed variable that is not "done" is going to be
    // problematic, because we will likely need to cross over it to
    // attain the order we want. So uncombine those.
    for (int c = 0; c < chutes.size(); c++) {
      Chute &chute = chutes[c];
      if (!chute.done &&
          chute.type == CType::MIXED &&
          std::holds_alternative<Var>(chute.prop.p)) {
        CHECK(chute.desire == DesireType::UNSPECIFIED);
        chute.desire = UNCOMBINE;
      }
    }

    // TODO: We should UNDUP propositions that are equal and
    // already next to one another. We want to do this before
    // crossing over, because reducing the number of total
    // chutes is a big efficiency win.
    for (int c = 0; c < (int)chutes.size() - 1; c++) {
      Chute &chute1 = chutes[c];
      Chute &chute2 = chutes[c + 1];
      // When we have two separated inputs for the same
      // proposition in a row, we should UNDUP them.
      if (!chute1.done &&
          !chute2.done &&
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
        // Note that if we have an odd number of equal propositions,
        // the last one will be passed through and be in the correct
        // position to undup on the next layer.
      }
    }

    // Unseparate exterior pairs of separated variables. We do this
    // after UNDUP so that UNDUP has higher priority.
    for (int c = 0; c < (int)chutes.size() - 1; c++) {
      Chute &chute1 = chutes[c];
      Chute &chute2 = chutes[c + 1];

      if (!chute1.done && !chute2.done &&
          chute1.desire == UNSPECIFIED &&
          chute2.desire == UNSPECIFIED &&
          chute1.type != CType::MIXED &&
          chute2.type != CType::MIXED &&
          chute1.type != chute2.type &&
          chute1.prop == chute2.prop) {

        // Props are equal.
        const Prop &prop = chute1.prop;

        // For variables, we only want to unseparate them if they are
        // exterior, so they will become done. Unseparating prematurely
        // creates obstacles that prevent us from exchanging across.
        if (std::holds_alternative<Var>(prop.p)) {
          // TODO: We should allow unseparating multiple
          // pairs in the same layer.
          const bool left_exterior = c == 0 || chutes[c - 1].done;
          const bool right_exterior =
            c + 2 >= chutes.size() || chutes[c + 2].done;

          if (left_exterior || right_exterior) {
            chute1.desire = UNSEPARATE;
            chute2.desire = UNSEPARATE;
          }

        } else if (std::holds_alternative<Binop>(prop.p)) {

          // Our binops all target mixed outputs, so we need
          // to unseparate wherever this is. On the next
          // layer we should be able to decompose.
          chute1.desire = UNSEPARATE;
          chute2.desire = UNSEPARATE;
        }
      }
    }

    // Similarly, decomposing a mixed binary proposition gets us
    // separated inputs (which we want) as well as simplifying.
    for (int c = 0; c < chutes.size(); c++) {
      Chute &chute = chutes[c];
      if (!chute.done &&
          chute.type == CType::MIXED &&
          (std::holds_alternative<Binop>(chute.prop.p) ||
           std::holds_alternative<Value>(chute.prop.p))) {
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
      if (!chute.done &&
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
      if (!chute.done &&
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
      if (!chute1.done &&
          !chute2.done &&
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

    // If no desire yet (e.g. already in order), just quiesce.
    for (int c = 0; c < chutes.size(); c++) {
      Chute &chute = chutes[c];
      if (chute.done) {
        CHECK(chute.desire != DesireType::UNSPECIFIED);
      } else if (chute.desire == DesireType::UNSPECIFIED) {
        chute.desire = DesireType::QUIESCE;
      }
    }

    // PERF: Adjust the amount of flow for the exterior, with the
    // delta according to some estimate of how much space we need in
    // the middle (could be negative even). For now they just flow
    // outward, making more and more space (which is probably fine
    // but untidy).
  }


  // Given a top layer (annotated with the propositions it takes as
  // inputs), create a new layer that produces those layers and is
  // simpler. (Simpler as in some unspecified well-founded ordering
  // so that this process terminates.) The input and output layers
  // should start at x=0.
  std::pair<std::vector<LC>, int> AddLayer(std::span<const LC> top) {
    CHECK(!top.empty()) << "Precondition.";

    // First we flatten all of the inputs we need to satisfy on the
    // top layer, with their positions. These are just fixed and
    // independent since we aren't going to try to move them around.
    std::vector<Chute> chutes = FlattenInputs(top);
    CHECK(!chutes.empty());

    // Now set the desires for each.
    SetChuteDesires(chutes);

    Print(AWHITE("Chute desires") " before placement:\n");
    for (int i = 0; i < (int)chutes.size(); i++) {
      Print(" [{}] {}\n", i,
            LayoutEngine::ChuteString(chutes[i]));
    }
    Print("\n");


    // Now greedily place, without blocking anything off.

    std::vector<PC> next_cells;
    std::vector<bool> assigned(chutes.size(), false);

    // Sanity check: Ensure the input chutes are not already stuck before
    // we even place anything.
    {
      const int clear_pos = chutes.back().pos +
        Levels::IN_WIDTH + 2 * min_clearance_far + 1;
      if (!CanPlaceCell(chutes, assigned, next_cells,
                        CellLibrary::Spacer(1),
                        clear_pos)) {
        LOG(FATAL) << "Input chutes are already in a state where "
          "we're stuck!\n" <<
          DebugLayerState(std::nullopt, chutes, next_cells);
      }
    }

    static constexpr int FLEE_AMOUNT = 16;
    // As we try placing, we note weighted conflicts at chute
    // locations. This helps us with heuristic direction of
    // flow.
    std::vector<int> conflict_weight(chutes.size(), 0);
    // In order to ensure we make progress, the first goal in
    // priority order that is in the right position but doesn't
    // have space is allowed to anchor itself and just propagate
    // upward its chutes with zero displacement. Others will
    // move away from the anchor.
    std::optional<int> anchor;

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
          int cell_val = 0,
          std::initializer_list<bool> allow_flips = {false, true}) -> bool {
        CHECK(!assigned[chute_idx]);
        Chute &chute = chutes[chute_idx];

        for (bool flip : allow_flips) {
          Cell cell(g, cell_val, flip);
          int xout = ItsOutputPos(cell);

          int cell_pos = chute.pos - xout;
          if (CanPlaceCell(chutes, assigned, next_cells, cell, cell_pos)) {
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

        conflict_weight[chute_idx]++;

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
          CHECK(info.outputs.size() == 2) << CellString(cell)
                                          << "\nGot\n"
                                          << CellLibrary::InfoString(info);

          int out0 = info.outputs[0].xblock;
          int out1 = info.outputs[1].xblock;

          CHECK(info.outputs[0].type == chute1.type &&
                info.outputs[1].type == chute2.type);

          int cell_pos = chute1.pos - out0;
          if (cell_pos + out1 == chute2.pos) {
            // Correct relative position, but will the cell fit?
            if (CanPlaceCell(chutes, assigned, next_cells, cell, cell_pos)) {
              assigned[chute_idx] = true;
              assigned[chute_idx + 1] = true;

              // XXX Maybe would be cleaner to pass this?
              std::vector<Prop> inprops;
              if (g == DUP0 || g == DUP1 ||
                  g == SEPARATOR01 || g == SEPARATOR10) {
                inprops = {chute1.prop};
              } else if (g == SELFXCHG01 ||
                         g == SELFXCHG10 ||
                         g == XCHG00 ||
                         g == XCHG01 ||
                         g == XCHG10 ||
                         g == XCHG11) {

                // All exchange gates swap their inputs.
                inprops = {chute2.prop, chute1.prop};

              } else {
                LOG(FATAL) << "Unhandled gate in PlaceBinary: "
                           << GateString(g);
              }
              next_cells.push_back(PC{
                  .xpos = cell_pos,
                  .cell = cell,
                  .inprops = std::move(inprops),
                });
              return;
            } else {
              // Couldn't place a binary gate even though the inputs
              // are already in the right spot. We treat this as a
              // more serious conflict, since these gates are harder
              // to set up.
              conflict_weight[chute_idx] += 2;
              conflict_weight[chute_idx + 1] += 2;
            }
          }
        }

        Cell cell_unflipped(gate, 0, false);
        CellLibrary::Info info = library.GetInfo(cell_unflipped);
        CHECK(info.outputs.size() == 2);

        int current_dist = chute2.pos - chute1.pos;
        int target_dist = info.outputs[1].xblock - info.outputs[0].xblock;

        Print("Chute {}: "
              "PlaceBinary ({}/{}) fallback.\n"
              "Current dist {}, target dist {}.\n",
              chute_idx, GateString(gate), GateString(flipped_gate),
              current_dist, target_dist);

        if (current_dist == target_dist) {
          if (!anchor.has_value()) {
            anchor = {chute_idx};
            // Propagate upward.
            chute1.desire = DesireType::FLOW;
            chute1.desire_val = 0;
            chute2.desire = DesireType::FLOW;
            chute2.desire_val = 0;
          } else {
            // Propagate in tandem away from the
            // anchor.
            int disp =
              (chute_idx < anchor.value()) ?
              -FLEE_AMOUNT : FLEE_AMOUNT;

            chute1.desire = DesireType::QUIESCE;
            chute1.desire_val = disp;
            chute2.desire = DesireType::QUIESCE;
            chute2.desire_val = disp;
          }

        } else if (current_dist > target_dist) {
          chute2.desire = DesireType::QUIESCE;
          chute2.desire_val = 0;
          chute1.desire = DesireType::QUIESCE;
          chute1.desire_val = (chute2.pos - info.outputs[1].xblock +
                               info.outputs[0].xblock) - chute1.pos;
        } else {
          chute1.desire = DesireType::QUIESCE;
          chute1.desire_val = 0;
          chute2.desire = DesireType::QUIESCE;
          chute2.desire_val = (chute1.pos - info.outputs[0].xblock +
                               info.outputs[1].xblock) - chute2.pos;
        }
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

    auto DoUnseparate = [&](int c) {
        // On the left one of a pair.
        if (c + 1 >= chutes.size()) return;
        if (assigned[c] || assigned[c + 1]) return;

        Chute &chute1 = chutes[c];
        Chute &chute2 = chutes[c + 1];

        if (chute1.desire != DesireType::UNSEPARATE ||
            chute2.desire != DesireType::UNSEPARATE)
          return;

        CHECK(chute1.type != chute2.type);

        // Here, 0 and 1 refer to the output types.
        Gate g = SEPARATOR01, ginv = SEPARATOR10;
        if (chute1.type == CType::ONE) std::swap(g, ginv);

        PlaceBinaryOrFallback(c, g, ginv);
      };

    auto DoDecompose = [&](int c) {
        Chute &chute = chutes[c];
        if (chute.desire != DesireType::DECOMPOSE) {
          return;
        }

        if (chute.type == CType::MIXED) {
          if (const Value *v = std::get_if<Value>(&chute.prop.p)) {
            Gate g = v->value ? CONST1 : CONST0;
            if (PlaceAlignedUnary(c, g, {})) {
              return;
            }
          } else {
            const Binop *b = std::get_if<Binop>(&chute.prop.p);
            CHECK(b && b->op == BinopOp::AND);
            if (PlaceAlignedUnary(c, AND0110,
                                  Span{*b->a, *b->a, *b->b, *b->b})) {
              return;
            }
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

        chute.desire = DesireType::QUIESCE;
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

        chute.desire = DesireType::QUIESCE;
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

        CHECK(chute.desire == DesireType::FLOW ||
              chute.desire == DesireType::QUIESCE) << "Bug: " <<
          LayoutEngine::DesireTypeString(chute.desire) << " should be "
          "handled above, perhaps by turning it into FLOW!";

        Print("Chute {} ({}) DoFlow: desire_val is {}.\n",
              c, LayoutEngine::DesireTypeString(chute.desire),
              chute.desire_val);

        // PERF: We could compute this cumulative sum outside. But
        // we probably want it to be weighted somehow.
        if (chute.desire == DesireType::QUIESCE) {
          // If quiesce, move "outward" from conflict.
          int left_conflicts = 0;
          for (int i = 0; i < c; i++) {
            left_conflicts += conflict_weight[i];
          }

          int right_conflicts = 0;
          for (int i = c + 1; i < (int)chutes.size(); i++) {
            right_conflicts += conflict_weight[i];
          }

          if (left_conflicts > right_conflicts) {
            chute.desire_val = FLEE_AMOUNT;
          } else if (right_conflicts > left_conflicts) {
            chute.desire_val = -FLEE_AMOUNT;
          }
        }

        if (chute.desire_val != 0) {
          bool flip = chute.desire_val > 0;
          int displacement = flip ? chute.desire_val : -chute.desire_val;

          // Both A and B wires slant down to the right (like a backslash;
          // the output is to the right of the input). If we want the
          // opposite slant we do that by flipping. The way they differ
          // is in their bias (is the larger side of the cell to the right,
          // or to the left?).
          Gate ga = chute.type == CType::MIXED ? WIREA :
            chute.type == CType::ZERO ? WIRE0A : WIRE1A;
          Gate gb = chute.type == CType::MIXED ? WIREB :
            chute.type == CType::ZERO ? WIRE0B : WIRE1B;

          for (int exponent = CellLibrary::MAX_WIRE_EXP;
               exponent >= 0;
               exponent--) {
            int amount = 1 << exponent;
            if (amount <= displacement) {
              if (PlaceAlignedUnary(
                      c, ga, Span{chute.prop}, amount, {flip})) {
                return;
              }
              if (PlaceAlignedUnary(
                      c, gb, Span{chute.prop}, amount, {flip})) {
                return;
              }
            }
          }
        }

        // Either the desired displacement was zero or we
        // weren't able to get any displacement in the correct
        // direction (e.g. because it's too crowded). Make a
        // vertical wire.
        int displacement = 0;

        // Fall through so that we try all wire types
        // for zero displacement.
        Gate ga = chute.type == CType::MIXED ? WIREA :
          chute.type == CType::ZERO ? WIRE0A : WIRE1A;
        Gate gb = chute.type == CType::MIXED ? WIREB :
          chute.type == CType::ZERO ? WIRE0B : WIRE1B;

        if (chute.desire_val != 0) {
          Print("Chute {} " AORANGE("fell back")
                " to displacement 0 wire (wanted {})!\n",
                c, chute.desire_val);
        }

        if (PlaceAlignedUnary(c, ga, Span{chute.prop}, displacement))
          return;
        if (PlaceAlignedUnary(c, gb, Span{chute.prop}, displacement))
          return;

        LOG(FATAL) <<
          "We should always have space remaining to place a "
          "0-displacement wire. (Originally wanted " <<
          chute.desire_val << ").\nState:\n" <<
          DebugLayerState(anchor, chutes, next_cells);
      };

    // Now do the passes above in priority order.

    auto ForAllRemaining = [&](auto f) {
        for (int c = 0; c < chutes.size(); c++)
          if (!assigned[c])
            f(c);
      };

    // These make clear progress to the final state.
    ForAllRemaining(DoUndup);
    ForAllRemaining(DoUnseparate);
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

    Print(AWHITE("Layer state at end") ":\n"
          "{}\n",
          DebugLayerState(anchor, chutes, next_cells));

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
  Layout DoLayout(std::span<const Prop> props_in) override {
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

    // Repeatedly take the front of the layers, and simplify.
    for (;;) {
      CHECK(!layers.empty());
      const std::vector<LC> &last = layers.front();

      // If it's all variables, then we are done!
      if (std::optional<std::vector<int>> ovars = AllVars(last)) {
        return Finalize(std::move(layers),
                        std::move(ovars.value()));
      }

      // Otherwise, compute a new top layer.
      auto [next, start_pos] = AddLayer(last);

      // True if the layers are effectively the same, ignoring leading
      // spacers (i.e. they can have different starting offsets). If
      // we have two such layers in a row, then we will just get in
      // an infinite loop, so we should abort.
      auto SameLayer = [](const std::vector<LC>& a,
                          const std::vector<LC>& b) {
        auto ita = a.begin(), itb = b.begin();
        while (ita != a.end() && ita->cell.gate == Gate::SPACER) ita++;
        while (itb != b.end() && itb->cell.gate == Gate::SPACER) itb++;
        while (true) {
          if (ita == a.end() && itb == b.end()) return true;
          if (ita == a.end() || itb == b.end()) return false;
          if (ita->cell.gate != itb->cell.gate ||
              ita->cell.v != itb->cell.v ||
              ita->cell.flip != itb->cell.flip ||
              ita->inprops != itb->inprops) {
            return false;
          }
          ita++;
          itb++;
        }
      };

      if (SameLayer(last, next)) {
        LOG(FATAL) << "Layout made no progress! New layer is identical\n"
          "to the previous one.";
      }

      // We might need to shift over this layer, or
      // shift over all the remaining ones, to align.
      auto AddLeftSpacer = [](std::vector<LC> &layer, int pad) {
        if (pad == 0) return;
        CHECK(pad > 0);
        if (!layer.empty() && layer.front().cell.gate == Gate::SPACER) {
          layer.front().cell.v += pad;
        } else {
          layer.insert(layer.begin(), LC{
              .inprops = {},
              .cell = CellLibrary::Spacer(pad),
          });
        }
      };

      if (start_pos > 0) {
        AddLeftSpacer(next, start_pos);
      } else if (start_pos < 0) {
        int pad = -start_pos;
        for (std::vector<LC> &layer : layers) {
          AddLeftSpacer(layer, pad);
        }
      }
      layers.push_front(std::move(next));
    }
  }

  // Delete unnecessary left/right padding, and convert to the Layout
  // format.
  Layout Finalize(std::deque<std::vector<LC>> layers,
                  std::vector<int> vars) {
    int min_left = 0;
    int min_right = 0;
    for (const std::vector<LC> &layer : layers) {
      int left = (!layer.empty() && layer.front().cell.gate == Gate::SPACER)
        ? layer.front().cell.v : 0;
      min_left = std::min(min_left, left);

      int right = (layer.size() > 1 && layer.back().cell.gate == Gate::SPACER)
        ? layer.back().cell.v : 0;
      min_right = std::min(min_right, right);
    }

    if (min_left > 0) {
      for (std::vector<LC> &layer : layers) {
        layer.front().cell.v -= min_left;
        if (layer.front().cell.v == 0) {
          layer.erase(layer.begin());
        }
      }
    }

    if (min_right > 0) {
      for (std::vector<LC> &layer : layers) {
        if (!layer.empty()) {
          layer.back().cell.v -= min_right;
          if (layer.back().cell.v == 0) {
            layer.pop_back();
          }
        }
      }
    }

    Layout ret;
    ret.input_vars = std::move(vars);
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

  // Args must outlast the engine.
  LayoutEngineImpl(const CellLibrary &library, const World &world) :
    world(world), library(library) {
    ComputeMinClearance();
    Print("Min clearance: close={}, far={}\n", min_clearance_close,
          min_clearance_far);
  }
};

}  // namespace

std::unique_ptr<LayoutEngine> LayoutEngine::Create(
    const CellLibrary &library, const World &world) {
  return std::make_unique<LayoutEngineImpl>(library, world);
}

LayoutEngine::LayoutEngine() {}
LayoutEngine::~LayoutEngine() {}


