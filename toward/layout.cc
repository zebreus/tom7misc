
#include "layout.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <format>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "ansi.h"
#include "arcfour.h"
#include "auto-histo.h"
#include "base/logging.h"
#include "base/print.h"
#include "base/stringprintf.h"
#include "cell-library.h"
#include "circuit.h"
#include "color-util.h"
#include "image.h"
#include "inline-vector.h"
#include "layout-canvas.h"
#include "level.h"
#include "map-util.h"
#include "periodically.h"
#include "png.h"
#include "prop.h"
#include "randutil.h"
#include "render-circuit.h"
#include "span-util.h"
#include "status-bar.h"
#include "timer.h"
#include "util.h"
#include "vector-util.h"

static constexpr bool ANCHORING = false;

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
using Chute = LayoutCanvas::Chute;
using DesireType = LayoutCanvas::DesireType;
using Spring = LayoutCanvas::Spring;
using enum LayoutCanvas::DesireType;

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

  // The desired displacement amount, when we didn't have an
  // exact match. Key is abs displacement, value is occurrence count.
  std::unordered_map<int, int> disp_histo;

  // The list of spring records; one per layer that we created.
  std::deque<std::vector<SpringRecord>> spring_records;

  // The amount of space we try to keep between the done region
  // (chutes on the far left and right that contain MIXED vars)
  // and the interior.
  static constexpr int DONE_GAP = 256;
  // And the spacing between chutes in the done region.
  static constexpr int EXTERIOR_SPACING = 32;

  // Just stats for reporting.
  int num_and = 0, num_xchg = 0, num_or = 0, num_wire = 0;
  int num_sep = 0, num_dup = 0, num_comb = 0, num_not = 0;
  int num_spaced_layers = 0;
  int resorted = 0;

  bool write_ranks = true;

  Periodically status_per = Periodically(1.0);
  std::unique_ptr<StatusBar> status_owned;
  StatusBar *status = nullptr;

  std::vector<int> wire_sizes_descending;
  std::unordered_set<int> wire_sizes_available;

  // For a positive sum (key) that is not in wire_sizes_available, all
  // pairs of wire sizes (large, small) that sum to it.
  // large >= abs(small). small may be negative.
  std::unordered_map<int, std::vector<std::pair<int, int>>> wire_sums;

  int verbose = 1;
  bool write_images = false;

  void SetVerbose(int v) override { verbose = v; }
  void SetWriteImages(bool b) override { write_images = b; }
  void SetStatusBar(StatusBar *s) override {
    // Not owned.
    status = s;
  }

  // The closest we ever need outputs to be for a single cell (blocks
  // between right edge and left edge). There's no reason for chutes
  // to be closer than this, so we push them apart to avoid getting
  // stuck.
  int min_output_distance = 0;
  // The largest cell width (not including wires).
  int max_cell_width = 0;

  ArcFour rc;

  template<typename... Args>
  inline void Print(std::format_string<Args...> fmt, Args &&...args) const {
    if (status != nullptr) {
      status->Print(fmt, std::forward<Args>(args)...);
    } else {
      ::Print(fmt, std::forward<Args>(args)...);
    }
  }

  const std::deque<std::vector<SpringRecord>> &GetSpringRecords() const override {
    return spring_records;
  }

  // Returns the flattened vector of variable ids if all
  // of the inputs are variables; nullopt otherwise.
  std::optional<std::vector<std::pair<int, CType>>>
  AllVars(std::span<const LC> lcs) override {
    std::vector<std::pair<int, CType>> vars;
    for (const LC &lc : lcs) {
      CellLibrary::Info info = library.GetInfo(lc.cell);
      for (size_t i = 0; i < lc.inprops.size(); i++) {
        if (info.inputs[i].type != CType::MIXED) {
          return std::nullopt;
        }
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

    if (verbose > 1) {
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

  // Set the desires and springs for "done" chutes on the exterior.
  void SetExterior(LayoutCanvas *canvas) {
    std::vector<Chute> &chutes = canvas->chutes;

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

    constexpr float EXT_WEIGHT = 0.5f;

    if (first_interior_chute > 0) {
      for (int c = 0; c < first_interior_chute; c++) {
        chutes[c].done = true;
        chutes[c].desire = DesireType::QUIESCE;
        int dist = (c == first_interior_chute - 1) ?
          DONE_GAP : EXTERIOR_SPACING;
        LayoutCanvas::UpdateSpring(&canvas->springs[c], dist,
                                   2 * library.MinClearanceFar(),
                                   EXT_WEIGHT, EXT_WEIGHT);
      }
    }

    if (last_interior_chute < (int)chutes.size() - 1) {
      for (int c = last_interior_chute + 1; c < chutes.size(); c++) {
        chutes[c].done = true;
        chutes[c].desire = DesireType::QUIESCE;
        int dist = (c == last_interior_chute + 1) ?
          DONE_GAP : EXTERIOR_SPACING;
        LayoutCanvas::UpdateSpring(&canvas->springs[c - 1], dist,
                                   2 * library.MinClearanceFar(),
                                   EXT_WEIGHT, EXT_WEIGHT);
      }
    }
  }

  // Generate the desired action for each chute by updating its
  // desire field in place.
  void SetChuteDesires(std::vector<Chute> &chutes) {

    struct Counts {
      int any = 0;
      int mixed = 0;
      int zero = 0;
      int one = 0;
    };

    std::unordered_map<Prop, Counts> prop_count;
    for (Chute &chute : chutes) {
      Counts &counts = prop_count[chute.prop];
      counts.any++;
      switch (chute.type) {
      case CType::MIXED:
        counts.mixed++;
        break;
      case CType::ONE:
        counts.one++;
        break;
      case CType::ZERO:
        counts.zero++;
        break;
      }
    }

    // After dealing with the exterior, a mixed variable that is not
    // "done" is going to be problematic, because we will likely need
    // to cross over it to attain the order we want. So uncombine
    // those.
    for (int c = 0; c < chutes.size(); c++) {
      Chute &chute = chutes[c];
      if (!chute.done &&
          chute.type == CType::MIXED &&
          std::holds_alternative<Var>(chute.prop.p)) {
        CHECK(chute.desire == DesireType::UNSPECIFIED);
        chute.desire = UNCOMBINE;
      }
    }

    // We attempt to UNDUP propositions that are equal and already
    // next to one another. We want to do this before crossing over,
    // because reducing the number of total chutes is a big efficiency
    // win. (TODO: We might want to prioritize this proportional to
    // the prop size!)
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
        chute1.desire = UNDUP_LHS;
        chute2.desire = UNDUP_RHS;
        // Note that if we have an odd number of equal propositions,
        // the last one will be passed through and be in the correct
        // position to undup on the next layer.
      }
    }

    // Unseparate separated propositions. We do this after UNDUP
    // so that UNDUP has higher priority. We also only perform
    // unseparation when the count of the relevant proposition
    // has exactly one 1 and 0 chute; otherwise we could end up
    // orphaning one of them. (If not exactly one, then we want
    // to undup them first).
    for (int c = 0; c < (int)chutes.size() - 1; c++) {
      Chute &chute1 = chutes[c];
      Chute &chute2 = chutes[c + 1];

      // Props are equal.
      const Prop &prop = chute1.prop;

      if (!chute1.done && !chute2.done &&
          chute1.desire == UNSPECIFIED &&
          chute2.desire == UNSPECIFIED &&
          chute1.type != CType::MIXED &&
          chute2.type != CType::MIXED &&
          chute1.type != chute2.type &&
          chute1.prop == chute2.prop &&
          // (Note: We could use a weaker condition here. We just don't
          // want to do it in a situation like zero = 2 and one = 1).
          prop_count[prop].zero == 1 &&
          prop_count[prop].one == 1) {

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
            chute1.desire = UNSEPARATE_LHS;
            chute2.desire = UNSEPARATE_RHS;
          }

        } else if (std::holds_alternative<Binop>(prop.p)) {
          // Our binops target mixed outputs, so we need
          // to unseparate wherever this is. On the next
          // layer we should be able to decompose.
          chute1.desire = UNSEPARATE_LHS;
          chute2.desire = UNSEPARATE_RHS;

        } else if (std::holds_alternative<Value>(prop.p)) {
          // Values should usually be optimized away, so
          // we don't worry too much about being clever
          // here. Since CONST0 and CONST1 output mixed
          // values, we should unseparate like in the
          // other cases.
          chute1.desire = UNSEPARATE_LHS;
          chute2.desire = UNSEPARATE_RHS;
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

    // Strip NOT from separated propositions, which is easy to do
    // locally. It would often be better to swap and dedup first if
    // possible (because when we swap with some non-negated
    // proposition, we are at least helping that other one get to the
    // right place!). But these are basically just wires so they don't
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
        if (chute1.rank > chute2.rank) {
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
  }

  void SetRanks(LayoutCanvas *canvas,
                const std::unordered_map<Prop, int> &prop_ranks) {
    auto type_rank = [](CType t) {
      if (t == CType::ZERO) return 0;
      if (t == CType::ONE) return 1;
      return 2;
    };

    for (Chute &chute : canvas->chutes) {
      auto it = prop_ranks.find(chute.prop);
      int pr = it != prop_ranks.end() ? it->second : 0;
      chute.rank = pr * 3 + type_rank(chute.type);
    }
  }

  // Given a top layer (annotated with the propositions it takes as
  // inputs), create a new layer that produces those layers and is
  // simpler. (Simpler as in some unspecified well-founded ordering
  // so that this process terminates.) The input and output layers
  // should start at x=0.
  //
  // If allow_placement is false, then we don't place useful gates;
  // we just compute the desired clearance and run the spring solver.
  // This allows us to space out large circuits without getting in
  // our own way.
  std::tuple<std::vector<LC>, int, std::vector<SpringRecord>>
  AddLayer(const std::deque<std::vector<LC>> &layers,
           const std::unordered_map<Prop, int> &prop_ranks,
           bool allow_placement) override {
    std::span<const LC> top = layers.front();
    CHECK(!top.empty()) << "Precondition.";

    // First we flatten all of the inputs we need to satisfy on the
    // top layer, with their positions. These are just fixed and
    // independent since we aren't going to try to move them around.
    //
    // The canvas represents the top of the existing circuit and
    // our in-progress new layer.
    LayoutCanvas canvas(library);
    canvas.Reset(canvas.FlattenInputs(top));
    canvas.SetVerbose(verbose);
    CHECK(!canvas.chutes.empty());

    if (verbose > 1) {
      Print("\n" AYELLOW("=========== AddLayer start") ":\n{}\n",
            canvas.DebugString());
    }

    // First, identify the already-completed portions of the
    // circuit and create springs for them. We don't want to
    // involve these in the gate-placement code below.
    SetExterior(&canvas);

    // Annotate the curent global ordering onto each chute.
    // We use this to perform local swaps to move chutes closer
    // to the desired position.
    SetRanks(&canvas, prop_ranks);

    OutputDebugging(layers, canvas);

    // Now set the desires for each.
    SetChuteDesires(canvas.chutes);

    if (verbose > 1) {
      Print(AWHITE("Chute desires") " before placement:\n");
      for (int i = 0; i < (int)canvas.chutes.size(); i++) {
        Print(" [{}] {}\n", i,
              LayoutCanvas::ChuteString(canvas.chutes[i]));
      }
      Print("\n");
    }

    // Now we do a greedy pass. If we can place a desired cell
    // (without creating a dead end) we do so, because this makes
    // definite progress.

    // Sanity check: Ensure the input chutes are not already stuck before
    // we even place anything.
    canvas.CheckNotStuck();

    // In order to ensure we make progress, the first goal in
    // priority order that is in the right position but doesn't
    // have space is allowed to anchor itself and just propagate
    // upward its chutes with zero displacement. Others will
    // move away from the anchor.
    bool took_anchor = false;

    // Try to place the specified gate so its single output aligns
    // with this chute (also trying its flipped version). The
    // inprops should reflect the input propositions for that cell
    // in its unflipped orientation.
    // Updates canvas.next and assigned if successful and returns
    // true. Otherwise, makes no change and returns false.
    auto PlaceAlignedUnary =
      [&](int chute_idx,
          Gate g,
          std::span<const Prop> inprops,
          int cell_val = 0,
          std::initializer_list<bool> allow_flips = {false, true}) -> bool {
        CHECK(!canvas.Assigned(chute_idx));
        Chute &chute = canvas.chutes[chute_idx];

        if (!allow_placement && !IsWire(g)) return false;

        for (bool flip : allow_flips) {
          Cell cell(g, cell_val, flip);
          int xout = ItsOutputPos(cell);

          int cell_pos = chute.pos - xout;
          if (canvas.CanPlaceCell(chute_idx, cell, cell_pos)) {
            canvas.Assign(chute_idx);
            std::vector<Prop> ip(inprops.begin(), inprops.end());
            if (flip) {
              VectorReverse(&ip);
            }
            canvas.AddNext(cell_pos, cell, std::move(ip));

            switch (cell.gate) {
            case AND0110: num_and++; break;
            case OR1100: num_or++; break;
            case COMBINE01:
            case COMBINE10: num_comb++; break;
            case NOT0:
            case NOT1: num_not++; break;
            default:
              if (IsWire(cell.gate)) num_wire++;
            }

            return true;
          }
        }

        return false;
      };

    // Updates the springs to the left and right of the given chute(s)
    // to ensure we have enough space to place the gate (unflipped).
    // The chute_idx should be the index of the leftmost chute used
    // by the gate.
    auto AcquireClearance = [&](int chute_idx, Gate gate) {
        Cell cell_unflipped(gate, 0, false);
        CellLibrary::Info info = library.GetInfo(cell_unflipped);

        int out0 = info.outputs.front().xblock;
        int out_last = info.outputs.back().xblock;

        // Since we don't actually know where the next
        // chute would be relative to its cell's right edge, we should
        // be conservative here.
        int additional_clearance =
          library.MinClearanceFar() * 4;

        int stick_out_left = out0;
        int stick_out_right = info.block_width - (out_last + Levels::IN_WIDTH);

        // Request the same amount on both sides.
        // Here we want the edge-to-edge clearance between the gates.
        int clearance = std::max(stick_out_left, stick_out_right) +
          additional_clearance;

        #if 0
        if (verbose > 2) {
          Print("[{}] acquire clearance for {}. L: {}, R: {}\n",
                chute_idx, GateString(gate),
                left_clearance, right_clearance);
        }
        #endif

        // (No spring to the left of the first chute.)
        if (chute_idx > 0) {
          Spring *left = &canvas.springs[chute_idx - 1];
          LayoutCanvas::UpdateSpring(
              left,
              clearance,
              2 * library.MinClearanceFar(),
              // Compressing would mean we're still unable to
              // place.
              100.0f,
              // Happy to have expansion here.
              0.01f);
        }

        // (No spring to the right of the last chute.)
        int last_chute_idx = chute_idx + info.outputs.size() - 1;
        if (last_chute_idx < (int)canvas.chutes.size() - 1) {
          Spring *right = &canvas.springs[last_chute_idx];
          LayoutCanvas::UpdateSpring(
              right,
              clearance,
              2 * library.MinClearanceFar(),
              100.0f,
              0.01f);
        }
      };

    // XXX: This works, but I think we can probably tweak it to make
    // it a lot more efficient; I often see in real examples that we
    // go several layers meandering before we hit the right distance
    // and deposit the gate (or perhaps we are at the right distance
    // but for some reason don't put the gate!).

    // Try placing a cell with two outputs atop chute_idx and
    // chute_idx+1. The output types are forced by the chutes we're
    // connecting to. Since when we flip a binary gate we may modify
    // its meaning, we take a gate and a flipped_gate. This might give
    // us two variants of the geometry to try. If we succeed, the
    // spring becomes rigid.
    //
    // If neither the gate or its flipped version fit, we don't place
    // anything, but use springs to try to achieve the right distance.
    // This also might anchor the chutes if this is the first such
    // failure.
    auto PlaceBinaryOrFallback = [&](int chute_idx,
                                     Gate gate, Gate flipped_gate) {
        CHECK(chute_idx < canvas.chutes.size() - 1);
        CHECK(!canvas.Assigned(chute_idx));
        CHECK(!canvas.Assigned(chute_idx + 1));

        Chute &chute1 = canvas.chutes[chute_idx];
        Chute &chute2 = canvas.chutes[chute_idx + 1];
        Spring &spring = canvas.springs[chute_idx];

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
                info.outputs[1].type == chute2.type) <<
            GateString(gate) << " " <<
            TypeString(info.outputs[0].type) << " == " <<
            TypeString(chute1.type) << " &&\n" <<
            TypeString(info.outputs[1].type) << " == " <<
            TypeString(chute2.type);

          int cell_pos = chute1.pos - out0;
          if (cell_pos + out1 == chute2.pos) {
            // Correct relative position, but will the cell fit?
            if (allow_placement &&
                canvas.CanPlaceCell(chute_idx, cell, cell_pos)) {
              canvas.Assign(chute_idx);
              canvas.Assign(chute_idx + 1);

              int dist = out1 - (out0 + Levels::IN_WIDTH);
              CHECK(dist >= 0);
              spring.target_dist = spring.min_dist = dist;
              // These parameters don't matter anyway, since
              // we marked both chutes as anchored.
              spring.compress = 1000.0f;
              spring.expand = 1000.0f;

              // XXX Maybe would be cleaner to pass this at
              // the call site?
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
              canvas.AddNext(cell_pos, cell, std::move(inprops));

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

              if (verbose > 2) {
                Print("[{}] Did place {}\n", chute_idx,
                      CellString(cell));
              }

              return;
            }

            if (verbose > 2) {
              Print("[{}] could not place {}\n", chute_idx,
                    CellString(cell));
            }

          }
        }

        // We need the chutes to have the correct output distance (which
        // will be the same in each orientation).
        Cell cell_unflipped(gate, 0, false);
        CellLibrary::Info info = library.GetInfo(cell_unflipped);
        CHECK(info.outputs.size() == 2);
        int out0 = info.outputs[0].xblock;
        int out1 = info.outputs[1].xblock;

        int dist = out1 - (out0 + Levels::IN_WIDTH);
        int current_dist = chute2.pos - (chute1.pos + Levels::IN_WIDTH);
        if (verbose > 1) {
          Print("[{}] {}: Have dist {} want dist {}\n",
                chute_idx, CellString(cell_unflipped), current_dist, dist);
        }
        if (dist == current_dist && !took_anchor && allow_placement &&
            ANCHORING) {
          // The first time (since we do these in a heuristic priority order)
          // that we have the right distance (but presumably could not fit
          // the cell itself) we anchor these so that we will eventually
          // make progress as things are pushed away from it.
          //
          // XXX We can probably still allow this single anchor when
          // allow_placement is false; the spring solver will choose
          // one arbitrarily anyway.
          took_anchor = true;
          chute1.anchored = true;
          chute2.anchored = true;
        }

        CHECK(dist >= 0);
        spring.target_dist = dist;
        // Very counterproductive to compress or expand when
        // we're already at the right distance.
        spring.compress = 1000.0f;
        spring.expand = 1000.0f;

        // Unclear what the correct absolute minimum is; match
        // the safe clearance used by LayoutCanvas.
        spring.min_dist = 2 * library.MinClearanceFar();

        // We also want to make space on each side of the chute for
        // the gate itself.
        AcquireClearance(chute_idx, gate);
      };

    auto DoUndup = [&](int c) {
        // On the left one of a pair.
        if (c + 1 >= canvas.chutes.size())
          return;
        if (canvas.Assigned(c) || canvas.Assigned(c + 1)) return;

        Chute &chute1 = canvas.chutes[c];
        Chute &chute2 = canvas.chutes[c + 1];

        if (chute1.desire != DesireType::UNDUP_LHS ||
            chute2.desire != DesireType::UNDUP_RHS)
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
        if (c + 1 >= canvas.chutes.size()) return;
        if (canvas.Assigned(c) || canvas.Assigned(c + 1)) return;

        Chute &chute1 = canvas.chutes[c];
        Chute &chute2 = canvas.chutes[c + 1];

        if (chute1.desire != DesireType::UNSEPARATE_LHS ||
            chute2.desire != DesireType::UNSEPARATE_RHS)
          return;

        // Must have same prop and opposite separated bits.
        CHECK(chute1.prop == chute2.prop &&
              chute1.type != CType::MIXED &&
              chute2.type != CType::MIXED &&
              chute1.type != chute2.type) << "\nchute1:\n" <<
          LayoutCanvas::ChuteString(chute1) << "\nchute2:\n" <<
          LayoutCanvas::ChuteString(chute2);

        // Here, 0 and 1 refer to the output types.
        Gate g = SEPARATOR01, ginv = SEPARATOR10;
        if (chute1.type == CType::ONE) std::swap(g, ginv);

        PlaceBinaryOrFallback(c, g, ginv);
      };

    auto DoDecompose = [&](int c) {
        Chute &chute = canvas.chutes[c];
        if (chute.desire != DesireType::DECOMPOSE) {
          return;
        }

        if (chute.type == CType::MIXED) {
          if (const Value *v = std::get_if<Value>(&chute.prop.p)) {
            Gate g = v->value ? CONST1 : CONST0;
            if (PlaceAlignedUnary(c, g, {})) {
              return;
            }
            AcquireClearance(c, g);

          } else {
            const Binop *b = std::get_if<Binop>(&chute.prop.p);
            CHECK(b != nullptr);
            if (b->op == BinopOp::AND) {
              if (PlaceAlignedUnary(c, AND0110,
                                    Span{*b->a, *b->a, *b->b, *b->b})) {
                return;
              }
              AcquireClearance(c, AND0110);

            } else if (b->op == BinopOp::OR) {
              if (PlaceAlignedUnary(c, OR1100,
                                    Span{*b->a, *b->b, *b->a, *b->b})) {
                return;
              }
              AcquireClearance(c, OR1100);

            } else {
              LOG(FATAL) << "Unexpected binop?";
            }
          }

        } else {
          // NOT0 takes a separated 0 as input, and outputs a separated 1.
          // So note we are switching on the output type here.
          Gate g = chute.type == CType::ZERO ? NOT1 : NOT0;
          const Unop *u = std::get_if<Unop>(&chute.prop.p);
          CHECK(u && u->op == UnopOp::NOT);
          if (PlaceAlignedUnary(c, g, Span{*u->a})) {
            return;
          }
          AcquireClearance(c, g);
        }

        chute.desire = DesireType::QUIESCE;
      };

    auto DoUncombine = [&](int c) {
        Chute &chute = canvas.chutes[c];
        if (chute.desire != DesireType::UNCOMBINE) {
          return;
        }

        if (PlaceAlignedUnary(c, COMBINE01, Span{chute.prop, chute.prop}) ||
            PlaceAlignedUnary(c, COMBINE10, Span{chute.prop, chute.prop})) {
          return;
        }

        chute.desire = DesireType::QUIESCE;
        AcquireClearance(c, COMBINE01);
      };

    auto DoExchange = [&](int c) {
        // On the left one of a pair.
        if (c + 1 >= canvas.chutes.size())
          return;
        if (canvas.Assigned(c) || canvas.Assigned(c + 1)) return;

        Chute &chute1 = canvas.chutes[c];
        Chute &chute2 = canvas.chutes[c + 1];

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

    // Now do the passes above in priority order.

    std::vector<int> todo;
    todo.reserve(canvas.chutes.size());
    {
      // Collate by descending proposition size. We prefer to act on big
      // propositions, since once they are split we can work on their
      // components in parallel.
      std::map<int, std::vector<int>, std::greater<int>> by_size;
      for (int c = 0; c < canvas.chutes.size(); c++) {
        by_size[PropSize(canvas.chutes[c].prop)].push_back(c);
      }

      for (auto &[size, indices] : by_size) {
        Shuffle(&rc, &indices);
        todo.insert(todo.end(), indices.begin(), indices.end());
      }
    }

    auto ForAllRemaining = [&](auto f) {
        for (int c : todo)
          if (!canvas.Assigned(c))
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

    // Default target distance for any remaining springs.
    for (int i = 0; i < (int)canvas.springs.size(); i++) {
      if (canvas.springs[i].target_dist < 0) {
        /*
        int current_dist = canvas.chutes[i + 1].pos -
                           (canvas.chutes[i].pos + Levels::IN_WIDTH);
        */
        LayoutCanvas::UpdateSpring(&canvas.springs[i],
                                   27,
                                   // min_output_distance,
                                   // max_cell_width + 1,
                                   2 * library.MinClearanceFar(),
                                   1.0f, 0.1f);
      }
    }

    if (verbose > 1) {
      status->Print("--- Springs ---\n");
      for (int i = 0; i < (int)canvas.springs.size(); i++) {
        const Spring &s = canvas.springs[i];
        status->Print(
            " [{}] target_dist={} min_dist={} compress={:.4f} expand={:.4f}\n",
            i, s.target_dist, s.min_dist, s.compress, s.expand);
      }
    }

    // Now we usually have a lot of chutes leftover that we need
    // to move around with wires. First we get the ideal positions
    // for them by solving the springs.
    std::vector<double> ideal_pos = canvas.SolveSprings();
    CHECK(ideal_pos.size() == canvas.chutes.size());

    std::vector<SpringRecord> spring_records;
    spring_records.reserve(canvas.chutes.size());

    // Displacement here is talking about the way we want the
    // chute to move as we go bottom up. Positive displacement
    // means that we want the gate on the next layer to be to the
    // right of where it currently is.
    std::vector<int> ideal_disp(canvas.chutes.size(), 0);
    for (int i = 0; i < canvas.chutes.size(); i++) {
      SpringRecord rec;
      rec.start_pos = canvas.chutes[i].pos;
      rec.ideal_pos = (float)ideal_pos[i];
      if (i < canvas.springs.size()) {
        const Spring &s = canvas.springs[i];
        rec.target_dist = s.target_dist;
        rec.min_dist = s.min_dist;
        rec.compress = s.compress;
        rec.expand = s.expand;
      }
      rec.anchored = canvas.chutes[i].anchored;
      spring_records.push_back(rec);

      ideal_disp[i] = (int)std::round(ideal_pos[i]) - canvas.chutes[i].pos;
      if (verbose > 1) {
        if (canvas.Assigned(i)) {
          Print("[{}] is assigned.\n", i);
        } else {
          Print("[{}] should move {} from {} to {:.2f}\n",
                i, ideal_disp[i], canvas.chutes[i].pos, ideal_pos[i]);
        }
      }
    }

    // Place the wire at chute index c.
    auto PlaceWire = [&](int c) {
        if (canvas.Assigned(c)) return;

        Chute &chute = canvas.chutes[c];
        int displacement = ideal_disp[c];
        // An unflipped wire has its output to the right of its input,
        // which corresponds to negative displacement when thinking
        // bottom-up.
        bool flip = displacement > 0;
        int abs_disp = std::abs(displacement);

        auto PlaceAnyWire = [&](int amount) -> bool {
            Gate ga = CellLibrary::Wire(
                amount, CellLibrary::Bias::RIGHT, chute.type).gate;
            Gate gb = CellLibrary::Wire(
                amount, CellLibrary::Bias::LEFT, chute.type).gate;

            if (PlaceAlignedUnary(c, ga, Span{chute.prop}, amount, {flip}))
              return true;

            // Also try the other wire variant if it exists.
            return ga != gb &&
              PlaceAlignedUnary(c, gb, Span{chute.prop}, amount, {flip});
          };

        // First, try the exact wire size.
        if (wire_sizes_available.contains(abs_disp)) {

          if (PlaceAnyWire(abs_disp))
            return;

        } else {
          // Record this so I know what wires would be most useful!
          if (abs_disp <= 256) {
            disp_histo[abs_disp]++;
          }
        }

        // Then, see if we can do it in two wires. Undershoot is
        // preferable because it uses less space on this layer.
        for (const auto &[large, small] : wire_sums[abs_disp])
          if (small > 0 && PlaceAnyWire(large))
              return;

        // Otherwise, see if we can use a wire that
        // would go over by an amount that we can subtract with
        // a single additional wire. If we have space to overshoot,
        // then the "negative" wire will fit on the next layer,
        // above this larger wire.
        for (const auto &[large, small] : wire_sums[abs_disp])
          if (small < 0 && PlaceAnyWire(large))
            return;


        // Otherwise, just take the largest step that fits.
        for (int amount : wire_sizes_descending) {
          if (amount > 0 && amount <= abs_disp) {
            if (PlaceAnyWire(amount)) {
              return;
            }
          }
        }
      };

    // If we didn't place anything, just propagate upward. We should
    // always have space for this. We do this last since it doesn't
    // accomplish anything; we don't want it to prevent a delicate
    // movement because we chose the wrong bias, for example.
    auto PlaceUnaryWire = [&](int c) {
        if (canvas.Assigned(c)) return;
        Chute &chute = canvas.chutes[c];

        Gate ga = CellLibrary::Wire(0, CellLibrary::Bias::RIGHT,
                                    chute.type).gate;
        Gate gb = CellLibrary::Wire(0, CellLibrary::Bias::LEFT,
                                    chute.type).gate;

        if (!PlaceAlignedUnary(c, ga, Span{chute.prop}, 0) &&
            (ga == gb || !PlaceAlignedUnary(c, gb, Span{chute.prop}, 0))) {
          LOG(FATAL) << "Could not even place a 0-displacement wire "
            "for chute " << c;
        }
      };

    // We want to assign the wires in a smart order so that
    // we don't step on our own toes, since wires take space.

    // First place the ones that are going strictly left in
    // left-to-right order, then the reverse.
    for (int c = 0; c < canvas.chutes.size(); c++)
      if (ideal_disp[c] < 0)
        PlaceWire(c);

    for (int c = canvas.chutes.size() - 1; c >= 0; c--)
      if (ideal_disp[c] > 0)
        PlaceWire(c);

    for (int c = 0; c < canvas.chutes.size(); c++)
      PlaceUnaryWire(c);

    if (verbose > 1) {
      Print(AWHITE("Layer state at end") ":\n"
            "{}\n",
            canvas.DebugString());
    }

    auto [next_layer, start_pos] = canvas.ConvertToLayer();
    return {std::move(next_layer), start_pos, std::move(spring_records)};
  }

  void DebugRender(const std::deque<std::vector<LC>> &layers) {
    if (layers.empty()) return;

    // XXX dynamic...
    bool mini = true;

    std::string filename = std::format("debug-render-{}.png", layers.size());

    // Render only the top of the circuit, since they can get very large!
    int MAX_CIRCUIT_LAYERS = layers.front().size() < 32768 ? 500 : 200;
    if (mini) MAX_CIRCUIT_LAYERS *= 2;

    Print("Saving top {} layers to {}...\n", MAX_CIRCUIT_LAYERS, filename);

    Circuit circuit;
    circuit.layers.reserve(std::min((int)layers.size(), MAX_CIRCUIT_LAYERS));

    for (int i = 0; i < layers.size() && i < MAX_CIRCUIT_LAYERS; i++) {
      const std::vector<LC> &layout_layer = layers[i];
      std::vector<Cell> layer;
      layer.reserve(layout_layer.size());
      for (const LC &lc : layout_layer) {
        layer.push_back(lc.cell);
      }
      circuit.layers.push_back(std::move(layer));
    }

    std::optional<int> min_left;
    for (const Layer &layer : circuit.layers) {
      if (layer.empty()) continue;
      int left = (layer.front().gate == Gate::SPACER) ? layer.front().v : 0;
      if (!min_left.has_value() || left < *min_left) {
        min_left = left;
      }
    }

    if (min_left.value_or(0) > 0) {
      for (Layer &layer : circuit.layers) {
        if (layer.empty()) continue;
        if (layer.front().gate == Gate::SPACER) {
          layer.front().v -= *min_left;
          if (layer.front().v == 0) {
            layer.erase(layer.begin());
          }
        }
      }
    }

    std::vector<bool> is_vars;
    for (const LC &lc : layers.front()) {
      for (const Prop &p : lc.inprops) {
        is_vars.push_back(std::holds_alternative<Var>(p.p));
      }
    }

    ImageRGBA img = mini ? RenderCircuitMini(library, circuit, is_vars) :
      RenderCircuit(library, circuit, is_vars);
    std::vector<uint8_t> png = PNG::EncodeInMemory(img);
    Util::WriteFileBytes(filename, png);
    Print("Wrote " AGREEN("{}") ".", filename);

    std::string sfilename = std::format("springs-{}.png", layers.size());
    WriteSpringHistory(MAX_CIRCUIT_LAYERS, sfilename);
    Print("\nWrote " AGREEN("{}") ".\n", sfilename);
  }

  void WriteSpringHistory(int max_circuit_layers, std::string_view filename) {

    // Render spring history.
    constexpr uint32_t ANCHORED_CHUTE = 0xFFFF00FF;
    constexpr uint32_t NORMAL_CHUTE = 0xAAAAAAFF;

    const float compressed_hue = std::get<0>(ColorUtil::RGBA32ToHSVA(0xFF0000FF));
    const float stretched_hue = std::get<0>(ColorUtil::RGBA32ToHSVA(0x00FF00FF));

    if (!spring_records.empty()) {
      int min_x = 0, max_x = 0;
      int num_render_layers =
          std::min((int)spring_records.size(), max_circuit_layers);
      for (int i = 0; i < num_render_layers; i++) {
        for (const SpringRecord &rec : spring_records[i]) {
          min_x = std::min({min_x, rec.start_pos / Levels::IN_WIDTH,
                            (int)std::round(rec.ideal_pos) / Levels::IN_WIDTH});
          max_x = std::max({max_x, rec.start_pos / Levels::IN_WIDTH,
                            (int)std::round(rec.ideal_pos) / Levels::IN_WIDTH});
        }
      }

      int offset_x = min_x < 0 ? -min_x + 10 : 10;
      int img_w = max_x + offset_x + 10;
      int img_h = num_render_layers * 3;
      ImageRGBA simg(img_w, img_h);
      simg.Clear32(0x000000FF);

      for (int i = 0; i < num_render_layers; i++) {
        const std::vector<SpringRecord> &layer_recs = spring_records[i];
        int y_base = i * 3;
        int y_top = y_base;
        int y_mid = y_base + 1;
        int y_bot = y_base + 2;

        for (size_t c = 0; c < layer_recs.size(); c++) {
          const SpringRecord &rec = layer_recs[c];
          int sx = rec.start_pos / Levels::IN_WIDTH + offset_x;
          int ix = (int)std::round(rec.ideal_pos) / Levels::IN_WIDTH + offset_x;

          uint32_t color = rec.anchored ? ANCHORED_CHUTE : NORMAL_CHUTE;
          simg.SetPixel32(sx, y_bot, color);
          simg.SetPixel32(ix, y_top, color);

          if (c < layer_recs.size() - 1) {
            const SpringRecord &next_rec = layer_recs[c + 1];
            int nx = next_rec.start_pos / Levels::IN_WIDTH + offset_x;
            int current_dist = next_rec.start_pos - rec.start_pos;
            if (current_dist >= 0 && rec.target_dist > 0) {
              bool compressed = rec.target_dist > current_dist;
              float diff = 0.0f;
              if (current_dist > 0) {
                float ratio = (float)rec.target_dist / current_dist;
                diff = compressed ? (ratio - 1.0f) : (1.0f - ratio);
              } else {
                diff = compressed ? 1.0f : 0.0f;
              }

              float val = std::clamp(0.5f + diff * 0.5f, 0.5f, 1.0f);
              float hue = compressed ? compressed_hue : stretched_hue;
              uint32_t line_color = ColorUtil::HSVAToRGBA32(hue, 1.0f, val, 0.25f);

              int center = (sx + nx) / 2;
              int half_len = (rec.target_dist / Levels::IN_WIDTH) / 2;
              int x0 = std::max(0, center - half_len);
              int x1 = std::min(img_w - 1, center + half_len);
              simg.BlendLine32(x0, y_mid, x1, y_mid, line_color);
            }
          }
        }
      }

      std::vector<uint8_t> spng = PNG::EncodeInMemory(simg);
      Util::WriteFileBytes(filename, spng);
    }
  }

  void OutputDebugging(const std::deque<std::vector<LC>> &layers,
                       const LayoutCanvas &canvas) {
    const std::vector<LC> &last = layers.front();
    if (verbose > 0 && status) {
      // Stats on the input propositions.
      AutoHisto histo(10000);
      size_t max_prop_size = 0;
      size_t total_prop_size = 0;
      int count_var = 0;
      int count_and = 0;
      int count_or = 0;
      int count_not = 0;
      for (const LC &lc : last) {
        for (const Prop &p : lc.inprops) {
          size_t ps = PropSize(p);
          max_prop_size = std::max(ps, max_prop_size);
          total_prop_size += ps;
          histo.Observe(ps);

          if (std::holds_alternative<Var>(p.p)) {
            count_var++;
          } else if (const Binop *b = std::get_if<Binop>(&p.p)) {
            if (b->op == BinopOp::AND) {
              count_and++;
            } else if (b->op == BinopOp::OR) {
              count_or++;
            }
          } else if (const Unop *u = std::get_if<Unop>(&p.p)) {
            if (u->op == UnopOp::NOT) {
              count_not++;
            }
          }
        }
      }

      // Number of pairs of separated chutes that are out of order
      // (using the global ordering), so they will need to be
      // exchanged in order to be combined. A mixed chute does not
      // count as out of order.
      int inversions = 0;

      for (size_t i = 0; i < canvas.chutes.size(); i++) {
        if (canvas.chutes[i].type == CType::MIXED) continue;
        for (size_t j = i + 1; j < canvas.chutes.size(); j++) {
          if (canvas.chutes[j].type == CType::MIXED) continue;
          if (canvas.chutes[i].rank > canvas.chutes[j].rank) {
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
        status_per.RunIf([&]() {
            status->Status(
                "{}\n"
                "Gates: {} " ABLUE("∧") " {} xchg {} " ABLUE("∨")
                " {} " ABLUE("|") " {} sep {} dup {} comb\n"
                "[" AWHITE("Layer {}") "] {} max prop, {} total. {} inv. "
                "{} resort. "
                "Width: {}.\n"
                "{} top: {} v {} and {} or {} not",
                histo.OneLineANSI(75),
                num_and, num_xchg, num_or, num_wire, num_sep,
                num_dup, num_comb,
                layers.size(), max_prop_size, total_prop_size,
                inversions, resorted,
                top_layer_width,
                last.size(), count_var, count_and, count_or, count_not);
          });
      }
    }

    if (write_images || (layers.size() % 500) == 0) {
      DebugRender(layers);
    }

    if (layers.size() % 1000) {
      std::vector<std::pair<int, int>> missing_wires =
        CountMapToDescendingVector(disp_histo);
      std::string content;
      for (int i = 0; i < missing_wires.size() && i < 80; i++) {
        const auto &[disp, count] = missing_wires[i];
        AppendFormat(&content, "{}: {}\n", disp, count);
      }
      Util::WriteFile("desired-wires.txt", content);
    }
  }

  // Returns true if we made definite progress on the layer,
  // by placing at least one decomposing or unduplicating gate.
  // This permits us to rerank the propositions without creating
  // the possibility of an endless loop.
  static bool DefiniteProgress(const std::vector<LC> &new_layer) {
    for (const LC &lc : new_layer) {
      switch (lc.cell.gate) {
      case Gate::AND0110:
      case Gate::OR1100:
      case Gate::NOT0:
      case Gate::NOT1:
      case Gate::NOT01:
      case Gate::NOT:
        return true;

      case Gate::DUP0:
      case Gate::DUP1:
        return true;

      default:
        continue;
      }
    }

    return false;
  }

  void DoAddLayer(std::deque<std::vector<LC>> *layers,
                  const std::unordered_map<Prop, int> &prop_ranks) override {
    const std::vector<LC> &last = layers->front();

    // TODO: See if we're too squished, and if so, don't allow
    // placement!

    // Compute a new top layer. Every once in a while we don't allow
    // the placement of useful gates, so that we can resolve spring
    // tension without anchors.
    constexpr int SPACING_EVERY = 100;
    const bool allow_placement = (layers->size() % SPACING_EVERY) != 0;
    auto [next, start_pos, layer_spring_records] = AddLayer(*layers, prop_ranks, allow_placement);

    // True if the layers are effectively the same, ignoring leading
    // spacers (i.e. they can have different starting offsets). If
    // we have two such layers in a row, then we will just get in
    // an infinite loop, so we should abort.
    auto SameLayer = [](const std::vector<LC> &a,
                        const std::vector<LC> &b) {
        auto ita = a.begin(), itb = b.begin();
        while (ita != a.end() && ita->cell.gate == Gate::SPACER) ita++;
        while (itb != b.end() && itb->cell.gate == Gate::SPACER) itb++;
        for (;;) {
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

    if (allow_placement && SameLayer(last, next)) {
      LOG(FATAL) << "Layout made no progress! New layer is identical\n"
        "to the previous one.";
    }

    // Another simple validity self-check: The layer should match
    // the number of chutes on the next one.
    {
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
      for (std::vector<SpringRecord> &shr : this->spring_records) {
        for (SpringRecord &rec : shr) {
          rec.start_pos += pad;
          rec.ideal_pos += pad;
        }
      }
      for (SpringRecord &rec : layer_spring_records) {
        rec.start_pos += pad;
        rec.ideal_pos += pad;
      }
    }

    layers->push_front(std::move(next));
    this->spring_records.push_front(std::move(layer_spring_records));
  }

  std::unordered_map<Prop, int> GetPropRanks(
      std::span<const LC> layer,
      std::string_view debug_filename = "") override {
    std::unordered_map<Prop, InlineVector<int>> positions;

    int next_pos = 0;
    int largest_pos = 0;
    int largest = 0;

    std::function<int(const Prop &)> Flatten = [&](const Prop &prop) -> int {
        int size = 0;
        // First descend left.
        if (const Binop *bop = std::get_if<Binop>(&prop.p)) {
          size += Flatten(*bop->a);
        } else if (const Unop *uop = std::get_if<Unop>(&prop.p)) {
          size += Flatten(*uop->a);
        }

        int pos = next_pos++;
        positions[prop].push_back(pos);

        // Then descend right.
        if (const Binop *bop = std::get_if<Binop>(&prop.p)) {
          size += Flatten(*bop->b);
        }

        size++;

        if (size > largest) {
          largest_pos = pos;
          largest = size;
        }

        return size;
      };

    for (const LC &lc : layer) {
      for (const Prop &prop : lc.inprops) {
        Flatten(prop);
      }
    }

    // Now we project everything to a position on the number line.
    // This is unsorted, but each prop is unique.
    // prop, count, pos
    std::vector<std::tuple<Prop, int, double>> proj;

    // A cheap way of separating into classes: We space them out
    // by the current total width.
    double width = next_pos;

    auto AveragePos = [](const InlineVector<int> &p) {
        CHECK(!p.empty());
        double sum = 0.0;
        for (int x : p) sum += x;
        return sum / p.size();
      };

    // For variables, we want these all to end up on the exterior
    // so that we can be done with them. They can either be on
    // the left or the right of the action. We use the largest pos
    // as the "middle".
    //
    // Wherever we have more than one position, we use the mean
    // position, since this would optimistically require the fewest
    // layers to swap the props to that same spot.
    for (const auto &[prop, ps] : positions) {
      double avg_pos = AveragePos(ps);
      if (std::holds_alternative<Var>(prop.p)) {
        // We pick the farthest class (2) for unique variables,
        // then the outer class (1) for other variables.
        int cls = ps.size() == 1 ? 2 : 1;

        if (avg_pos <= largest_pos) {
          proj.emplace_back(prop, ps.size(), -cls * width + avg_pos);
        } else {
          proj.emplace_back(prop, ps.size(), cls * width + avg_pos);
        }
      } else {
        // Everything else just goes in the main class, targeting
        // its average position.

        proj.emplace_back(prop, ps.size(), avg_pos);
      }
    }

    std::sort(proj.begin(), proj.end(),
              [](const auto &a, const auto &b) {
                return std::get<2>(a) < std::get<2>(b);
              });

    if (!debug_filename.empty()) {
      std::string content = "--- Top layer props ---\n";
      int prop_idx = 0;
      for (const LC &lc : layer) {
        CellLibrary::Info info = library.GetInfo(lc.cell);
        for (size_t i = 0; i < lc.inprops.size(); i++) {
          const Prop &prop = lc.inprops[i];
          char c = 'M';
          if (info.inputs[i].type == CType::ZERO) c = '0';
          else if (info.inputs[i].type == CType::ONE) c = '1';
          content.append(std::format(" [{}] {:c}: {}\n", prop_idx++, c,
                                     PropString(prop, 8)));
        }
      }
      content.append("\n--- Prop ranks ---\n");
      for (int i = 0; i < (int)proj.size(); i++) {
        const auto &[prop, ct, pos] = proj[i];
        size_t sz = PropSize(prop);
        content.append(
            std::format(" [{}] size {}. x{}. {} (pos: {:.2f})\n",
                        i, sz, ct, PropString(prop, 8), pos));
      }
      Util::WriteFile(debug_filename, content);
      if (verbose > 0 && status != nullptr) {
        status->Print("Wrote {}\n", debug_filename);
      }
    }

    std::unordered_map<Prop, int> prop_ranks;
    prop_ranks.reserve(proj.size());
    for (int i = 0; i < (int)proj.size(); i++) {
      prop_ranks[std::get<0>(proj[i])] = i;
    }

    return prop_ranks;
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
    this->spring_records.clear();

    // First create a layer with just wires to get us started. This is
    // probably not necessary, but it makes it easier to reason about
    // the steady state and makes sure that we don't get an empty
    // circuit.

    {
      std::vector<LC> final_layer;
      std::vector<SpringRecord> final_springs;
      int cur_pos = 0;
      for (int i = 0; i < props.size(); i++) {
        if (i > 0) {
          int pad = 2 * library.MinClearanceFar();
          final_layer.push_back(LC{
            .inprops = {},
            .cell = CellLibrary::Spacer(pad),
          });
          cur_pos += pad;
        }
        LC lc{
          .inprops = {props[i]},
          .cell = CellLibrary::Wire(0, CellLibrary::Bias::LEFT),
        };
        final_layer.push_back(lc);
        SpringRecord rec;
        rec.start_pos = cur_pos;
        rec.ideal_pos = cur_pos;
        final_springs.push_back(rec);
      }
      layers.push_front(std::move(final_layer));
      this->spring_records.push_front(std::move(final_springs));
    }

    // A map for all propositions (including their subterms) in
    // the problem to the rank (not necessarily dense) in the
    // desired order. This may change as we make progress on
    // the network.
    CHECK(!layers.empty());
    std::unordered_map<Prop, int> prop_ranks =
      GetPropRanks(layers.front(), write_ranks ? "initial-ranks.txt" : "");

    bool write_ranks_next = false;
    // Repeatedly take the front of the layers, and simplify.
    for (int num_layers = 1; true; num_layers++) {
      if (num_layers % 500 == 0) {
        write_ranks_next = true;
      }

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

      DoAddLayer(&layers, prop_ranks);

      CHECK(!layers.empty());
      if (DefiniteProgress(layers.front())) {
        std::string file;
        if (write_ranks && write_ranks_next) {
          file = std::format("ranks-{}.txt", num_layers);
        }
        prop_ranks = GetPropRanks(layers.front(), file);
        resorted++;
        write_ranks_next = false;
      }
    }
  }

  // Delete unnecessary left/right padding, and convert to the Layout
  // format.
  Layout Finalize(std::deque<std::vector<LC>> layers,
                  std::vector<std::pair<int, CType>> vars) {
    std::optional<int> min_left;
    std::optional<int> min_right;
    for (const std::vector<LC> &layer : layers) {
      int left = (!layer.empty() && layer.front().cell.gate == Gate::SPACER)
        ? layer.front().cell.v : 0;
      min_left = !min_left.has_value() ? left : std::min(*min_left, left);

      int right = (layer.size() > 1 && layer.back().cell.gate == Gate::SPACER)
        ? layer.back().cell.v : 0;
      min_right = !min_right.has_value() ? right : std::min(*min_right, right);
    }

    if (min_left.value_or(0) > 0) {
      for (std::vector<LC> &layer : layers) {
        layer.front().cell.v -= *min_left;
        if (layer.front().cell.v == 0) {
          layer.erase(layer.begin());
        }
      }
      for (std::vector<SpringRecord> &shr : this->spring_records) {
        for (SpringRecord &rec : shr) {
          rec.start_pos -= *min_left;
          rec.ideal_pos -= *min_left;
        }
      }
    }

    if (min_right.value_or(0) > 0) {
      for (std::vector<LC> &layer : layers) {
        if (!layer.empty()) {
          layer.back().cell.v -= *min_right;
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
    world(world), library(library), rc("layout") {
    status_owned.reset(new StatusBar(4));
    status = status_owned.get();

    ComputeMinOutputDistance();
    ComputeMaxCellWidth();

    for (int s : CellLibrary::WIRE_SIZES) {
      wire_sizes_descending.push_back(s);
      wire_sizes_available.insert(s);
    }
    std::sort(wire_sizes_descending.begin(),
              wire_sizes_descending.end(),
              [](int a, int b) {
                return a > b;
              });

    for (int s : CellLibrary::WIRE_SIZES) {
      for (int t : CellLibrary::WIRE_SIZES) {
        if (s >= t) {
          int sum = s + t;
          int dif = s - t;

          if (!wire_sizes_available.contains(sum))
            wire_sums[sum].emplace_back(s, t);

          if (!wire_sizes_available.contains(dif))
            wire_sums[dif].emplace_back(s, t);
        }
      }
    }
  }

};

}  // namespace

std::unique_ptr<LayoutEngine> LayoutEngine::Create(
    const CellLibrary &library, const World &world) {
  return std::make_unique<LayoutEngineImpl>(library, world);
}

LayoutEngine::LayoutEngine() {}
LayoutEngine::~LayoutEngine() {}


std::string LayoutEngine::Serialize(const Layout &layout) {
  std::string out;
  bool first = true;
  for (const auto &[var, ctype] : layout.input_vars) {
    if (!first) out += " ";
    char c = '?';
    switch (ctype) {
    case CType::ZERO: c = 'O'; break;
    case CType::ONE: c = 'I'; break;
    case CType::MIXED: c = 'M'; break;
    }
    out += std::to_string(var) + c;
    first = false;
  }
  out += "\n";
  out += SerializeCircuit(layout.circuit);
  return out;
}

std::optional<Layout> LayoutEngine::Parse(std::string_view content) {
  size_t pos = content.find('\n');
  if (pos == std::string_view::npos) {
    return std::nullopt;
  }

  std::string_view vars_line = content.substr(0, pos);
  if (!vars_line.empty() && vars_line.back() == '\r') {
    vars_line.remove_suffix(1);
  }
  std::string_view circuit_content = content.substr(pos + 1);

  Layout layout;
  while (!vars_line.empty()) {
    Util::ConsumePrefixMatching([](char c) { return c == ' '; }, &vars_line);
    if (vars_line.empty()) break;

    std::string_view digits = Util::ConsumePrefixMatching(
        [](char c) { return c >= '0' && c <= '9'; }, &vars_line);
    if (digits.empty()) return std::nullopt;

    int var = 0;
    std::from_chars(digits.data(), digits.data() + digits.size(), var);

    if (vars_line.empty()) return std::nullopt;
    char c = vars_line[0];
    vars_line.remove_prefix(1);

    CType ctype;
    if (c == 'O') {
      ctype = CType::ZERO;
    } else if (c == 'I') {
      ctype = CType::ONE;
    } else if (c == 'M') {
      ctype = CType::MIXED;
    } else {
      return std::nullopt;
    }

    layout.input_vars.emplace_back(var, ctype);
  }

  std::optional<Circuit> circuit = ParseCircuit(circuit_content);
  if (!circuit.has_value()) return std::nullopt;

  layout.circuit = std::move(circuit.value());
  return layout;
}

std::string LayoutEngine::ToString(const Layout &layout) {
  std::string out;
  AppendFormat(&out, "Layout ({} inputs, {} layers):\n",
               layout.input_vars.size(), layout.circuit.layers.size());

  out += "Inputs: ";
  bool first = true;
  for (const auto &[var, ctype] : layout.input_vars) {
    if (!first) out += " ";
    char c = '?';
    switch (ctype) {
    case CType::ZERO: c = 'O'; break;
    case CType::ONE: c = 'I'; break;
    case CType::MIXED: c = 'M'; break;
    }
    AppendFormat(&out, "{}{}", var, c);
    first = false;
  }
  out += "\n";

  for (size_t i = 0; i < layout.circuit.layers.size(); i++) {
    AppendFormat(&out, "Layer {}: ", i);
    const std::vector<Cell> &layer = layout.circuit.layers[i];
    for (size_t j = 0; j < layer.size(); j++) {
      if (j > 0) out += ", ";
      AppendFormat(&out, "{}", CellString(layer[j]));
    }
    out += "\n";
  }

  return out;
}

int LayoutEngine::RedundantInputs(const Layout &layout) {
  std::unordered_set<int> saw;
  int redundant = 0;
  for (const auto &[v, t] : layout.input_vars) {
    if (saw.contains(v)) {
      redundant++;
    } else {
      saw.insert(v);
    }
  }

  return redundant;
}

std::string LayoutEngine::LayoutInfo(const Layout &layout) {
  int redundant = RedundantInputs(layout);
  std::string rs;
  if (redundant > 0) {
    rs = std::format(" ({} red.)", redundant);
  }
  return std::format("{} layers, {} inputs{}, {} cells",
                     layout.circuit.layers.size(),
                     layout.input_vars.size(),
                     rs,
                     CircuitSize(layout.circuit));
}

Layout LayoutEngine::Normalize(Layout layout) {
  for (Layer &layer : layout.circuit.layers) {
    Layer new_layer;
    for (const Cell &cell : layer) {
      if (cell.gate == Gate::SPACER) {
        if (cell.v <= 0) continue;
        if (!new_layer.empty() && new_layer.back().gate == Gate::SPACER) {
          new_layer.back().v += cell.v;
        } else {
          new_layer.push_back(cell);
        }
      } else {
        new_layer.push_back(cell);
      }
    }
    if (!new_layer.empty() && new_layer.back().gate == Gate::SPACER) {
      new_layer.pop_back();
    }
    layer = std::move(new_layer);
  }

  std::optional<int> min_left;
  for (const Layer &layer : layout.circuit.layers) {
    if (layer.empty()) continue;
    int left = (layer.front().gate == Gate::SPACER) ? layer.front().v : 0;
    if (!min_left.has_value() || left < *min_left) {
      min_left = left;
    }
  }

  if (min_left.value_or(0) > 0) {
    for (Layer &layer : layout.circuit.layers) {
      if (layer.empty()) continue;
      if (layer.front().gate == Gate::SPACER) {
        layer.front().v -= *min_left;
        if (layer.front().v == 0) {
          layer.erase(layer.begin());
        }
      }
    }
  }

  return layout;
}

