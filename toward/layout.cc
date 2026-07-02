
#include "layout.h"

#include <algorithm>
#include <compare>
#include <cstdlib>
#include <deque>
#include <format>
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
#include "auto-histo.h"
#include "base/logging.h"
#include "base/print.h"
#include "base/stringprintf.h"
#include "cell-library.h"
#include "circuit.h"
#include "image.h"
#include "layout-canvas.h"
#include "level.h"
#include "prop.h"
#include "render-circuit.h"
#include "span-util.h"
#include "status-bar.h"
#include "timer.h"
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
//   The generated file cell-library.txt has the actual dimensions and
//   I/O locations for the cells.
//
// level.h (Levels):
//   Defines the 2D physics geometry (Level, LevelBody, polygon meshes).
//   While LayoutEngine mainly works with abstract Cells, the CellLibrary
//   loads the underlying Level geometry from SVGs. The final Circuit is
//   ultimately composed into a large Level for the physics engine to
//   simulate the marbles (1s and 0s) rolling through it.
//
// layout-canvas.h:
//   Data structure and utilities for managing the top of the circuit
//   and the in-progress next layer, used during this layout algorithm.
// ----------------------------------------------------------------------

using LC = LayoutCanvas::LC;
using PC = LayoutCanvas::PC;
using Chute = LayoutCanvas::Chute;
using DesireType = LayoutCanvas::DesireType;
using enum LayoutCanvas::DesireType;

static constexpr bool WRITE_IMAGES = false;

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
// propositions; we can directly strip ¬, create constants,
// and postpone variables until the end.) The tricky thing about
// *this* is that we can only reorder inputs when they are separated.

namespace {
struct LayoutEngineImpl : public LayoutEngine {
  const World &world;
  const CellLibrary &library;

  // The amount of space we try to keep between the done region
  // (chutes on the far left and right that contain MIXED vars)
  // and the interior.
  static constexpr int DONE_GAP = 256;

  int num_and = 0, num_xchg = 0, num_or = 0, num_wire = 0;
  int num_sep = 0, num_dup = 0, num_comb = 0;

  std::unique_ptr<StatusBar> status;

  int num_spaced_layers = 0;

  int verbose = 1;

  void SetVerbose(int v) override { verbose = v; }

  // The closest we ever need outputs to be for a single cell (blocks
  // between right edge and left edge). There's no reason for chutes
  // to be closer than this, so we push them apart to avoid getting
  // stuck.
  int min_output_distance = 0;
  // The largest cell width (not including wires).
  int max_cell_width = 0;

  template<typename... Args>
  inline void Print(std::format_string<Args...> fmt, Args &&...args) const {
    if (status.get() != nullptr) {
      status->Print(fmt, std::forward<Args>(args)...);
    } else {
      ::Print(fmt, std::forward<Args>(args)...);
    }
  }

  // Returns the flattened vector of variable ids if all
  // of the inputs are variables; nullopt otherwise.
  std::optional<std::vector<std::pair<int, CType>>>
  AllVars(std::span<const LC> lcs) override {
    std::vector<std::pair<int, CType>> vars;
    for (const LC &lc : lcs) {
      CellLibrary::Info info = library.GetInfo(lc.cell);
      for (size_t i = 0; i < lc.inprops.size(); i++) {
        /*
          // XXX should put this back, but it's currently possible
          // for a solo separated wire to persist to the end.
          // We could add cleanup layers at the end.
        if (info.inputs[i].type != CType::MIXED) {
          return std::nullopt;
        }
        */
        const Prop &p = lc.inprops[i];
        if (const Var *v = std::get_if<Var>(&p.p)) {
          vars.emplace_back(v->id, info.inputs[i].type);
        } else {
          return std::nullopt;
        }
      }
    }
    return vars;
  }

