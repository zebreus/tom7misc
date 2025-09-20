
#include "hypercube.h"

#include <algorithm>
#include <cstdio>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "big-polyhedra.h"
#include "bignum/big-interval.h"
#include "bignum/big.h"
#include "threadutil.h"
#include "util.h"

static constexpr bool SELF_CHECK = false;

using Pt4Data = Hypercube::Pt4Data;
using Pt5Data = Hypercube::Pt5Data;
using Rejection = Hypercube::Rejection;

const char *ParameterName(int param) {
  switch (param) {
  case OUTER_AZIMUTH: return "O_AZ";
  case OUTER_ANGLE: return "O_AN";
  case INNER_AZIMUTH: return "I_AZ";
  case INNER_ANGLE: return "I_AN";
  case INNER_ROT: return "I_R";
  case INNER_X: return "I_X";
  case INNER_Y: return "I_Y";
  default: return "??";
  }
}

Hypercube::Hypercube() : bounds(MakeStandardBounds()), root(0),
                         nodes({Node(Leaf{.completed = 0})}) {
}

bool Hypercube::Empty() {
  MutexLock ml(&mu);
  if (nodes.size() > 1) return false;
  // Must be an incomplete leaf to be empty.
  if (const Leaf *leaf = std::get_if<Leaf>(&nodes[0])) {
    return leaf->completed == 0;
  }
  return false;
}

BigRat Hypercube::Hypervolume(const Volume &vol) {
  BigRat product(1);
  for (int d = 0; d < NUM_DIMENSIONS; d++) {
    product *= vol[d].Width();
  }
  return product;
}

std::string Hypercube::VolumeString(const Volume &volume) {
  auto DimString = [](const Bigival &bi) {
      BigRat w = bi.Width();
      double lb = bi.LB().ToDouble();
      double ub = bi.UB().ToDouble();

      std::string wstr;
      if (w > BigRat(1, int64_t{1024} * 1024 * 1024)) {
        wstr = std::format(ABLUE("width ") "{:.8f}", w.ToDouble());
      } else {
        const auto &[n, d] = w.Parts();
        wstr = std::format(AORANGE("tiny ") " denom {}", FormatNum(d));
      }

      // TODO: Use dynamic precision here.
      // TODO: Consider showing fractions when simple.
      return std::format(AGREY("[") "{:.8g}" AGREY(",") " {:.8g}" AGREY("]")
                         "   {}",
                         lb, ub, wstr);
    };

  return std::format("O φ:{}\n"
                     "O θ:{}\n"
                     "I φ:{}\n"
                     "I θ:{}\n"
                     "I α:{}\n"
                     "I x:{}\n"
                     "I y:{}",
                     DimString(volume[OUTER_AZIMUTH]),
                     DimString(volume[OUTER_ANGLE]),
                     DimString(volume[INNER_AZIMUTH]),
                     DimString(volume[INNER_ANGLE]),
                     DimString(volume[INNER_ROT]),
                     DimString(volume[INNER_X]),
                     DimString(volume[INNER_Y]));
}

static std::string SerializeRejection(const Rejection &rej) {
  std::string ret = std::format("{}", (uint8_t)rej.reason);
  if (const Pt4Data *p = std::get_if<Pt4Data>(&rej.data)) {
    AppendFormat(&ret, " {} {}", p->edge, p->point);
  } else if (const Pt5Data *p = std::get_if<Pt5Data>(&rej.data)) {
    AppendFormat(&ret, " {} {} {}", p->edge, p->point,
                 p->bias.ToString());
  } else {
    // Nothing.
    CHECK(std::holds_alternative<std::monostate>(rej.data)) << "Must "
      "be missing a variant?";
  }
  return ret;
}

