
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
  case UNDUP: return "UNDUP";
  case UNSEPARATE: return "UNSEPARATE";
  case EXCHANGE_LEFT: return "EXCHANGE_LEFT";
  case EXCHANGE_RIGHT: return "EXCHANGE_RIGHT";
  case FLOW: return "FLOW";
  case QUIESCE: return "QUIESCE";
  default: return "??BAD DESIRETYPE??";
  }
}

std::string LayoutCanvas::ChuteString(const Chute &chute) {
  std::string s;
  std::string ds, dv;
  if (chute.desire != UNSPECIFIED)
    ds = std::format(", desire={}", DesireTypeString(chute.desire));
  if (chute.desire_val != 0)
    dv = std::format(", val={}", chute.desire_val);
  AppendFormat(&s, "Chute(pos={}, prop={}, type={}{}{})",
               chute.pos,
               PropString(chute.prop, 6),
               TypeString(chute.type),
               ds, dv);
  return s;
}

LayoutCanvas::LayoutCanvas(const CellLibrary &library) :
  library(library) {
}

void LayoutCanvas::Reset(std::vector<Chute> chutes_in) {
  chutes = std::move(chutes_in);
  assigned.resize(chutes.size(), false);
  next.clear();
}

void LayoutCanvas::SetVerbose(int v) { verbose = v; }

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
          .desire_val = 0,
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
  const int min_clearance_close = library.MinClearanceClose();
  const int min_clearance_far = library.MinClearanceFar();

  CellLibrary::Info info = library.GetInfo(cell);
  int cell_left = xpos;
  int cell_right = xpos + info.block_width;

  // Overlapping something already placed in the next layer?
  for (const PC &pc : next) {
    int pc_left = pc.xpos;
    int pc_right = pc_left + library.GetInfo(pc.cell).block_width;
    if (cell_left < pc_right && cell_right > pc_left) {
      if (verbose > 1) {
        Print("[{}] Can't place {} at {}: Would overlap cell at x={}\n",
              chute_context,
              CellString(cell), xpos, pc.xpos);
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
    for (const PC &pc : next) {
      CellLibrary::Info pc_info = library.GetInfo(pc.cell);
      for (const CellLibrary::IO &pc_in : pc_info.inputs) {
        int pc_in_pos = pc.xpos + pc_in.xblock;
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
          if (lidx >= 0 && !assigned[lidx] && !MatchedHere(chutes[lidx])) {
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
              !assigned[ridx] &&
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
    if (!assigned[i]) {
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
  std::sort(next.begin(), next.end(),
            [](const PC &a, const PC &b) {
              return a.xpos < b.xpos;
            });

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
