
#ifndef _RUPERTS_HYPERCUBE_H
#define _RUPERTS_HYPERCUBE_H

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "bignum/big-interval.h"
#include "bignum/big.h"
#include "threadutil.h"

inline constexpr int OUTER_AZIMUTH = 0;
inline constexpr int OUTER_ANGLE = 1;
inline constexpr int INNER_AZIMUTH = 2;
inline constexpr int INNER_ANGLE = 3;
inline constexpr int INNER_ROT = 4;
inline constexpr int INNER_X = 5;
inline constexpr int INNER_Y = 6;

inline constexpr int NUM_DIMENSIONS = 7;

const char *ParameterName(int param);

// Rejection means we've proved some cell is impossible (can't
// contain a solution). This is all the different reasons that
// we use to do that.
enum RejectionReason : uint8_t {
  REJECTION_UNKNOWN = 0,
  OUTSIDE_OUTER_PATCH = 1,
  OUTSIDE_INNER_PATCH = 2,
  OUTSIDE_OUTER_PATCH_BALL = 3,
  OUTSIDE_INNER_PATCH_BALL = 4,
  CLOSE_TO_DIAGONAL = 13,

  // POINT_OUTSIDE1...3 are all deprecated and shouldn't appear.
  POINT_OUTSIDE1 = 5,
  POINT_OUTSIDE2 = 6,
  POINT_OUTSIDE3 = 7,
  POINT_OUTSIDE4 = 8,
  POINT_OUTSIDE5 = 9,
  POINT_OUTSIDE6 = 12,
  POLY_AREA = 10,
  DIAMETER = 11,
};
inline constexpr int NUM_REJECTION_REASONS = 14;

// Hypercube using big rationals. (It's actually a hyperrectangle
// because the sides are not all the same length...)
struct Hypercube {
  Hypercube();

  // When the rejection reason is PT4, PT5, PT6, then we found a point
  // that is definitely on the wrong side of some edge.
  struct Pt4Data {
    // This is the index of that edge (start endpoint) and point inside
    // the outer and inner hulls, respectively. (NOT a vertex index.)
    int8_t edge = -1;
    int8_t point = -1;
  };

  // Also for Pt6. Maybe should merge these when pt6 is not
  // experimental.
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

  // Represents a (hyper)rectangular volume within the search space.
  using Volume = std::vector<Bigival>;

  static std::string StandardFilename(
      uint64_t outer_code, uint64_t inner_code);

  // Replaces the entire hypercube.
  void FromDisk(std::string_view filename);
  void FromString(std::string_view contents);

  // Test if the contents represents a completed hypercube (no empty
  // leaves).
  static bool IsComplete(std::string_view contents);

  bool Empty();

  void ToDisk(std::string_view filename);

  struct Internal;
  struct Leaf;
  using Node = std::variant<Internal, Leaf>;

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
  struct Internal {
    // Which axis?
    int axis = 0;
    // left side is <, right side is >=.
    BigRat split;
    // Indices into the nodes vector.
    int64_t left, right;
  };

  // Compute the "left" and "right" volumes that result from splitting
  // the parameter at the split point.
  static std::pair<Volume, Volume>
  SplitVolume(const Volume &volume, int axis, const BigRat &split);

  // Though we store the state as a tree, we mostly work with
  // a queue containing all of the unexplored leaves. Extract
  // those as indices into the nodes vector.
  std::vector<std::pair<Volume, int64_t>>
  GetLeaves(double *volume_outscope, double *volume_proved);

  // The n-dimensional hypervolume of the cell.
  static BigRat Hypervolume(const Volume &vol);

  // Produces 7-line string with ANSI color.
  static std::string VolumeString(const Volume &volume);

  static Volume MakeStandardBounds();

  const Volume &Bounds() const { return bounds; }

  // Copies.
  Node GetNode(int64_t idx) {
    MutexLock ml(&mu);
    CHECK(idx >= 0 && idx < nodes.size());
    return nodes[idx];
  }

  void SetNode(int64_t idx, Node node) {
    MutexLock ml(&mu);
    CHECK(idx >= 0 && idx < nodes.size());
    nodes[idx] = std::move(node);
  }

  int64_t AddNode(Node node) {
    MutexLock ml(&mu);
    const int64_t idx = nodes.size();
    nodes.push_back(std::move(node));
    return idx;
  }

 private:
  // Full bounds of the search space.
  Volume bounds;

  // The nodes vector is protected by the mutex.
  std::mutex mu;
  int64_t root = 0;
  // A flat representation.
  std::vector<Node> nodes;
};

#endif