static std::optional<Rejection> ParseRejection(std::string_view s) {
  // Format is
  //   reason_number additional_data
  Rejection ret;
  int64_t r = Util::ParseInt64(Util::Chop(&s), -1);
  if (r <= 0 || r >= NUM_REJECTION_REASONS) return std::nullopt;
  ret.reason = (RejectionReason)r;

  switch (ret.reason) {
  case REJECTION_UNKNOWN:
    LOG(FATAL) << "Checked above";
    break;
  case OUTSIDE_OUTER_PATCH:
  case OUTSIDE_INNER_PATCH:
  case OUTSIDE_OUTER_PATCH_BALL:
  case OUTSIDE_INNER_PATCH_BALL:
    // No metadata. Could keep the code?
    break;

  case CLOSE_TO_DIAGONAL:
    // No metadata.
    break;

  case POLY_AREA:
  case DIAMETER:
    // No metadata.
    break;

  case POINT_OUTSIDE1:
  case POINT_OUTSIDE2:
  case POINT_OUTSIDE3:
    // Abort if we see these old dead ones?
    break;

  case POINT_OUTSIDE4: {
    int64_t eidx = Util::ParseInt64(Util::Chop(&s), -1);
    int64_t pidx = Util::ParseInt64(Util::Chop(&s), -1);
    if (eidx < 0 || pidx < 0) return std::nullopt;
    ret.data = Pt4Data({.edge = (int8_t)eidx, .point = (int8_t)pidx});
    break;
  }

  case POINT_OUTSIDE6:
  case POINT_OUTSIDE5: {
    int64_t eidx = Util::ParseInt64(Util::Chop(&s), -1);
    int64_t pidx = Util::ParseInt64(Util::Chop(&s), -1);
    BigRat b(Util::NormalizeWhitespace(Util::Chop(&s)));
    if (eidx < 0 || pidx < 0) return std::nullopt;
    // Bias cannot be zero; it's probably missing.
    if (b == 0) return std::nullopt;
    ret.data = Pt5Data({
        .edge = (int8_t)eidx,
        .point = (int8_t)pidx,
        .bias = std::move(b),
      });
    break;
  }
  }

  return ret;
}

std::string Hypercube::StandardFilename(
    uint64_t outer_code, uint64_t inner_code) {
  return std::format("hc-{}-{}.cube", outer_code, inner_code);
}

// This is a hack; would be better to at least loop over lines!
bool Hypercube::IsComplete(std::string_view contents) {
  return !contents.empty() &&
    contents[0] != 'E' &&
    contents.find("\nE\n") == std::string_view::npos;
}


void Hypercube::FromDisk(std::string_view filename) {
  return FromString(Util::ReadFile(filename));
}


void Hypercube::FromString(std::string_view contents) {
  // The file format is line based. Each line is a node;
  // either:
  // L reason completed   (completed leaf)
  // or
  // E                    (empty leaf)
  // or
  // S axis split
  //
  // The nodes are in post order, so we process them with
  // a stack:

  std::vector<Hypercube::Node> new_nodes;
  new_nodes.reserve(std::count(contents.begin(), contents.end(), '\n'));

  std::vector<int64_t> stack;

  Util::ForEachLineInString(
      contents,
      [&](std::string_view raw_line) {

        std::string line_string = Util::NormalizeWhitespace(raw_line);
        std::string_view line(line_string);
        if (line.empty()) return;

        char cmd = line[0];
        line.remove_prefix(1);
        if (!line.empty() && line[0] == ' ')
          line.remove_prefix(1);

        if (cmd == 'E') {
          const int64_t idx = new_nodes.size();
          new_nodes.emplace_back(Leaf{
              .completed = 0,
              .rejection = {.reason = REJECTION_UNKNOWN},
            });
          stack.push_back(idx);

        } else if (cmd == 'L') {
          int64_t comp = Util::ParseInt64(Util::Chop(&line), -1);
          CHECK(comp >= 0) << line;

          std::optional<Rejection> rej = ParseRejection(line);
          CHECK(rej.has_value()) << raw_line;

          const int64_t idx = new_nodes.size();
          new_nodes.emplace_back(Leaf{
              .completed = comp,
              .rejection = rej.value(),
            });
          stack.push_back(idx);

        } else if (cmd == 'S') {
          int axis = Util::ParseInt64(Util::Chop(&line), -1);
          CHECK(axis >= 0 && axis < NUM_DIMENSIONS) << raw_line;
          BigRat split_pt(Util::Chop(&line));

          CHECK(stack.size() >= 2) << "Saw split node, so there "
            "should be two children in the stack!";

          const int64_t idx = new_nodes.size();
          new_nodes.emplace_back(Internal{
              .axis = axis,
              .split = std::move(split_pt),
              .left = std::move(stack[stack.size() - 2]),
              .right = std::move(stack[stack.size() - 1]),
            });

          stack.pop_back();
          stack.pop_back();
          stack.push_back(idx);

        } else {
          LOG(FATAL) << "Bad line in cube file: " << raw_line;
        }
      });

  CHECK(stack.size() == 1) << "Expected a single root node to "
    "result.";
  root = std::move(stack[0]);
  nodes = std::move(new_nodes);
  stack.clear();
}


