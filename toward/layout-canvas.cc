
#include "layout-canvas.h"

#include <algorithm>
#include <cstdlib>
#include <format>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "base/stringprintf.h"
#include "cell-library.h"
#include "circuit.h"
#include "level.h"
#include "prop.h"

using LC = LayoutCanvas::LC;
using Chute = LayoutCanvas::Chute;
using PC = LayoutCanvas::PC;

std::string_view LayoutCanvas::DesireTypeString(DesireType dt) {
  switch (dt) {
  case UNSPECIFIED: return "UNSPECIFIED";
  case DECOMPOSE: return "DECOMPOSE";
  case UNCOMBINE: return "UNCOMBINE";
  case UNDUP_LHS: return "UNDUP_LHS";
  case UNDUP_RHS: return "UNDUP_RHS";
  case UNSEPARATE_LHS: return "UNSEPARATE_LHS";
  case UNSEPARATE_RHS: return "UNSEPARATE_RHS";
  case EXCHANGE_LEFT: return "EXCHANGE_LEFT";
  case EXCHANGE_RIGHT: return "EXCHANGE_RIGHT";
  case QUIESCE: return "QUIESCE";
  default: return "??BAD DESIRETYPE??";
  }
}

std::string LayoutCanvas::ChuteString(const Chute &chute) {
  std::string s;
  std::string ds, anc;
  if (chute.desire != UNSPECIFIED)
    ds = std::format(", desire={}", DesireTypeString(chute.desire));
  if (!chute.assigned && chute.anchored)
    anc = AGREEN(" (anchor)");
  AppendFormat(&s, "Chute(pos={}, prop={}, type={}{}{})",
               chute.pos,
               PropString(chute.prop, 6),
               TypeString(chute.type),
               ds, anc);
  return s;
}

void LayoutCanvas::UpdateSpring(Spring *spring,
                                int target_dist,
                                int min_dist,
                                float compress,
                                float expand) {
  bool fresh = spring->target_dist < 0;

  if (fresh) spring->target_dist = target_dist;
  else spring->target_dist += target_dist;

  spring->min_dist = std::max(spring->min_dist, min_dist);

  if (fresh) {
    spring->compress = compress;
    spring->expand = expand;
  } else {
    spring->compress = std::max(spring->compress, compress);
    spring->expand = std::max(spring->expand, expand);
  }
}


LayoutCanvas::LayoutCanvas(const CellLibrary &library) :
  library(library) {
}

void LayoutCanvas::Reset(std::vector<Chute> chutes_in) {
  chutes = std::move(chutes_in);
  CHECK(!chutes.empty()) << "Layout Canvas requires non-empty layer";

  for (Chute &chute : chutes) {
    chute.assigned = false;
  }

  springs = std::vector<Spring>(chutes.size() - 1);
  next.clear();
}

void LayoutCanvas::SetVerbose(int v) { verbose = v; }

// Is the chute already assigned to an output on the new layer?
bool LayoutCanvas::Assigned(int chute_idx) const {
  CHECK(chute_idx >= 0 && chute_idx < chutes.size());
  return chutes[chute_idx].assigned;
}

// Mark a chute as assigned (only once).
void LayoutCanvas::Assign(int chute_idx) {
  CHECK(chute_idx >= 0 && chute_idx < chutes.size() &&
        !Assigned(chute_idx));
  chutes[chute_idx].assigned = true;
  chutes[chute_idx].anchored = true;
}

  // Flatten the inputs. Desires are not yet specified.
std::vector<Chute> LayoutCanvas::FlattenInputs(
    std::span<const LC> top) const {
  std::vector<Chute> chutes;
  // Current position (left edge of the next cell in the top layer,
  // in blocks).
  int pos = 0;
  for (const LC &lc : top) {
    CellLibrary::Info info = library.GetInfo(lc.cell);
    CHECK(info.inputs.size() == lc.inprops.size()) <<
      info.inputs.size() << " " << lc.inprops.size();

    for (int i = 0; i < lc.inprops.size(); i++) {
      const Prop &prop = lc.inprops[i];
      const CellLibrary::IO &io = info.inputs[i];

      int input_pos = pos + io.xblock;
      chutes.emplace_back(Chute{
          .pos = input_pos,
          .prop = prop,
          .type = io.type,
          .desire = DesireType::UNSPECIFIED,
        });
    }
    pos += info.block_width;
  }

  return chutes;
}