  void ComputeMinOutputDistance() {
    // All the non-unary gates we use in the layout algorithm:
    static constexpr std::initializer_list<Gate> USED = {
      SEPARATOR01,
      SEPARATOR10,
      SELFXCHG01,
      SELFXCHG10,
      XCHG00,
      XCHG01,
      XCHG10,
      XCHG11,
      DUP0,
      DUP1,
    };

    std::optional<int> min_dist = std::nullopt;
    Gate min_gate = SPACER;

    for (const Gate g : USED) {
      // Flipping cannot change inter-output distance.
      Cell cell(g);
      CellLibrary::Info info = library.GetInfo(cell);
      CHECK(info.outputs.size() > 1) << "expected non-unary gate!";
      for (int i = 0; i < info.outputs.size() - 1; i++) {
        int dist = info.outputs[i + 1].xblock - info.outputs[i].xblock -
          Levels::OUT_WIDTH;
        CHECK(dist >= 0) << "Bad cell " << CellString(cell);
        if (!min_dist.has_value() || dist < min_dist.value()) {
          min_gate = g;
          min_dist = {dist};
        }
      }
    }

    CHECK(min_dist.has_value());
    min_output_distance = min_dist.value();

    if (verbose > 0) {
      Print("Minimum output dist is for {}: {}\n",
            GateString(min_gate), min_output_distance);
    }
  }

  void ComputeMaxCellWidth() {
    // Could perhaps limit ourselves only to gates we actually use.
    max_cell_width = 0;
    for (const Gate g : ALL_GATES) {
      if (g == SPACER) continue;
      // Flipping cannot change width.
      Cell cell(g);
      CellLibrary::Info info = library.GetInfo(cell);
      max_cell_width = std::max(max_cell_width, info.block_width);
    }
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
        AGREEN(" (ANCHOR)") : "";
      AppendFormat(&s, " [{}] {}{}\n", i,
                   LayoutCanvas::ChuteString(chutes[i]), anch);
    }
    AppendFormat(&s, "--- Next Cells ---\n");
    for (int i = 0; i < (int)next_cells.size(); i++) {
      AppendFormat(&s, " [{}] xpos={} cell={}\n", i, next_cells[i].xpos,
                   CellString(next_cells[i].cell));
    }
    return s;
  }

