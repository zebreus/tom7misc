
#ifndef _RUPERTS_HYPERCUBE_H
#define _RUPERTS_HYPERCUBE_H

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "big-interval.h"
#include "bignum/big.h"

inline constexpr int OUTER_AZIMUTH = 0;
inline constexpr int OUTER_ANGLE = 1;
inline constexpr int INNER_AZIMUTH = 2;
inline constexpr int INNER_ANGLE = 3;
inline constexpr int INNER_ROT = 4;
inline constexpr int INNER_X = 5;
inline constexpr int INNER_Y = 6;

inline constexpr int NUM_DIMENSIONS = 7;

const char *ParameterName(int param);

enum RejectionReason : uint8_t {
  REJECTION_UNKNOWN = 0,
  OUTSIDE_OUTER_PATCH = 1,
  OUTSIDE_INNER_PATCH = 2,
  OUTSIDE_OUTER_PATCH_BALL = 3,
  OUTSIDE_INNER_PATCH_BALL = 4,
  POINT_OUTSIDE1 = 5,
  POINT_OUTSIDE2 = 6,
  POINT_OUTSIDE3 = 7,
  POINT_OUTSIDE4 = 8,
  POINT_OUTSIDE5 = 9,
  POLY_AREA = 10,
};
inline constexpr int NUM_REJECTION_REASONS = 12;

struct Pt4Data {
  // When the rejection reason is PT4 or PT5, then we found a point
  // that is definitely on the wrong side of some edge.
  // This is the index of that edge (start endpoint) and point inside
  // the outer and inner hulls, respectively. (NOT a vertex index.)
  int8_t edge = -1;
  int8_t point = -1;
};

struct Pt5Data {
  int8_t edge = -1;
  int8_t point = -1;
  // For a pt5 rejection, the bias we used for the disc.
  BigRat bias;
};

struct Rejection {
  RejectionReason reason = REJECTION_UNKNOWN;
  std::variant<std::monostate, Pt4Data, Pt5Data> data;
};

std::string SerializeRejection(const Rejection &rej);

// Must be a proper rejection (not UNKNOWN).
// Doesn't check the case that point/edge indices are too large.
std::optional<Rejection> ParseRejection(std::string_view s);

// Represents a (hyper)rectangular volume within the search space.
using Volume = std::vector<Bigival>;

// The n-dimensional hypervolume of the cell.
inline BigRat Hypervolume(const Volume &vol) {
  BigRat product(1);
  for (int d = 0; d < NUM_DIMENSIONS; d++) {
    product *= vol[d].Width();
  }
  return product;
}

// Hypercube using big rationals. (It's actually a hyperrectangle
// because the sides are not all the same length...)
struct Hypercube {

  Hypercube(const Volume &bounds) :
    bounds(std::move(bounds)) {
    root.reset(new Node(Leaf{.completed = 0}));
  }

  // Replaces the entire hypercube.
  void FromDisk(const std::string &filename);

  void ToDisk(const std::string &filename) const;

  struct Split;
  struct Leaf;
  using Node = std::variant<Split, Leaf>;

  struct Leaf {
    // If 0, no info yet. Otherwise, the timestamp when it was completed
    // and the RejectionReason.
    // Completed means we've determined that this cell cannot contain
    // a solution.
    int64_t completed = 0;
    Rejection rejection;
  };

  // Internal node, which is a binary split along one of the parameter
  // axes.
  struct Split {
    // Which axis?
    int axis = 0;
    // left side is <, right side is >=.
    BigRat split;
    std::shared_ptr<Node> left, right;
  };

  // Compute the "left" and "right" volumes that result from splitting
  // the parameter at the split point.
  static std::pair<Volume, Volume>
  SplitVolume(const Volume &volume, int axis, const BigRat &split);

  // Though we store the state as a tree, we mostly work with
  // a queue containing all of the unexplored leaves. Extract
  // those.
  std::vector<std::pair<Volume, std::shared_ptr<Hypercube::Node>>>
  GetLeaves(double *volume_outscope, double *volume_proved) const;

  // Produces 7-line string with ANSI color.
  static std::string VolumeString(const Volume &volume);

  // Full bounds of the search space.
  Volume bounds;
  std::shared_ptr<Node> root;
};

#endif