bool LayoutCanvas::CanPlaceCell(
      // The chute idx requesting this (just used for debug output).
      int chute_context,
      const Cell &cell,
      int xpos,
      bool check_stuck) const {
  const int min_clearance_close = check_stuck ? library.MinClearanceClose() : 0;
  const int min_clearance_far = check_stuck ? library.MinClearanceFar() : 0;

  CellLibrary::Info info = library.GetInfo(cell);
  int cell_left = xpos;
  int cell_right = xpos + info.block_width;

  // Overlapping something already placed in the next layer?
  auto overlap_it = std::lower_bound(next.begin(), next.end(), cell_left,
                                     [](const PC &pc, int x) {
                                       return pc.xpos < x;
                                     });
  if (overlap_it != next.begin()) --overlap_it;
  for (auto it = overlap_it; it != next.end(); ++it) {
    int pc_left = it->xpos;
    if (pc_left >= cell_right) break;
    int pc_right = pc_left + library.GetInfo(it->cell).block_width;
    if (cell_left < pc_right && cell_right > pc_left) {
      if (verbose > 1) {
        Print("[{}] Can't place {} at {}: Would overlap cell at x={}\n",
              chute_context,
              CellString(cell), xpos, pc_left);
      }
      return false;
    }
  }

  // Check if the inputs of the hypothetical cell are too close to the
  // inputs of already placed cells. If so, they would become stuck chutes
  // on the next layer.
  int min_input_dist = Levels::OUT_WIDTH + 2 * min_clearance_close;
  for (const CellLibrary::IO &in : info.inputs) {
    int in_pos = xpos + in.xblock;
    auto pc_it = std::lower_bound(
        next.begin(), next.end(), in_pos - min_input_dist,
        [](const PC &pc, int x) {
          return pc.xpos < x;
        });
    if (pc_it != next.begin()) --pc_it;
    for (auto it = pc_it; it != next.end(); ++it) {
      if (it->xpos >= in_pos + min_input_dist) break;
      CellLibrary::Info pc_info = library.GetInfo(it->cell);
      for (const CellLibrary::IO &pc_in : pc_info.inputs) {
        int pc_in_pos = it->xpos + pc_in.xblock;
        if (std::abs(in_pos - pc_in_pos) < min_input_dist) {
          if (verbose > 1) {
            Print("[{}] Can't place {} at {}: "
                  "Input at {} is too close to already placed input at {}.\n",
                  chute_context,
                  CellString(cell), xpos, in_pos, pc_in_pos);
          }
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
  std::vector<std::optional<bool>> ok_left(chutes.size(), std::nullopt);
  // And symmetrically for the right.
  std::vector<std::optional<bool>> ok_right(chutes.size(), std::nullopt);

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
        const Chute &chute = chutes[cidx];

        int c_left = chute.pos - req_left;
        int c_right = chute.pos + Levels::OUT_WIDTH + req_right;

        // Check the hypothetical cell.
        if (cell_left < c_right && cell_right > c_left) {
          memo = {false};
          return false;
        }

        // Check already placed cells.
        auto pc_it = std::lower_bound(next.begin(), next.end(), c_left,
                                      [](const PC &pc, int x) {
                                        return pc.xpos < x;
                                      });
        if (pc_it != next.begin()) --pc_it;
        for (auto it = pc_it; it != next.end(); ++it) {
          int pc_left = it->xpos;
          if (pc_left >= c_right) break;
          int pc_right = pc_left + library.GetInfo(it->cell).block_width;
          if (pc_left < c_right && pc_right > c_left) {
            memo = {false};
            return false;
          }
        }

        {
          // Check left neighbor.
          int lidx = cidx - 1;
          if (lidx >= 0 && !Assigned(lidx) && !MatchedHere(chutes[lidx])) {
            const Chute &p_chute = chutes[lidx];
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
          if (ridx < (int)chutes.size() &&
              !Assigned(ridx) &&
              !MatchedHere(chutes[ridx])) {
            const Chute &n_chute = chutes[ridx];
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
  for (size_t i = 0; i < chutes.size(); i++) {
    if (!Assigned(i)) {
      const Chute &chute = chutes[i];
      if (MatchedHere(chute))
        continue;

      if (!ChuteStillHasSpace(i, true) &&
          !ChuteStillHasSpace(i, false)) {
        if (verbose > 1) {
          Print("[{}] Can't place {} at {}: "
                "Cell {} would be blocked.\n",
                chute_context,
                CellString(cell), xpos, i);
        }
        return false;
      }

    }
  }

  return true;
}


std::pair<std::vector<LC>, int>
LayoutCanvas::ConvertToLayer() {
  std::vector<LC> next_layer;
  int start_pos = next.empty() ? 0 : next[0].xpos;
  int current_x = start_pos;

  for (PC &pc : next) {
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


std::vector<double> LayoutCanvas::SolveSprings() {
  constexpr float ANCHOR_WEIGHT = 0.01f;

  // Depend on the number of chutes, since forces move 1 element per iteration.
  const int max_iters = 50 * chutes.size();

  // Desired left edge, initialized to the current location.
  std::vector<double> xpos(chutes.size());
  for (int i = 0; i < chutes.size(); i++) {
    xpos[i] = (double)chutes[i].pos;
  }

  for (int iter = 0; iter < max_iters; iter++) {
    for (int cidx = 0; cidx < xpos.size(); cidx++) {
      // If the chute is already anchored, its xpos cannot
      // change. It pushes and pulls neighbors in their own
      // updates.
      if (chutes[cidx].anchored)
        continue;

      // Weakly prefer the current position. We don't want the
      // whole network to drift to one side, for example.
      double weighted_pos = ANCHOR_WEIGHT * (double)chutes[cidx].pos;
      double total_weight = ANCHOR_WEIGHT;

      // Spring to the left.
      if (cidx > 0) {
        const Spring &spring = springs[cidx - 1];
        float current_dist = xpos[cidx] - (xpos[cidx - 1] + Levels::IN_WIDTH);
        float weight = (current_dist < spring.target_dist) ?
          spring.compress : spring.expand;

        weighted_pos += weight * (xpos[cidx - 1] +
                                  Levels::IN_WIDTH + spring.target_dist);
        total_weight += weight;
      }

      // Spring to the right.
      if (cidx < xpos.size() - 1) {
        const Spring &spring = springs[cidx];
        float current_dist = xpos[cidx + 1] - (xpos[cidx] + Levels::IN_WIDTH);

        float weight = (current_dist < spring.target_dist) ?
          spring.compress : spring.expand;

        weighted_pos += weight * (xpos[cidx + 1] -
                                  Levels::IN_WIDTH - spring.target_dist);
        total_weight += weight;
      }

      xpos[cidx] = weighted_pos / total_weight;

      // Never (well, subject to physical possibility) let chutes be
      // closer than the min distance (which also prohibits overlap).
      //
      // Normally checking the left neighbor alone would be sufficient,
      // but we skip chutes that are anchored (and can't move them).
      // So we also need to check overlaps to the right.
      //
      // Since we could violate both constraints at once, we compute
      // them up front and settle for the midpoint if we can't satisfy
      // both of them. This at least ensures that chutes don't get
      // reordered.
      std::optional<double> min_x, max_x;

      if (cidx > 0) {
        min_x = xpos[cidx - 1] + Levels::IN_WIDTH +
          springs[cidx - 1].min_dist;
      }

      if (cidx < xpos.size() - 1) {
        max_x = xpos[cidx + 1] - Levels::IN_WIDTH -
          springs[cidx].min_dist;
      }

      if (min_x.has_value() && max_x.has_value() &&
          min_x.value() > max_x.value()) {
        // If constrained on both sides and unable to satisfy both,
        // place the chute at the midpoint.
        xpos[cidx] = (min_x.value() + max_x.value()) / 2.0;
      } else {
        if (min_x.has_value() && xpos[cidx] < min_x.value())
          xpos[cidx] = min_x.value();
        if (max_x.has_value() && xpos[cidx] > max_x.value())
          xpos[cidx] = max_x.value();
      }

    }
  }

  return xpos;
}

std::string LayoutCanvas::DebugString() const {
  std::string s;
  AppendFormat(&s, "--- Chutes ---\n");
  for (int i = 0; i < (int)chutes.size(); i++) {
    AppendFormat(&s, " [{}] {}\n", i,
                 LayoutCanvas::ChuteString(chutes[i]));
  }
  AppendFormat(&s, "--- Next Cells ---\n");
  for (int i = 0; i < (int)next.size(); i++) {
    AppendFormat(&s, " [{}] xpos={} cell={}\n", i, next[i].xpos,
                 CellString(next[i].cell));
  }
  return s;
}

void LayoutCanvas::AddNext(int xpos, const Cell &cell,
                           std::vector<Prop> inprops) {
  auto it = std::lower_bound(
      next.begin(), next.end(), xpos,
      [](const PC &pc, int x) { return pc.xpos < x; });
  next.insert(it, PC{
      .xpos = xpos,
      .cell = cell,
      .inprops = std::move(inprops),
    });
}


void LayoutCanvas::CheckNotStuck() {
  // Some location that can't interfere with anything.
  const int clear_pos = chutes.back().pos +
    Levels::IN_WIDTH + 2 * library.MinClearanceFar() + 1;
  if (!CanPlaceCell(
          // Not a real chute
          -1,
          CellLibrary::Spacer(1),
          clear_pos,
          // Ensure safe clearance.
          true)) {

    Print("Stuck circuit!\n{}\n\n", DebugString());

    // We're going to abort, but get a clearer error message!
    // Turn off verbosity for the probes.
    verbose = 0;

    auto Overlaps = [&](const Cell &cell, int xpos) {
      int cell_left = xpos;
      int cell_right = xpos + library.GetInfo(cell).block_width;
      auto overlap_it = std::lower_bound(next.begin(), next.end(), cell_left,
                                         [](const PC &pc, int x) {
                                           return pc.xpos < x;
                                         });
      if (overlap_it != next.begin()) --overlap_it;
      for (auto it = overlap_it; it != next.end(); ++it) {
        int pc_left = it->xpos;
        if (pc_left >= cell_right) break;
        int pc_right = pc_left + library.GetInfo(it->cell).block_width;
        if (cell_left < pc_right && cell_right > pc_left) {
          return true;
        }
      }
      return false;
    };

    std::vector<int> stuck_chutes;
    for (int i = 0; i < (int)chutes.size(); i++) {
      if (Assigned(i)) continue;
      const Chute &chute = chutes[i];

      Cell wire_l = CellLibrary::Wire(0, CellLibrary::Bias::LEFT, chute.type);
      Cell wire_r = CellLibrary::Wire(0, CellLibrary::Bias::RIGHT, chute.type);

      int xl = chute.pos - library.GetInfo(wire_l).outputs[0].xblock;
      int xr = chute.pos - library.GetInfo(wire_r).outputs[0].xblock;

      if (Overlaps(wire_l, xl) && Overlaps(wire_r, xr)) {
        stuck_chutes.push_back(i);
      }
    }

    std::string stuck_str;
    for (int idx : stuck_chutes) {
      if (!stuck_str.empty()) stuck_str += ", ";
      stuck_str += std::to_string(idx);
    }
    if (stuck_str.empty()) stuck_str = "none (complex blockage)";

    std::string close_str;
    int mcc = library.MinClearanceClose();
    int mcf = library.MinClearanceFar();
    for (int i = 0; i < (int)chutes.size() - 1; i++) {
      if (Assigned(i) || Assigned(i + 1)) continue;
      int dist = chutes[i + 1].pos - (chutes[i].pos + Levels::OUT_WIDTH);
      if (dist < mcc + mcf) {
        if (!close_str.empty()) close_str += ", ";
        close_str += std::format("{}-{} (dist {})", i, i + 1, dist);
      }
    }
    if (!close_str.empty()) {
      close_str = std::format(
          "\nVery close unassigned chutes (mcc={}, mcf={}): [{}]",
          mcc, mcf, close_str);
    }

    LOG(FATAL) << "Input chutes are already in a state "
      "where we're stuck! Stuck chutes: [" << stuck_str << "]" <<
      close_str << "\n";
  }
}