void Hypercube::ToDisk(std::string_view filename) {
  MutexLock ml(&mu);
  FILE *f = fopen(std::string(filename).c_str(), "wb");
  CHECK(f != nullptr);

  using StackElt = std::variant<std::string, int64_t>;

  std::vector<StackElt> stack = {StackElt(0)};

  while (!stack.empty()) {
    StackElt &elt = stack.back();
    if (std::string *s = std::get_if<std::string>(&elt)) {
      fwrite(s->data(), 1, s->size(), f);
      stack.pop_back();

    } else {
      const int64_t idx = std::get<int64_t>(elt);
      stack.pop_back();
      const Hypercube::Node &node = nodes[idx];

      if (const Leaf *leaf = std::get_if<Leaf>(&node)) {
        if (leaf->completed) {
          std::string line =
            std::format("L {} {}\n",
                        leaf->completed,
                        SerializeRejection(leaf->rejection));
          fwrite(line.data(), 1, line.size(), f);
        } else {
          fprintf(f, "E\n");
        }
      } else {
        const Internal *split = std::get_if<Internal>(&node);
        CHECK(split != nullptr) << "Must be leaf or split.";

        std::string line = std::format("S {} {}\n",
                                       split->axis,
                                       split->split.ToString());
        stack.emplace_back(line);
        stack.emplace_back(split->right);
        stack.emplace_back(split->left);
      }
    }
  }

  fclose(f);
}

std::vector<std::pair<Hypercube::Volume, int64_t>>
Hypercube::GetLeaves(double *volume_outscope, double *volume_proved) {
  MutexLock ml(&mu);
  *volume_outscope = 0.0;
  *volume_proved = 0.0;

  std::vector<std::pair<Volume, int64_t>> leaves;

  std::vector<std::pair<Volume, int64_t>> stack = {
    {bounds, root}
  };

  while (!stack.empty()) {
    Volume volume;
    int64_t node_idx = 0;
    std::tie(volume, node_idx) = std::move(stack.back());
    stack.pop_back();

    CHECK(node_idx >= 0 && node_idx < nodes.size());
    const Node &node = nodes[node_idx];

    if (const Leaf *leaf = std::get_if<Leaf>(&node)) {
      if (leaf->completed == 0) {
        leaves.emplace_back(std::move(volume), node_idx);
      } else {
        const double dvol = Hypervolume(volume).ToDouble();
        switch (leaf->rejection.reason) {
        case OUTSIDE_OUTER_PATCH:
        case OUTSIDE_INNER_PATCH:
        case OUTSIDE_OUTER_PATCH_BALL:
        case OUTSIDE_INNER_PATCH_BALL:
          *volume_outscope += dvol;
          break;
        default:
          *volume_proved += dvol;
        }
      }
    } else {
      const Internal *split = std::get_if<Internal>(&node);
      CHECK(split != nullptr) << "Must be leaf or split.";

      std::pair<Volume, Volume> vols =
        SplitVolume(volume, split->axis, split->split);

      stack.emplace_back(std::move(vols.first), split->left);
      stack.emplace_back(std::move(vols.second), split->right);
    }
  }

  return leaves;
}


std::pair<Hypercube::Volume, Hypercube::Volume>
Hypercube::SplitVolume(const Volume &volume, int axis, const BigRat &split) {
  CHECK(axis >= 0 && axis < NUM_DIMENSIONS);
  if (SELF_CHECK) {
    CHECK(split > volume[axis].LB());
    CHECK(split < volume[axis].UB());
  }
  Volume left = volume;
  Volume right = volume;
  left[axis] = Bigival(left[axis].LB(), split,
                       left[axis].IncludesLB(), false);
  right[axis] = Bigival(split, right[axis].UB(),
                        true, right[axis].IncludesUB());
  return std::make_pair(left, right);
}

Hypercube::Volume Hypercube::MakeStandardBounds() {
  // We don't want every volume's endpoints to involve some
  // subdivision of an extremely accurate pi, and we don't need them
  // to; we just need the starting interval to *cover* [0, π] (or
  // 2π). So we use a simple rational upper bound to π.
  // Slightly larger than π. Accurate to 16 digits.
  // pi is:        3.14159265358979323...
  // here we have: 3.14159265358979340...
  BigRat big_pi(165707065, 52746197);

  Volume bounds;
  bounds.resize(7);
  bounds[OUTER_AZIMUTH] = Bigival(BigRat(0), big_pi * 2, true, true);
  bounds[OUTER_ANGLE] = Bigival(BigRat(0), big_pi, true, true);
  bounds[INNER_AZIMUTH] = Bigival(BigRat(0), big_pi * 2, true, true);
  bounds[INNER_ANGLE] = Bigival(BigRat(0), big_pi, true, true);
  bounds[INNER_ROT] = Bigival(BigRat(0), big_pi * 2, true, true);
  bounds[INNER_X] = Bigival(BigRat(-4), BigRat(4), true, true);
  bounds[INNER_Y] = Bigival(BigRat(-4), BigRat(4), true, true);
  return bounds;
}
