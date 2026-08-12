
#include "layout-canvas.h"

#include <algorithm>
#include <cstdlib>
#include <format>
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
      int xpos) const {
  const int min_clearance_far = library.MinClearanceFar();
  const int min_gate_dist = Levels::IN_WIDTH + 2 * min_clearance_far;

  CellLibrary::Info info = library.GetInfo(cell);
  int cell_left = xpos;
  int cell_right = xpos + info.block_width;

  // Check physical overlap with already placed cells in the next layer.
  // The clearance can be arbitrarily close as long as they are not touching.
  auto overlap_it = std::lower_bound(next.begin(), next.end(), cell_left,
                                     [](const PC &pc, int x) {
                                       return pc.xpos < x;
                                     });
  if (overlap_it != next.begin()) overlap_it--;
  for (auto it = overlap_it; it != next.end(); it++) {
    int pc_left = it->xpos;
    if (pc_left >= cell_right) break;
    int pc_right = pc_left + library.GetInfo(it->cell).block_width;
    if (cell_left < pc_right && cell_right > pc_left) {
      if (verbose > 1) {
        Print("[{}] Can't place {} at {}: Would overlap cell at x={}\n",
              chute_context, CellString(cell), xpos, pc_left);
      }
      return false;
    }
  }

  // Did we consume this chute with the hypothetical cell?
  auto MatchedHere = [&](const Chute &chute) {
    for (const CellLibrary::IO &out : info.outputs) {
      if (xpos + out.xblock == chute.pos) {
        return true;
      }
    }
    return false;
  };

  // Check clearance against unassigned chutes.
  // The cell body cannot physically cover any unassigned chute it doesn't
  // consume, and we must leave enough clearance (min_clearance_far) so
  // that a 0-displacement wire can later be placed on the chute.
  int min_chute_pos = cell_left - Levels::IN_WIDTH - min_clearance_far;
  auto chute_overlap_it = std::lower_bound(
      chutes.begin(), chutes.end(), min_chute_pos,
      [](const Chute &c, int pos) { return c.pos < pos; });

  for (auto it = chute_overlap_it; it != chutes.end(); it++) {
    int chute_left = it->pos - min_clearance_far;
    if (chute_left >= cell_right) break;

    if (!it->assigned) {
      if (MatchedHere(*it)) continue;

      int chute_right = it->pos + Levels::IN_WIDTH + min_clearance_far;
      if (cell_left < chute_right && cell_right > chute_left) {
        if (verbose > 1) {
          Print("[{}] Can't place {} at {}: Would physically overlap "
                "unassigned chute at {}\n",
                chute_context, CellString(cell), xpos, it->pos);
        }
        return false;
      }
    }
  }

  // Check clearance between active gates.
  // To ensure we don't get stuck routing, every input gate must have enough
  // clearance to place a small wire in either orientation. This translates
  // to a minimum edge-to-edge distance of 2 * min_clearance_far.
  // Note: Outputs are at the bottom and do not need routing clearance.
  for (const CellLibrary::IO &in : info.inputs) {
    int cg = xpos + in.xblock;
    int min_ng = cg - min_gate_dist;
    int max_ng = cg + min_gate_dist;

    auto next_it = std::lower_bound(
        next.begin(), next.end(), min_ng,
        [](const PC &pc, int x) { return pc.xpos < x; });
    if (next_it != next.begin()) next_it--;

    for (auto it = next_it; it != next.end(); it++) {
      if (it->xpos > max_ng) break;
      CellLibrary::Info pc_info = library.GetInfo(it->cell);
      if (it->xpos + pc_info.block_width < min_ng) continue;

      for (const CellLibrary::IO &pc_in : pc_info.inputs) {
        int ng = it->xpos + pc_in.xblock;
        if (std::abs(cg - ng) < min_gate_dist) {
          if (verbose > 1) {
            Print("[{}] Can't place {} at {}: Input gate at {} is too close "
                  "(dist {}) to neighboring input gate at {}.\n",
                  chute_context, CellString(cell), xpos, cg,
                  std::abs(cg - ng), ng);
          }
          return false;
        }
      }
    }

    auto chute_it = std::lower_bound(
        chutes.begin(), chutes.end(), min_ng,
        [](const Chute &c, int pos) { return c.pos < pos; });

    for (auto it = chute_it; it != chutes.end(); it++) {
      if (it->pos > max_ng) break;
      if (!it->assigned && !MatchedHere(*it)) {
        int ng = it->pos;
        if (std::abs(cg - ng) < min_gate_dist) {
          if (verbose > 1) {
            Print("[{}] Can't place {} at {}: Input gate at {} is too close "
                  "(dist {}) to neighboring input gate at {}.\n",
                  chute_context, CellString(cell), xpos, cg,
                  std::abs(cg - ng), ng);
          }
          return false;
        }
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

// Calculates the length of a single spring given a global
// tension/compression force f.
static double SpringLength(const LayoutCanvas::Spring &s, double f) {
  double dist = s.target_dist + (f > 0 ? (f / s.expand) : (f / s.compress));
  return std::max((double)s.min_dist, dist);
}

// Solves a single contiguous segment of springs between two anchored chutes.
static void SolveSpringSegment(
    const std::vector<LayoutCanvas::Spring> &springs,
    std::vector<double> &xpos,
    int start_idx,
    int end_idx) {
  static constexpr int CHUTE_WIDTH = Levels::IN_WIDTH;

  if (end_idx - start_idx <= 0)
    return;

  // The minimal required space the springs must fill.
  double total_chute_width = 0;
  for (int i = start_idx; i < end_idx; i++) {
    total_chute_width += CHUTE_WIDTH;
  }

  // Distance between anchors, minus the physical width of the chutes
  // in between.
  double target_total_length =
      xpos[end_idx] - xpos[start_idx] - total_chute_width;

  // The min possible length to avoid violating min_dist.
  double min_possible_length = 0;
  for (int i = start_idx; i < end_idx; i++) {
    min_possible_length += springs[i].min_dist;
  }

  if (target_total_length <= min_possible_length) {
    // Impossible to satisfy all min_dists without overrunning the anchor.
    // We must violate min_dist. Distribute the available space based on
    // the spring compression rates.
    double total_target = 0;
    double total_inv_comp = 0;
    for (int i = start_idx; i < end_idx; i++) {
      total_target += springs[i].target_dist;
      total_inv_comp += 1.0 / springs[i].compress;
    }

    if (total_inv_comp > 1e-6) {
      double F = (target_total_length - total_target) / total_inv_comp;
      double current_x = xpos[start_idx];
      for (int i = start_idx; i < end_idx; i++) {
        xpos[i] = current_x;
        double dist = springs[i].target_dist + (F / springs[i].compress);
        current_x += CHUTE_WIDTH + dist;
      }
    } else {
      double step = (xpos[end_idx] - xpos[start_idx]) / (end_idx - start_idx);
      for (int i = start_idx; i < end_idx; i++) {
        xpos[i] = xpos[start_idx] + (i - start_idx) * step;
      }
    }
    return;
  }

  // Binary search to find the equilibrium force 'f'.
  double f_min = -1000000.0;
  double f_max = 1000000.0;

  // Dynamically expand bounds if necessary.
  auto TotalLen = [&](double F) {
      double len = 0;
      for (int i = start_idx; i < end_idx; i++)
        len += SpringLength(springs[i], F);
      return len;
    };

  while (TotalLen(f_min) > target_total_length)
    f_min *= 2.0;
  while (TotalLen(f_max) < target_total_length)
    f_max *= 2.0;

  // 64 iterations of binary search provides enough precision for
  // IEEE double.
  double f_mid = 0;
  for (int iter = 0; iter < 64; iter++) {
    f_mid = (f_min + f_max) * 0.5;
    double current_length = TotalLen(f_mid);

    if (current_length < target_total_length) {
      f_min = f_mid;
    } else {
      f_max = f_mid;
    }
  }

  // 4. Apply the final calculated positions
  double current_x = xpos[start_idx];
  for (int i = start_idx; i < end_idx; i++) {
    xpos[i] = current_x;
    current_x += CHUTE_WIDTH + SpringLength(springs[i], f_mid);
  }
}

// Solve the whole system
std::vector<double> LayoutCanvas::SolveSprings() {
  if (chutes.empty())
    return {};

  // Require at least one anchored chute so that the entire circuit
  // doesn't drift. If none are anchored, anchor the middle one.
  bool any_anchored = false;
  for (const Chute &chute : chutes) {
    if (chute.anchored) {
      any_anchored = true;
      break;
    }
  }
  if (!any_anchored) {
    chutes[chutes.size() / 2].anchored = true;
  }

  std::vector<double> xpos(chutes.size());
  for (int i = 0; i < (int)chutes.size(); i++) {
    xpos[i] = (double)chutes[i].pos;
  }

  int first_anchor_idx = -1;
  int last_anchor_idx = -1;

  for (int i = 0; i < (int)chutes.size(); i++) {
    if (chutes[i].anchored) {
      if (first_anchor_idx == -1) {
        first_anchor_idx = i;
      }
      if (last_anchor_idx != -1) {
        // Solve the chunk between the previous anchor and this anchor
        SolveSpringSegment(springs, xpos, last_anchor_idx, i);
      }
      last_anchor_idx = i;
    }
  }

  // Handle "loose ends" (chutes before the first anchor, or
  // after the last anchor). Because they are loose, tension F = 0.
  // They just assume their target_dist (or min_dist, whichever is larger).
  for (int i = first_anchor_idx - 1; i >= 0; --i) {
    xpos[i] = xpos[i + 1] - Levels::IN_WIDTH - SpringLength(springs[i], 0.0);
  }

  for (int i = last_anchor_idx + 1; i < (int)chutes.size(); i++) {
    xpos[i] = xpos[i - 1] +
      Levels::IN_WIDTH + SpringLength(springs[i - 1], 0.0);
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
    int mcf = library.MinClearanceFar();
    for (int i = 0; i < (int)chutes.size() - 1; i++) {
      if (Assigned(i) || Assigned(i + 1)) continue;
      int dist = chutes[i + 1].pos - (chutes[i].pos + Levels::IN_WIDTH);
      if (dist < 2 * mcf) {
        if (!close_str.empty()) close_str += ", ";
        close_str += std::format("{}-{} (dist {})", i, i + 1, dist);
      }
    }
  if (!stuck_chutes.empty() || !close_str.empty()) {
    Print("Stuck circuit!\n{}\n\n", DebugString());

    // We're going to abort, but get a clearer error message!
    // Turn off verbosity for the probes.
    verbose = 0;

    std::string stuck_str;
    for (int idx : stuck_chutes) {
      if (!stuck_str.empty()) stuck_str += ", ";
      stuck_str += std::to_string(idx);
    }
    if (stuck_str.empty()) stuck_str = "none (complex blockage)";

    if (!close_str.empty()) {
      close_str = std::format(
          "\nVery close unassigned chutes (mcf={}): [{}]",
          mcf, close_str);
    }

    LOG(FATAL) << "Input chutes are already in a state "
      "where we're stuck! Stuck chutes: [" << stuck_str << "]" <<
      close_str << "\n";
  }
}