  // Generate the desired action for each chute by updating its
  // desire (and desire_val) fields in place.
  void SetChuteDesires(std::vector<Chute> &chutes) {

    // We often will need to reorder chutes. But we only need
    // to do this within the "interior," because contiguous
    // sequences of mixed variables on the left and right side
    // are already done (the input layer can take variables in
    // any order). So start by identifying and marking chutes
    // that are done.

    int first_interior_chute = -1;
    int last_interior_chute = 0;
    // Left side.
    for (int c = 0; c < chutes.size(); c++) {
      Chute &chute = chutes[c];
      if (!std::holds_alternative<Var>(chute.prop.p) ||
          chute.type != CType::MIXED) {
        first_interior_chute = c;
        break;
      }
    }

    // Also the right side.
    for (int c = (int)chutes.size() - 1; c >= 0; c--) {
      Chute &chute = chutes[c];
      if (!std::holds_alternative<Var>(chute.prop.p) ||
          chute.type != CType::MIXED) {
        last_interior_chute = c;
        break;
      }
    }

    CHECK(first_interior_chute >= 0 &&
          last_interior_chute >= 0) << "Precondition is that we're "
      "not already done!";


    // We want to move the exterior away from the action, but not
    // indefinitely; it's just sloppy to have really big wings.

    // XXX: This bakes in an assumption about small wires.
    constexpr int DONE_SPACING = 32;
    if (first_interior_chute > 0) {
      int interior_pos = chutes[first_interior_chute].pos;

      for (int c = 0; c < first_interior_chute; c++) {
        // How far into the exterior we are.
        int count = first_interior_chute - c;
        // The displacement for the last exterior chute.
        int target_pos = interior_pos - DONE_GAP -
          DONE_SPACING * count;

        int disp = target_pos - chutes[c].pos;
        Chute &chute = chutes[c];
        chute.done = true;
        chute.desire = DesireType::QUIESCE;
        chute.desire_val = disp;
      }
    }

    if (last_interior_chute < (int)chutes.size() - 1) {
      int interior_pos = chutes[last_interior_chute].pos;
      for (int c = last_interior_chute + 1; c < chutes.size(); c++) {
        int count = c - last_interior_chute;
        int target_pos = interior_pos + DONE_GAP + DONE_SPACING * count;

        int disp = target_pos - chutes[c].pos;
        Chute &chute = chutes[c];
        chute.done = true;
        chute.desire = DesireType::QUIESCE;
        chute.desire_val = disp;
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

  // An island is a contiguous sequence of chutes that might interfere
  // on this layer. We separate the chutes into islands that are far
  // enough apart that we can deal with them independently.
  struct Island {
    int start = 0;
    int size = 0;
  };
  std::vector<Island> GetIslands(std::span<const Chute> chutes) {
    const int ISLAND_GAP = 2 * max_cell_width;

    std::vector<Island> islands;

    int current_start = 0;
    for (int i = 1; i < (int)chutes.size(); i++) {
      int gap = chutes[i].pos - (chutes[i - 1].pos + Levels::OUT_WIDTH);
      if (gap >= ISLAND_GAP) {
        islands.push_back(Island{
            .start = current_start,
            .size = i - current_start,
          });
        current_start = i;
      }
    }
    islands.push_back(Island{
        .start = current_start,
        .size = (int)chutes.size() - current_start,
      });

    CHECK(!islands.empty());
    return islands;
  }

  // The code below tries to work on chutes in parallel, but it can
  // get stuck when the chutes are overconstrained by being too close.
  // If there are any that are too close, we prioritize a wire-only
  // layer that spaces them out more.
  std::optional<std::pair<std::vector<LC>, int>>
  SpaceLayerIfNeeded(LayoutCanvas &canvas,
                     std::span<const Island> islands) {
    std::span<const Chute> chutes = canvas.chutes;
    CHECK(!chutes.empty()) << "Precondition";

    // Do we need to do this phase?
    auto Needed = [&]() {
        for (int i = 0; i < (int)chutes.size() - 1; i++) {
          int gap = chutes[i + 1].pos - (chutes[i].pos + Levels::OUT_WIDTH);
          CHECK(gap >= 0) << "Bad chutes?";
          if (gap < min_output_distance) return true;
        }
        return false;
      };

    if (!Needed()) return std::nullopt;

    if (verbose > 1) {
      Print(AWHITE("SpaceLayer") " needed. Chutes and islands:\n");
      for (int i = 0; i < (int)chutes.size(); i++) {
        std::string warn = "";
        if (i < (int)chutes.size() - 1) {
          int gap = chutes[i + 1].pos - (chutes[i].pos + Levels::OUT_WIDTH);
          if (gap < min_output_distance) {
            warn = std::format(" " ARED("GAP {}"), gap);
          }
        }

        Print(" [{}] {}{}\n", i,
              LayoutCanvas::ChuteString(chutes[i]), warn);
      }

      for (size_t i = 0; i < islands.size(); i++) {
        Print(" Island {}: start={}, size={}\n",
              i, islands[i].start, islands[i].size);
      }
    }

    for (const Island &island : islands) {
      // Find the leftmost violation (gap that's too small) and
      // the rightmost one.
      //
      // Since we build the circuit bottom-up, the inputs to this new
      // layer are geometrically below the outputs. To increase the gap
      // between the inputs, we slant wires away from the violation.
      //
      // This gets the violation index and its size, if any.
      std::optional<std::pair<int, int>> lviolation = std::nullopt;
      std::optional<std::pair<int, int>> rviolation = std::nullopt;
      for (int i = 0; i < island.size - 1; i++) {
        int cidx = island.start + i;
        int gap = chutes[cidx + 1].pos - (chutes[cidx].pos + Levels::OUT_WIDTH);
        if (gap < min_output_distance) {
          // violation here. Take the first one as the left.
          if (!lviolation.has_value()) {
            lviolation = {{i, gap}};
          }

          // and keep overwriting the right one so we get the last.
          rviolation = {{i, gap}};
        }
      }

      if (!lviolation.has_value()) {
        if (verbose > 1) {
          Print("Island has no violations.\n");
        }
        // This island is already OK. Just propagate everything
        // directly up. (PERF: Do some greedy decomposition, etc. here)
        for (int i = 0; i < island.size; i++) {
          int cidx = island.start + i;
          const Chute &chute = chutes[cidx];

          Gate ga = chute.type == CType::MIXED ? WIREA :
            chute.type == CType::ZERO ? WIRE0A : WIRE1A;
          Gate gb = chute.type == CType::MIXED ? WIREB :
            chute.type == CType::ZERO ? WIRE0B : WIRE1B;

          auto TryPlace = [&](Gate g, bool flip) -> bool {
              Cell cell(g, 0, flip);
              int xpos = chute.pos - ItsOutputPos(cell);
              if (canvas.CanPlaceCell(cidx, cell, xpos)) {
                canvas.assigned[cidx] = true;
                canvas.next.push_back(PC{
                    .xpos = xpos,
                    .cell = cell,
                    .inprops = {chute.prop},
                  });
                return true;
              }
              return false;
            };

          CHECK(TryPlace(ga, false) || TryPlace(gb, false) ||
                TryPlace(ga, true) || TryPlace(gb, true)) <<
            "We already verified that there is enough space!";
        }
        continue;
      }

      CHECK(rviolation.has_value()) << "If there is a leftmost "
        "violation, then there is a rightmost one too!";

      const auto &[vleft_idx, vleft_size] = lviolation.value();
      const auto &[vright_idx, vright_size] = rviolation.value();

      if (verbose > 1) {
        Print("Processing island @{}; "
              "violation at idx {} ({} < {}) and {} ({} < {})\n",
              island.start,
              vleft_idx, vleft_size, min_output_distance,
              vright_idx, vright_size, min_output_distance);
      }

      CHECK(vleft_size > 0 && vleft_size < min_output_distance);
      CHECK(vright_size > 0 && vright_size < min_output_distance);
      int left_space = min_output_distance - vleft_size;
      // Careful: This might be the same quantity if there is one
      // violation, since then left = right. We need to reduce
      // it by the amount we end up slanting left if so.
      int right_space = min_output_distance - vright_size;

      for (int iidx = 0; iidx < island.size; iidx++) {
        bool ok = false;
        int cidx = island.start + iidx;

        enum Slant {
          LEFT,
          UP,
          RIGHT,
        };

        // Thinking bottom up: Should we slant left (positive displacement)
        // to move away from the violation? Between the two violations
        // we just go straight up so that we don't get ourselves stuck.
        const Slant slant =
          iidx <= vleft_idx ? LEFT :
          iidx > vright_idx ? RIGHT :
          UP;

        const Chute &chute = chutes[cidx];

        Gate ga = chute.type == CType::MIXED ? WIREA :
          chute.type == CType::ZERO ? WIRE0A : WIRE1A;
        Gate gb = chute.type == CType::MIXED ? WIREB :
          chute.type == CType::ZERO ? WIRE0B : WIRE1B;

        int space = (slant == LEFT) ? left_space :
                    (slant == RIGHT) ? right_space : 0;
        bool default_flip = slant == RIGHT;

        for (int exp = CellLibrary::MAX_WIRE_EXP; exp >= -1; exp--) {
          int amount = exp == -1 ? 0 : (1 << exp);

          // Don't move more than we need.
          if (amount > space) continue;

          auto TryPlace = [&](Gate g, bool flip) -> bool {
              Cell cell(g, amount, flip);
              int xout = ItsOutputPos(cell);
              int cell_pos = chute.pos - xout;

              if (canvas.CanPlaceCell(cidx, cell, cell_pos)) {
                canvas.assigned[cidx] = true;
                canvas.next.push_back(PC{
                    .xpos = cell_pos,
                    .cell = cell,
                    .inprops = {chute.prop},
                  });
                return true;
              }

              return false;
            };

          if (TryPlace(ga, default_flip) || TryPlace(gb, default_flip) ||
              (amount == 0 && (TryPlace(ga, !default_flip) ||
                               TryPlace(gb, !default_flip)))) {
            // If this is the last gate to the left of the gap, and
            // there is just one violation, then we are reducing the
            // shortfall by the amount we slanted left. Otherwise
            // the space is independent.
            if (iidx == vleft_idx && iidx == vright_idx) {
              right_space -= amount;
            }
            ok = true;
            break;
          }
        }

        CHECK(ok) << "Could not space out chute " << cidx << ":\n"
                  << DebugLayerState(std::nullopt, canvas.chutes, canvas.next);
      }
    }

    if (verbose > 1) {
      Print(AWHITE("Spaced layer result") ":\n{}\n",
            DebugLayerState(std::nullopt, canvas.chutes, canvas.next));
    }

    return {canvas.ConvertToLayer()};
  }

  // Given a top layer (annotated with the propositions it takes as
  // inputs), create a new layer that produces those layers and is
  // simpler. (Simpler as in some unspecified well-founded ordering
  // so that this process terminates.) The input and output layers
  // should start at x=0.
  std::pair<std::vector<LC>, int>
  AddLayer(std::span<const LC> top) override {
    CHECK(!top.empty()) << "Precondition.";

    // First we flatten all of the inputs we need to satisfy on the
    // top layer, with their positions. These are just fixed and
    // independent since we aren't going to try to move them around.
    //
    // The canvas represents the
    LayoutCanvas canvas(library);
    canvas.Reset(canvas.FlattenInputs(top));
    canvas.SetVerbose(verbose);
    std::vector<Chute> &chutes = canvas.chutes;

    CHECK(!chutes.empty());

    if (verbose > 1) {
      Print("Addlayer start:\n{}\n",
            DebugLayerState(std::nullopt, chutes, {}));
    }

    std::vector<Island> islands = GetIslands(chutes);

    if (std::optional<std::pair<std::vector<LC>, int>> ores =
        SpaceLayerIfNeeded(canvas, islands)) {
      num_spaced_layers++;
      if (verbose > 1) {
        Print(AYELLOW("Spaced layer")
              " because some chutes were too close.\n");
      }
      return std::move(ores.value());
    }

    // Now set the desires for each.
    SetChuteDesires(chutes);

    if (verbose > 1) {
      Print(AWHITE("Chute desires") " before placement:\n");
      for (int i = 0; i < (int)chutes.size(); i++) {
        Print(" [{}] {}\n", i,
              LayoutCanvas::ChuteString(chutes[i]));
      }
      Print("\n");
    }


    // Now greedily place, without blocking anything off.

    std::vector<PC> &next_cells = canvas.next;
    std::vector<bool> &assigned = canvas.assigned;

    // Sanity check: Ensure the input chutes are not already stuck before
    // we even place anything.
    {
      const int clear_pos = chutes.back().pos +
        Levels::IN_WIDTH + 2 * library.MinClearanceFar() + 1;
      if (!canvas.CanPlaceCell(
              // Not a real chute
              -1,
              CellLibrary::Spacer(1),
              clear_pos)) {
        LOG(FATAL) << "Input chutes are already in a state where "
          "we're stuck!\n" <<
          DebugLayerState(std::nullopt, chutes, next_cells);
      }
    }

    static constexpr int FLEE_AMOUNT = 32;
    // As we try placing, we note weighted conflicts at chute
    // locations. This helps us with heuristic direction of
    // flow.
    std::vector<int> conflict_weight(chutes.size(), 0);

    // In order to ensure we make progress, the first goal in
    // priority order that is in the right position but doesn't
    // have space is allowed to anchor itself and just propagate
    // upward its chutes with zero displacement. Others will
    // move away from the anchor.
    // TODO: One of these per island?
    std::optional<int> anchor = std::nullopt;

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
          if (canvas.CanPlaceCell(chute_idx, cell, cell_pos)) {
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

            switch (cell.gate) {
            case AND0110: num_and++; break;
            case OR1100: num_or++; break;
            case COMBINE01:
            case COMBINE10: num_comb++; break;
              // TODO also NOT!
            default:
              if (IsWire(cell.gate)) num_wire++;
            }

            return true;
          }
        }

        conflict_weight[chute_idx]++;

        return false;
      };

    // Try placing a cell with two outputs atop chute_idx and
    // chute_idx+1. The output types are forced by the chutes we're
    // connecting to. Since when we flip a binary gate we may modify
    // its meaning, we take a gate and a flipped_gate. This might give
    // us two variants of the geometry to try.
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
            if (canvas.CanPlaceCell(chute_idx, cell, cell_pos)) {
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

              switch (cell.gate) {
              case SEPARATOR01:
              case SEPARATOR10:
                num_sep++;
                break;
              case SELFXCHG01:
              case SELFXCHG10:
              case XCHG00:
              case XCHG01:
              case XCHG10:
              case XCHG11:
                num_xchg++;
                break;
              case DUP0:
              case DUP1:
                num_dup++;
                break;
              default:;
              }

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

        if (verbose > 1) {
          Print("Chute {}: "
                "PlaceBinary ({}/{}) fallback.\n"
                "Current dist {}, target dist {}.\n",
                chute_idx, GateString(gate), GateString(flipped_gate),
                current_dist, target_dist);
        }

        if (current_dist == target_dist) {
          if (!anchor.has_value()) {
            if (verbose > 1) {
              Print("Took anchor @{}\n", chute_idx);
            }
            anchor = {chute_idx};
            // Propagate upward.
            chute1.desire = DesireType::FLOW;
            chute1.desire_val = 0;
            chute2.desire = DesireType::FLOW;
            chute2.desire_val = 0;
          } else {
            // Propagate in tandem away from the
            // anchor.
            CHECK(chute_idx != anchor.value());
            int disp =
              (chute_idx < anchor.value()) ?
              -FLEE_AMOUNT : FLEE_AMOUNT;

            chute1.desire = DesireType::FLOW;
            chute1.desire_val = disp;
            chute2.desire = DesireType::FLOW;
            chute2.desire_val = disp;
          }

        } else if (current_dist > target_dist) {
          chute2.desire = DesireType::FLOW;
          chute2.desire_val = 0;
          chute1.desire = DesireType::FLOW;
          chute1.desire_val = (chute2.pos - info.outputs[1].xblock +
                               info.outputs[0].xblock) - chute1.pos;

          conflict_weight[chute_idx]++;
          conflict_weight[chute_idx + 1]++;

        } else {
          CHECK(current_dist < target_dist);

          chute1.desire = DesireType::FLOW;
          chute1.desire_val = 0;
          chute2.desire = DesireType::FLOW;
          chute2.desire_val = (chute1.pos - info.outputs[0].xblock +
                               info.outputs[1].xblock) - chute2.pos;

          conflict_weight[chute_idx]++;
          conflict_weight[chute_idx + 1]++;
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
            CHECK(b != nullptr);
            if (b->op == BinopOp::AND) {
              if (PlaceAlignedUnary(c, AND0110,
                                    Span{*b->a, *b->a, *b->b, *b->b})) {
                return;
              }
            } else if (b->op == BinopOp::OR) {
              if (PlaceAlignedUnary(c, OR1100,
                                    Span{*b->a, *b->b, *b->a, *b->b})) {
                return;
              }

            } else {
              LOG(FATAL) << "Unexpected binop?";
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
          LayoutCanvas::DesireTypeString(chute.desire) << " should be "
          "handled above, perhaps by turning it into FLOW!";

        if (verbose > 1) {
          Print("Chute {} ({}) DoFlow: desire_val is {}.\n",
                c, LayoutCanvas::DesireTypeString(chute.desire),
                chute.desire_val);
        }

        // XXX when searching for conflict, we should only
        // do this within the current island, I think. No need
        // to push everything away!
        //
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
          if (verbose > 0) {
            Print("Chute {} " AORANGE("fell back")
                  " to displacement 0 wire (wanted {})!\n",
                  c, chute.desire_val);
          }
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

    if (verbose > 1) {
      Print(AWHITE("Layer state at end") ":\n"
            "{}\n",
            DebugLayerState(anchor, chutes, next_cells));
    }

    return canvas.ConvertToLayer();
  }

  void DebugRender(const std::deque<std::vector<LC>> &layers) {
    std::string filename = std::format("debug-render-{}.png", layers.size());
    Circuit circuit;
    circuit.layers.reserve(layers.size());
    for (const std::vector<LC> &layout_layer : layers) {
      std::vector<Cell> layer;
      layer.reserve(layout_layer.size());
      for (const LC &lc : layout_layer) {
        layer.push_back(lc.cell);
      }
      circuit.layers.push_back(std::move(layer));
    }

    ImageRGBA img = RenderCircuit(library, circuit);
    img.Save(filename);
  }

  void DoAddLayer(std::deque<std::vector<LC>> *layers) override {
    const std::vector<LC> &last = layers->front();

    if (verbose > 0) {
      AutoHisto histo(1000);
      size_t max_prop_size = 0;
      size_t total_prop_size = 0;
      for (const LC &lc : last) {
        for (const Prop &p : lc.inprops) {
          size_t ps = PropSize(p);
          max_prop_size = std::max(ps, max_prop_size);
          total_prop_size += ps;
          histo.Observe(ps);
        }
      }

      // Number of pairs of separated chutes that are out of order
      // (using the global <=> ordering), so they will need to be
      // exchanged in order to be combined. A mixed chute does not
      // count as out of order.
      int inversions = 0;
      std::vector<std::pair<Prop, CType>> flat_inputs;
      for (const LC &lc : last) {
        CellLibrary::Info info = library.GetInfo(lc.cell);
        for (size_t i = 0; i < lc.inprops.size(); i++) {
          flat_inputs.emplace_back(lc.inprops[i], info.inputs[i].type);
        }
      }

      for (size_t i = 0; i < flat_inputs.size(); i++) {
        if (flat_inputs[i].second == CType::MIXED) continue;
        for (size_t j = i + 1; j < flat_inputs.size(); j++) {
          if (flat_inputs[j].second == CType::MIXED) continue;
          auto ord = flat_inputs[i].first <=> flat_inputs[j].first;
          if (ord == std::strong_ordering::greater ||
              (ord == std::strong_ordering::equal &&
               flat_inputs[i].second == CType::ONE &&
               flat_inputs[j].second == CType::ZERO)) {
            inversions++;
          }
        }
      }

      // Block width of the top layer, not including exterior spacers.
      int top_layer_width = 0;
      int first_non_spacer = -1;
      int last_non_spacer = -1;
      for (int i = 0; i < (int)last.size(); i++) {
        if (last[i].cell.gate != Gate::SPACER) {
          if (first_non_spacer == -1) first_non_spacer = i;
          last_non_spacer = i;
        }
      }
      if (first_non_spacer != -1) {
        for (int i = first_non_spacer; i <= last_non_spacer; i++) {
          top_layer_width += library.GetInfo(last[i].cell).block_width;
        }
      }

      if (status) {
        status->Status("{}\n"
                       "{} and {} xchg {} or {} wire {} sep {} dup {} comb\n"
                       "Layer {}: {} max prop, {} total. {} inv. {} sp. "
                       "Width: {}.\n",
                       histo.OneLineANSI(75),
                       num_and, num_xchg, num_or, num_wire, num_sep,
                       num_dup, num_comb,
                       layers->size(), max_prop_size, total_prop_size,
                       inversions,
                       num_spaced_layers,
                       top_layer_width);
      }
    }

    if (WRITE_IMAGES /* || (num_layers % 100) == 0 */) {
      DebugRender(*layers);
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

    size_t num_next_outputs = 0;
    for (const LC &lc : next) {
      num_next_outputs += library.GetInfo(lc.cell).outputs.size();
    }

    size_t num_last_inputs = 0;
    for (const LC &lc : last) {
      num_last_inputs += lc.inprops.size();
    }

    if (num_next_outputs != num_last_inputs) {

      LOG(FATAL)
        << "Error after " << layers->size() << "layers:\n"
        << "Bad Layer! New outputs (" << num_next_outputs
        << ") != top layer inputs (" << num_last_inputs << ").";
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
      for (std::vector<LC> &layer : *layers) {
        AddLeftSpacer(layer, pad);
      }
    }

    layers->push_front(std::move(next));
  }


  // We work bottom-up. The goal is to add layers so that we simplify
  // the inputs, until they're all variables.
  Layout DoLayoutInternal(std::span<const Prop> props_in) {
    // std::vector<Prop> props = VectorMap(props_in, NormalizeToAnd);

    // We support AND, OR, NOT.
    std::vector<Prop> props = VectorMap(props_in, NormalizeRemoveXor);

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
    for (int num_layers = 1; true; num_layers++) {
      (void)num_layers;

      // CHECK(num_layers < 8);

      /*
      if (num_layers > 1000) verbose = 2;
      CHECK(num_layers < 1004);
      */

      CHECK(!layers.empty());
      const std::vector<LC> &last = layers.front();

      // If it's all variables, then we are done!
      if (std::optional<std::vector<std::pair<int, CType>>> ovars =
              AllVars(last)) {
        return Finalize(std::move(layers),
                        std::move(ovars.value()));
      }

      DoAddLayer(&layers);
    }
  }

  // Delete unnecessary left/right padding, and convert to the Layout
  // format.
  Layout Finalize(std::deque<std::vector<LC>> layers,
                  std::vector<std::pair<int, CType>> vars) {
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

  Layout DoLayout(std::span<const Prop> props_in) override {
    Timer timer;
    if (verbose > 0) {
      Print("Min output distance: {}\n"
            "Max cell width: {}\n",
            min_output_distance,
            max_cell_width);
    }

    Layout lay = DoLayoutInternal(props_in);

    if (verbose > 0) {
      Print("Got {} inputs; {} layers.\n",
            lay.input_vars.size(),
            lay.circuit.layers.size());
      Print("Finished layout in {}\n", ANSI::Time(timer.Seconds()));
    }

    return lay;
  }

  // Args must outlast the engine.
  LayoutEngineImpl(const CellLibrary &library, const World &world) :
    world(world), library(library) {
    status.reset(new StatusBar(3));

    ComputeMinOutputDistance();
    ComputeMaxCellWidth();
  }
};

}  // namespace

std::unique_ptr<LayoutEngine> LayoutEngine::Create(
    const CellLibrary &library, const World &world) {
  return std::make_unique<LayoutEngineImpl>(library, world);
}

LayoutEngine::LayoutEngine() {}
LayoutEngine::~LayoutEngine() {}

