
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <deque>
#include <format>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "ansi.h"
#include "arcfour.h"
#include "atomic-util.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "big-interval.h"
#include "big-polyhedra.h"
#include "bignum/big-overloads.h"
#include "bignum/big.h"
#include "bounds.h"
#include "image.h"
#include "patches.h"
#include "periodically.h"
#include "polyhedra.h"
#include "randutil.h"
#include "status-bar.h"
#include "threadutil.h"
#include "timer.h"
#include "util.h"
#include "yocto_matht.h"

DECLARE_COUNTERS(counter_processed, counter_completed, counter_split,
                 counter_bad_midpoint, counter_inside,
                 counter_degenerate_disc);

static constexpr bool SELF_CHECK = true;

static constexpr int SCUBE_DIGITS = 24;

using vec2 = yocto::vec<double, 2>;

StatusBar status = StatusBar(14);

// Adaptation of Jason's idea.
// Take a pair of patches, one for outer and one for inner.
// We know what the hulls are; each a function of the view position.

// We'd get the fewest parameters as:
//  - outer view position (azimuth, angle)
//  - inner view position (azimuth, angle)
//  - inner rotation (theta)
//  - inner translation (x, y)

// This is 7 parameters.

// We have a 7-hypercube for those parameters.
// Azimuths are in [0, 2π].
// Polar angles (inclination) are in [0, π].
// Theta is in [0, 2π],
// Translations are like [-diameter, diameter].

// We will operate on intervals. Like, the outer view position's
// angle would be an interval like [1/2, 3/4]. We can do all of
// the normal calculations to compute the locations of the inner
// and outer hull points, but these 2D points will have coordinates
// that are also intervals (so they are rectangles). Then our
// question is: Does one of the inner points definitely fall outside
// the outer hull? If so, we know that that this cell in the 7-hypercube
// cannot contain a solution, so we check it off the list.
//
// If it may contain a solution, we split one of the parameters and
// try again.
//
// Remember that the intervals represent bounds on the true underlying
// values that we are reasoning about. For example we often know that
// a 3d vector is unit length, but the bounds on each component do not
// express this (it is always an axis-aligned bounding box). Nonetheless,
// we can still draw conclusions that rely on properties of the underlying
// values. For example, we can take the cross product of two unit-length,
// orthogonal vectors (even if their bounds would include non-unit vectors
// or non-orthogonal vectors) and know that the result is unit length
// (though its bounds will also include non unit-length vectors).
//
// In this code it's important to be careful about winding order, and
// in particular since we are sometimes using a flipped coordinate
// system for graphics (y down), "clockwise" is ambiguous. This code
// tries to be careful about "screen clockwise" and "cartesian
// clockwise." The hulls tested here are in screen clockwise (which is
// cartesian counter-clockwise) winding order.

inline constexpr int OUTER_AZIMUTH = 0;
inline constexpr int OUTER_ANGLE = 1;
inline constexpr int INNER_AZIMUTH = 2;
inline constexpr int INNER_ANGLE = 3;
inline constexpr int INNER_ROT = 4;
inline constexpr int INNER_X = 5;
inline constexpr int INNER_Y = 6;

inline constexpr int NUM_DIMENSIONS = 7;

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

// A set of those 7 parameters. Value semantics.
struct ParameterSet {
  // Default empty.
  ParameterSet() : bits(0) {}
  static ParameterSet All() {
    return ParameterSet((1 << NUM_DIMENSIONS) - 1);
  }

  bool Empty() const { return !bits; }

  // With a list of dimensions to allow.
  explicit ParameterSet(const std::initializer_list<int> &dimensions) {
    bits = 0;
    for (int x : dimensions) {
      CHECK(x >= 0 && x < NUM_DIMENSIONS);
      bits |= (1 << x);
    }
  }

  bool Contains(int d) const {
    return !!(bits & (1 << d));
  }

  void Add(int d) {
    CHECK(d >= 0 && d < NUM_DIMENSIONS);
    bits |= (1 << d);
  }

  int Size() const {
    return std::popcount<uint32_t>(bits);
  }

  int operator[](int idx) const {
    uint32_t shift = bits;
    int dim = 0;
    for (;;) {
      CHECK(shift != 0);
      if (!(shift & 1)) {
        int skip = std::countr_zero<uint32_t>(shift);
        dim += skip;
        shift >>= skip;
      }

      CHECK(shift & 1);
      if (idx == 0) {
        return dim;
      }
      idx--;
      shift >>= 1;
      dim++;
    }
  }


 private:
  ParameterSet(uint32_t b) : bits(b) {}
  uint32_t bits = 0;
};

// Represents a (hyper)rectangular volume within the search space.
using Volume = std::vector<Bigival>;

// The n-dimensional hypervolume of the cell.
static BigRat Hypervolume(const Volume &vol) {
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

  void FromDisk(const std::string &filename) {
    // The file format is line based. Each line is a node;
    // either:
    // L completed
    // or
    // S axis split
    //
    // The nodes are in post order, so we process them with
    // a stack:

    std::vector<std::shared_ptr<Hypercube::Node>> stack;

    Util::ForEachLine(
        filename,
        [&](const std::string &raw_line) {
          std::string line_string = Util::NormalizeWhitespace(raw_line);
          std::string_view line(line_string);
          if (line.empty()) return;
          CHECK(line.size() > 2);

          char cmd = line[0];
          line.remove_prefix(2);
          if (cmd == 'L') {
            int64_t comp = Util::ParseInt64(Util::Chop(&line), -1);
            CHECK(comp >= 0) << line;
            auto leaf =
              std::make_shared<Hypercube::Node>(Leaf{
                  .completed = comp,
                });
            stack.push_back(std::move(leaf));
          } else if (cmd == 'S') {
            int axis = Util::ParseInt64(Util::Chop(&line), -1);
            CHECK(axis >= 0 && axis < NUM_DIMENSIONS) << line;
            BigRat split_pt(Util::Chop(&line));

            CHECK(stack.size() >= 2) << "Saw split node, so there "
              "should be two children in the stack!";

            auto split =
              std::make_shared<Hypercube::Node>(Split{
                  .axis = axis,
                  .split = std::move(split_pt),
                  .left = std::move(stack[stack.size() - 2]),
                  .right = std::move(stack[stack.size() - 1]),
                });

            stack.pop_back();
            stack.pop_back();
            stack.push_back(std::move(split));

          } else {
            LOG(FATAL) << "Bad line in cube file: " << line;
          }
        });

    CHECK(stack.size() == 1) << "Expected a single root node to "
      "result.";
    root = std::move(stack[0]);
    stack.clear();
  }

  void ToDisk(const std::string &filename) {
    FILE *f = fopen(filename.c_str(), "wb");
    CHECK(f != nullptr);

    using StackElt = std::variant<
      std::string,
      std::shared_ptr<Hypercube::Node>
      >;


    std::vector<StackElt> stack = {StackElt(root)};

    while (!stack.empty()) {
      StackElt &elt = stack.back();
      if (std::string *s = std::get_if<std::string>(&elt)) {
        fprintf(f, "%s", s->c_str());
        stack.pop_back();

      } else {
        auto *p = std::get_if<std::shared_ptr<Hypercube::Node>>(&elt);
        CHECK(p != nullptr);
        std::shared_ptr<Hypercube::Node> node = std::move(*p);
        stack.pop_back();

        CHECK(node.get() != nullptr);

        if (Leaf *leaf = std::get_if<Leaf>(node.get())) {
          std::string line = std::format("L {}\n", leaf->completed);
          fprintf(f, "%s", line.c_str());
        } else {
          Split *split = std::get_if<Split>(node.get());
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


  struct Split;
  struct Leaf;
  using Node = std::variant<Split, Leaf>;

  struct Leaf {
    // If 0, no info yet. Otherwise, the timestamp when it was completed.
    // Completed means we've determined that this cell cannot contain
    // a solution.
    int64_t completed = 0;
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
  SplitVolume(const Volume &volume, int axis, const BigRat &split) {
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

  // Though we store the state as a tree, we mostly work with
  // a queue containing all of the unexplored leaves. Extract
  // those.
  std::vector<std::pair<Volume, std::shared_ptr<Hypercube::Node>>> GetLeaves(
      double *volume_done) {
    std::vector<std::pair<Volume, std::shared_ptr<Hypercube::Node>>> leaves;

    std::vector<std::pair<Volume, std::shared_ptr<Hypercube::Node>>> stack = {
      {bounds, root}
    };

    while (!stack.empty()) {
      Volume volume;
      std::shared_ptr<Hypercube::Node> node;
      std::tie(volume, node) = std::move(stack.back());
      stack.pop_back();

      CHECK(node.get() != nullptr);

      if (Leaf *leaf = std::get_if<Leaf>(node.get())) {
        if (leaf->completed == 0) {
          leaves.emplace_back(std::move(volume), node);
        } else {
          *volume_done += Hypervolume(volume).ToDouble();
        }
      } else {
        Split *split = std::get_if<Split>(node.get());
        CHECK(split != nullptr) << "Must be leaf or split.";

        std::pair<Volume, Volume> vols =
          SplitVolume(volume, split->axis, split->split);

        stack.emplace_back(std::move(vols.first), split->left);
        stack.emplace_back(std::move(vols.second), split->right);
      }
    }

    return leaves;
  }

  // Full bounds of the search space.
  Volume bounds;
  std::shared_ptr<Node> root;
};

// Vec3 where each component is an interval.
struct Vec3ival {
  Bigival x, y, z;
  Vec3ival(Bigival xx, Bigival yy, Bigival zz) :
    x(std::move(xx)), y(std::move(yy)), z(std::move(zz)) {}
  Vec3ival() : x(0), y(0), z(0) {}

  std::string ToString() const {
    return std::format("x: {}, y: {}, z: {}",
                       x.ToString(), y.ToString(), z.ToString());
  }
};

Bigival Dot(const Vec3ival &a, const BigVec3 &b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

// PERF: I think we only need Rot3 for this task; the origin is always
// zero. Can save ourselves some pointless addition and copying.
struct Frame3ival {
  Vec3ival x = {BigRat(1), BigRat(0), BigRat(0)};
  Vec3ival y = {BigRat(0), BigRat(1), BigRat(0)};
  Vec3ival z = {BigRat(0), BigRat(0), BigRat(1)};
  Vec3ival o = {BigRat(0), BigRat(0), BigRat(0)};

  Vec3ival& operator[](int i) {
    switch (i) {
    case 0: return x;
    case 1: return y;
    case 2: return z;
    case 3: return o;
    default:
      LOG(FATAL) << "Bad";
      return x;
    }
  }

  const Vec3ival &operator[](int i) const {
    switch (i) {
    case 0: return x;
    case 1: return y;
    case 2: return z;
    case 3: return o;
    default:
      LOG(FATAL) << "Bad";
      return x;
    }
  }
};

// Vec2 where each component is an interval.
// This is an axis-aligned bounding box (AABB).
struct Vec2ival {
  Bigival x, y;
  Vec2ival(Bigival xx, Bigival yy) :
    x(std::move(xx)), y(std::move(yy)) {}
  Vec2ival() : x(0), y(0) {}

  // The exact area of the AABB.
  BigRat Area() const {
    return x.Width() * y.Width();
  }

  std::string ToString() const {
    return std::format("(⏹ x: {}, y: {})",
                       x.ToString(), y.ToString());
  }

  bool Contains(const BigVec2 &v) const {
    return x.Contains(v.x) && y.Contains(v.y);
  }
};

// Small Fixed-size matrices stored in column major format.
struct Mat3ival {
  // left column
  Vec3ival x = {BigRat(1), BigRat(0), BigRat(0)};
  // middle column
  Vec3ival y = {BigRat(0), BigRat(1), BigRat(0)};
  // right column
  Vec3ival z = {BigRat(0), BigRat(0), BigRat(1)};

  Vec3ival &operator[](int i) {
    switch (i) {
    case 0: return x;
    case 1: return y;
    case 2: return z;
    default:
      LOG(FATAL) << "Index out of bounds.";
    }
  }

  const Vec3ival &operator[](int i) const {
    switch (i) {
    case 0: return x;
    case 1: return y;
    case 2: return z;
    default:
      LOG(FATAL) << "Index out of bounds.";
    }
  }
};

// A bounding sphere (not ball), represented as an center (usually
// very accurate) and an interval on its radius (could be wide). To be
// clear, this is the shell; it does not generally contain the origin
// for example.
#if 0
struct Sphereival {
  Vec3ival center;
  Bigival radius;
  Sphereival(Vec3ival c, Bigival r) : center(std::move(c)),
                                      radius(std::move(r)) {
    CHECK(!r.MightBeNegative()) << r.ToString();
  }
};
#endif

// Bounding ball.
struct Ballival {
  // An exact center.
  BigVec3 center;
  // Upper bound on the *squared* radius.
  BigRat radius_sq;
  // TODO: We could probably have a constructor that took
  // Vec3ival center and/or Bigival radius, and computed a
  // bounding sphere from those. But we aren't using that
  // today.
  Ballival(BigVec3 c, BigRat rsq) : center(std::move(c)),
                                    radius_sq(std::move(rsq)) {
    CHECK(BigRat::Sign(radius_sq) != -1) << radius_sq.ToString();
  }

  // Upper bound on the radius.
  BigRat Radius(const BigInt &inv_epsilon) const {
    return BigRat::SqrtBounds(radius_sq, inv_epsilon).second;
  }
};

// Compute a bounding ball for the patch on the unit sphere
// given by the azimuth and angle (this is the view position).
// The patch must be smaller than a hemisphere or you will
// get a degenerate (but correct) result.
static Ballival SphericalPatchBall(const Bigival &azimuth,
                                   const Bigival &angle,
                                   const BigInt &inv_epsilon) {

  // We need the chosen center to be in the convex hull of the patch.
  // This is easy for small patches, but like elsewhere if the patch
  // is a whole hemisphere we'd need to start checking other points.
  // Just return a conservative but degenerate ball (full unit ball)
  // if the intervals are too wide.
  if (azimuth.Width() > BigRat(3) || angle.Width() > BigRat(3)) {
    return Ballival(BigVec3(BigRat(0), BigRat(0), BigRat(0)),
                    BigRat(1));
  }

  // Compute a good center.
  BigRat mid_azimuth = azimuth.Midpoint();
  BigRat mid_angle = angle.Midpoint();

  Bigival mid_sinz = Bigival::Sin(mid_azimuth, inv_epsilon);
  Bigival mid_cosz = Bigival::Cos(mid_azimuth, inv_epsilon);
  Bigival mid_sina = Bigival::Sin(mid_angle, inv_epsilon);
  Bigival mid_cosa = Bigival::Cos(mid_angle, inv_epsilon);

  // We have an approximate center (AABB) because of the
  // transcendental functions. We have our choice of center
  // for the bounding ball that we create, though! So
  // we just use the midpoint of this tiny interval.
  // Uncertainty essentially gets transferred into the
  // radius.
  BigVec3 center = BigVec3(
      (mid_sina * mid_cosz).Midpoint(),
      (mid_sina * mid_sinz).Midpoint(),
      (mid_cosa).Midpoint());

  // Find a squared radius that will include all the corners.
  BigRat max_sqdist(0);

  // The corners of the patch are the furthest away from the
  // chosen center. The furthest of these will determine the
  // radius.
  for (const BigRat &az_c : {azimuth.LB(), azimuth.UB()}) {
    for (const BigRat &an_c : {angle.LB(), angle.UB()}) {
      // The location of the corner.
      Bigival c_sinz = Bigival::Sin(az_c, inv_epsilon);
      Bigival c_cosz = Bigival::Cos(az_c, inv_epsilon);
      Bigival c_sina = Bigival::Sin(an_c, inv_epsilon);
      Bigival c_cosa = Bigival::Cos(an_c, inv_epsilon);
      Vec3ival corner(c_sina * c_cosz, c_sina * c_sinz, c_cosa);

      // Distance to the actual center.
      // PERF: We could do a bit less computation here because we are
      // only using the upper bound.
      Bigival dx = corner.x - center.x;
      Bigival dy = corner.y - center.y;
      Bigival dz = corner.z - center.z;
      Bigival sqdist = dx.Squared() + dy.Squared() + dz.Squared();

      max_sqdist = BigRat::Max(max_sqdist, sqdist.UB());
    }
  }

  return Ballival(std::move(center), std::move(max_sqdist));
}

// Bounding disc.
struct Discival {
  // An exact center.
  BigVec2 center;
  // Upper bound on the squared radius.
  BigRat radius_sq;
  Discival(BigVec2 c, BigRat r_sq) : center(std::move(c)),
                                     radius_sq(std::move(r_sq)) {
    CHECK(BigRat::Sign(radius_sq) != -1) << radius_sq.ToString();
  }

  Discival(const Vec2ival &v) : center(v.x.Midpoint(),
                                       v.y.Midpoint()) {
    // All corners are the same distance from the exact
    // center.
    BigRat dx = v.x.Width() / 2;
    BigRat dy = v.y.Width() / 2;
    radius_sq = dx * dx + dy * dy;

    if (SELF_CHECK) {
      CHECK(v.Contains(center));
    }

    /*
    status.Print("Input AABB: {:.5f},{:.5f} to {:.5f},{:.5f}. "
                 "Output disc: {} rad: {}",
                 v.x.LB().ToDouble(),
                 v.y.LB().ToDouble(),
                 v.x.UB().ToDouble(),
                 v.y.UB().ToDouble(),
                 ToString(),
                 std::sqrt(radius_sq.ToDouble())
                 );
    */

    /*
    status.Print("Input AABB: {} " AGREY("(Width: {} Height: {}  ≅  {}x{})")
                 ". Output Disc: {}",
                 v.ToString(),
                 v.x.Width().ToString(),
                 v.y.Width().ToString(),
                 v.x.Width().ToDouble(),
                 v.y.Width().ToDouble(),
                 ToString());
    */
  }

  // Upper bound on the radius.
  BigRat Radius(const BigInt &inv_epsilon) const {
    return BigRat::SqrtBounds(radius_sq, inv_epsilon).second;
  }

  std::string ToString() const {
    return std::format("(⏺ c: {}, r²: {} " ABLUE("≅ {}") ")",
                       VecString(center), radius_sq.ToString(),
                       radius_sq.ToDouble()
                       );
  }
};

[[maybe_unused]]
static Bigival Cross(const Vec2ival &a, const Vec2ival &b) {
  return a.x * b.y - a.y * b.x;
}

static Vec3ival Cross(const Vec3ival &a, const Vec3ival &b) {
  return Vec3ival(
    a.y * b.z - a.z * b.y,
    a.z * b.x - a.x * b.z,
    a.x * b.y - a.y * b.x);
}

static Bigival Length(const Vec3ival &v, const BigInt &inv_epsilon) {
  return Bigival::Sqrt(v.x.Squared() + v.y.Squared() + v.z.Squared(),
                       inv_epsilon);
}

[[maybe_unused]]
static Bigival Length(const Vec2ival &v, const BigInt &inv_epsilon) {
  return Bigival::Sqrt(v.x.Squared() + v.y.Squared(),
                       inv_epsilon);
}

[[maybe_unused]]
static Vec3ival Normalize(const Vec3ival &v, const BigInt &inv_epsilon) {
  Bigival len = Length(v, inv_epsilon);
  CHECK(!len.ContainsZero()) << "Can't normalize if the length might be "
    "zero. v was: " << v.ToString();
  return Vec3ival(
      v.x / len,
      v.y / len,
      v.z / len);
}

inline static Vec3ival operator +(const Vec3ival &a,
                                  const Vec3ival &b) {
  return Vec3ival(a.x + b.x, a.y + b.y, a.z + b.z);
}

[[maybe_unused]]
inline static Vec3ival operator *(const Vec3ival &a,
                                  const Vec3ival &b) {
  return Vec3ival(a.x * b.x, a.y * b.y, a.z * b.z);
}

[[maybe_unused]]
inline static Vec3ival operator *(const Vec3ival &a,
                                  const BigVec3 &b) {
  return Vec3ival(a.x * b.x, a.y * b.y, a.z * b.z);
}

inline static Vec3ival operator *(const Vec3ival &a,
                                  const BigRat &s) {
  return Vec3ival(a.x * s, a.y * s, a.z * s);
}

[[maybe_unused]]
inline static Vec2ival operator -(const Vec2ival &a,
                                  const BigVec2 &b) {
  return Vec2ival(a.x - b.x, a.y - b.y);
}


// This can certainly work on Vec3ival, but our input data at this point
// is a single point and this is in inner loops.
static Vec2ival TransformPointTo2D(const Frame3ival &frame,
                                   const BigVec3 &v) {
  // PERF don't even compute z component!
  Vec3ival v3 = frame.x * v.x + frame.y * v.y + frame.z * v.z + frame.o;
  return Vec2ival(std::move(v3.x), std::move(v3.y));
}


// view pos must be unit length, and moreover, the origin can't be
// included (or approached) in the interval. The view positions are
// naturally unit length, but their interval approximations (AABBs)
// can easily include the origin if the angles subtended are more than
// a hemisphere, for example. So below we eagerly split if the angle
// is not already small.
//
// This also requires that the z-axis is not included in the view interval.
Frame3ival FrameFromViewPos(const Vec3ival &view, const BigInt &inv_epsilon) {
  const Vec3ival &frame_z = view;

  // Vec3ival up_z = {BigRat(0), BigRat(0), BigRat(1)};
  // We want frame_x = Normalize(Cross(up_z, frame_z)).
  // cross(a, b) is a2b3 - a3b2,  a3b1 - a1b3,  a1b2 - a2b1
  // cross(up_z, b) is 0*b3 - 1*b2,  1*b1 - 0*b3,  0*b2 - 0*b1
  //                so -b.y, b.x, 0
  // So we have c = (-frame_z.y, frame_z.x, 0).
  // |c| is sqrt(frame_z.y² + frame_z.x²).
  // But since frame_z is unit length, we have
  //   1 = frame_z.x² + frame_z.y² + frame_z.z²
  // And so |c| = sqrt(1 - frame_z.z²).
  // (The purpose of rearranging this is to avoid dependency
  // problems between the vector components.)

  Bigival len = Bigival::Sqrt(1 - frame_z.z.Squared(), inv_epsilon);
  if (SELF_CHECK) {
    CHECK(!len.ContainsOrApproachesZero()) << len.ToString();
  }
  Vec3ival frame_x = Vec3ival(-frame_z.y / len, frame_z.x / len, 0);
  // Since frame_z and frame_x are orthogonal unit vectors, their
  // cross product is a unit vector.
  Vec3ival frame_y = Cross(frame_z, frame_x);

  // Following patches.cc, the convention was opposite of what
  // ViewPosFromNonUnitQuat did, so invert this frame. Since
  // the origin is zero, the inverse is just the transpose.
  Vec3ival xt = Vec3ival(std::move(frame_x.x),
                         std::move(frame_y.x), frame_z.x);
  Vec3ival yt = Vec3ival(std::move(frame_x.y),
                         std::move(frame_y.y), frame_z.y);
  Vec3ival zt = Vec3ival(std::move(frame_x.z),
                         std::move(frame_y.z), frame_z.z);

  return Frame3ival{
    .x = std::move(xt),
    .y = std::move(yt),
    .z = std::move(zt),
    .o = Vec3ival{0, 0, 0},
  };
}

static std::string FormatNum(const BigInt &b) {
  if (BigInt::Abs(b) >= 1'000'000'000) {
    return std::format("{} digits", b.ToString().size());
  }

  std::optional<int64_t> i = b.ToInt();
  if (!i.has_value()) return ARED("???");
  int64_t n = i.value();

  if (n > 1'000'000) {
    double m = n / 1'000'000.0;
    if (m >= 1'000'000.0) {
      return std::format("{:.1f}T", m / 1'000'000.0);
    } else if (m >= 1000.0) {
      return std::format("{:.1f}B", m / 1000.0);
    } else if (m >= 100.0) {
      return std::format("{}M", (int)std::round(m));
    } else if (m > 10.0) {
      return std::format("{:.1f}M", m);
    } else {
      // TODO: Integer division. color decimal place and suffix.
      return std::format("{:.2f}M", m);
    }

  } else {
    return Util::UnsignedWithCommas(n);
  }
}


std::string VolumeString(const Volume &volume, bool multiline = false) {
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
      return std::format(AGREY("[") "{:.8g}" AGREY(",") " {:.8g}" AGREY("]")
                         "   {}",
                         lb, ub, wstr);
    };

  if (multiline) {
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
  } else {
    return std::format("O φ:{} θ:{}, I φ:{} θ:{} α:{}, x:{}, y:{}",
                       DimString(volume[OUTER_AZIMUTH]),
                       DimString(volume[OUTER_ANGLE]),
                       DimString(volume[INNER_AZIMUTH]),
                       DimString(volume[INNER_ANGLE]),
                       DimString(volume[INNER_ROT]),
                       DimString(volume[INNER_X]),
                       DimString(volume[INNER_Y]));
  }
}

// Note: Here we have the dependency problem. This one might be
// solvable with somewhat straightforward analysis. I think what we
// want to do is think of this as rotating an axis-aligned rectangle
// (the 2D bounds) and then getting the axis-aligned rectangle that
// contains that.
// This is also one of the very last things we do, so it's plausible
// that we could just do some geometric reasoning about the actual
// rotated rectangle at this point.
static Vec2ival Rotate2D(const Vec2ival &v, const Bigival &angle,
                         const BigInt &inv_epsilon) {
  Bigival sin_a = angle.Sin(inv_epsilon);
  Bigival cos_a = angle.Cos(inv_epsilon);

  return Vec2ival(v.x * cos_a - v.y * sin_a,
                  v.x * sin_a + v.y * cos_a);
}


// Given an AABB, find the squared magnitude of the point within it
// that is furthest from the origin. This is the same as the maximum
// squared length of the vector described by the interval.
[[maybe_unused]]
static BigRat MaxSqLength(const Vec2ival &v) {
  auto MaxAbs = [](const Bigival &i) {
    return BigRat::Max(BigRat::Abs(i.LB()), BigRat::Abs(i.UB()));
  };
  BigRat x = MaxAbs(v.x);
  BigRat y = MaxAbs(v.y);
  return x * x + y * y;
}

// Rotates a disc by an angle interval, producing a new, larger disc that
// bounds the entire swept shape. There are many choices of bounding disc;
// this code uses one that is biased away from the origin, because in the
// success case where we are able to prove the point is on the outside of
// the edge, we want to minimize the error on the *inside* and don't really
// care about the outside. (If the disc gets *too* big then it might
// intersect the edge somewhere else, so we don't go crazy here.)
// See rotate-disc-inner-bias.png.
//
// To simplify the math, the angle interval's width must be reasonable (less
// than 3) or the resulting disc will be very conservative.
static Discival RotateDiscInnerBias(
    const Discival &disc,
    const Bigival &angle,
    // A factor > 1 pushes the center away from the origin
    // to create a tighter inner bound. 1.0 is unbiased.
    const BigRat &bias,
    const BigInt &inv_epsilon_orig) {

  // Use a much smaller epsilon to assess the hypothesis.
  // BigInt inv_epsilon = inv_epsilon_orig * inv_epsilon_orig;
  const BigInt &inv_epsilon = inv_epsilon_orig;

  if (angle.Width() > BigRat(3)) {
    // We can't get a good disc with this method. Just return
    // something correct. A really simple choice is just a
    // disc centered at the origin, whose radius is though
    // we sweep the input disc over the entire circle.
    counter_degenerate_disc++;
    BigRat center_dist = BigRat::SqrtBounds(dot(disc.center, disc.center),
                                            inv_epsilon).second;
    BigRat radius = BigRat::SqrtBounds(disc.radius_sq, inv_epsilon).second;

    BigRat bounding_radius = center_dist + radius;
    return Discival(BigVec2(BigRat(0), BigRat(0)),
                    bounding_radius * bounding_radius);
  }


  // The center of the disc will be on the same vector as the center
  // of the arc, just further out (according to the bias parameter).
  // Using the exact center would be nice here (the distance to the
  // arc endpoints is equal on the perpendicular bisector) but we
  // can't compute it precisely since we have the transcendentals.
  // We'll just commit to a point decently close to the geometric center,
  // and then compute a radius that definitely includes the sweep
  // for the chosen point.



  BigRat mid_angle = angle.Midpoint();
  Bigival sin_ma = Bigival::Sin(mid_angle, inv_epsilon);
  Bigival cos_ma = Bigival::Cos(mid_angle, inv_epsilon);
  Vec2ival arc_center_ival(
      disc.center.x * cos_ma - disc.center.y * sin_ma,
      disc.center.x * sin_ma + disc.center.y * cos_ma);
  BigVec2 arc_center = {arc_center_ival.x.Midpoint(),
                        arc_center_ival.y.Midpoint()};

  // Push this center away from the origin by the bias.
  BigVec2 bounding_center = arc_center * bias;

  // Radius for the bounding disc.
  // The radius must be large enough to contain the furthest point on
  // the swept shape, which will be on the circumference of one of the
  // endpoint discs. We use the triangle inequality:

  // Upper bound on the input disc's actual radius.
  BigRat in_r = BigRat::SqrtBounds(disc.radius_sq, inv_epsilon).second;

  // The AABBs for the disc's center rotated to the angle's endpoints.
  // PERF: These should be very tight intervals, but we could probably
  // do better here with a routine that computes a disc for a rotated
  // point.
  auto RotatePt = [&](const BigRat &angle, const BigVec2 &p) {
      Bigival sina = Bigival::Sin(angle, inv_epsilon);
      Bigival cosa = Bigival::Cos(angle, inv_epsilon);
      return Vec2ival(p.x * cosa - p.y * sina,
                      p.x * sina + p.y * cosa);
    };

  // Very tight AABBs bounding the centers of the rotated disc at
  // the angle lower bound and upper bound.
  Vec2ival center_lb = RotatePt(angle.LB(), disc.center);
  Vec2ival center_ub = RotatePt(angle.UB(), disc.center);

  // Now find the maximum squared distance to the two rotated endpoints.
  // These should be almost the same except for the small amount of error
  // from estimating Sin and Cos. But we need to get a result that is
  // correct, so we need to incorporate the error in the radius.
  // This requires picking the corner of the AABB that is furthest.

  BigRat max_arc_dist_sq(0);
  auto TryCorners = [&](const Vec2ival &c) {
      for (const BigRat &x : {c.x.LB(), c.x.UB()}) {
        BigRat dx = x - bounding_center.x;
        BigRat dxx = dx * dx;
        for (const BigRat &y : {c.y.LB(), c.y.UB()}) {
          BigRat dy = y - bounding_center.y;
          BigRat dyy = dy * dy;

          BigRat dist_sq = dxx + dyy;
          if (dist_sq > max_arc_dist_sq)
            max_arc_dist_sq = std::move(dist_sq);
        }
      }
    };

  TryCorners(center_lb);
  TryCorners(center_ub);

  /*
  Vec2ival delta_lb = center_lb - bounding_center;
  Vec2ival delta_ub = center_ub - bounding_center;
  BigRat max_arc_dist_sq = BigRat::Max(MaxSqLength(delta_lb),
                                       MaxSqLength(delta_ub));
  */

  BigRat center_r = BigRat::SqrtBounds(max_arc_dist_sq, inv_epsilon).second;
  // The radius is bounded by the sum of the distance to the arc and the
  // distance from the arc to the arc's circumference (input disc's radius),
  // because of the triangle inequality.
  BigRat bounding_radius = center_r + in_r;

  return Discival(std::move(bounding_center),
                  bounding_radius * bounding_radius);
}

// Check if a disc is guaranteed to be strictly on the "outside" of an
// edge. "Outside" means the side of the line that doesn't contain the
// origin.
static bool IsDiscOutsideEdge(const Discival &disc,
                              const Vec2ival &outer_edge,
                              const Bigival &outer_cross_va_vb) {
  // We want to test if for all points p in the disc, the line-side test
  //   L(p) = edge.x * p.y - edge.y * p.x + cross_va_vb
  // is strictly negative.

  // The value of the test at the disc's exact center is:
  Bigival l_at_center =
    outer_edge.x * disc.center.y -
    outer_edge.y * disc.center.x +
    outer_cross_va_vb;

  // If the center might be on the inside, then we defintiely aren't
  // going to prove the whole thing is outside!
  if (l_at_center.MightBePositive()) {
    return false;
  }

  // The center is outside. So the whole disc is outside if the distance
  // from the center to the line is more than the disc's radius.
  //   distance² > radius²
  //   L(center)² / |edge|² > R²
  //   L(center)² > R² * |edge|²

  // To prove this we want to compute the smallest L(center)² and
  // the largest R² * |edge|².
  // l_at_center is not positive, so the smallest value of
  // L(center)² is l_at_center.UB()² (value closer to zero).
  // Just compute that rather than the whole interval.
  BigRat min_l_at_center_sq = l_at_center.UB() * l_at_center.UB();
  Bigival edge_len_sq = outer_edge.x.Squared() + outer_edge.y.Squared();
  Bigival margin_sq = edge_len_sq * disc.radius_sq;

  // Now, check whether the inequality can hold.
  return min_l_at_center_sq > margin_sq.UB();
}

// Bisect an interval. We choose a split point that's close to half
// way, but prefer simple fractions! The endpoints form the basis of
// the intervals that we calculate with, so higher quality fractions
// are much preferable (and we don't really care about where we split
// as long as we are getting logarithmic size reduction).
static BigRat SplitInterval(const Bigival &ival) {
  // return (ival.LB() + ival.UB()) / 2;

  BigRat sum = ival.LB() + ival.UB();

  BigRat mid = sum / 2;

  // We'll require the split point to be in the middle third of
  // the interval; this guarantees we get logarithmic reduction.
  BigRat lthird = (sum + ival.LB()) / 3;
  BigRat uthird = (sum + ival.UB()) / 3;

  // Try simplifying.
  BigInt inv_epsilon = BigInt::Sqrt(mid.Denominator());
  BigRat simple_mid = BigRat::Truncate(mid, inv_epsilon);

  // Truncate doesn't really make any guarantees about the
  // error on the result, so it could be too far from the
  // midpoint (even outside the original bounds!).

  if (simple_mid >= lthird && simple_mid <= uthird) {
    // Found a high quality rational that's close enough to the middle.
    if (SELF_CHECK) {
      CHECK(simple_mid >= ival.LB() && simple_mid <= ival.UB());
    }
    return simple_mid;
  } else {
    // Return the exact midpoint, even if it is low quality.
    counter_bad_midpoint++;
    return mid;
  }
}

// Translates a disc by an interval (tx, ty), producing a new, larger disc
// that bounds the entire resulting shape (a roundrect).
static Discival TranslateDisc(
    const Discival &disc,
    const Bigival &tx,
    const Bigival &ty,
    const BigInt &inv_epsilon) {

  // Exact center for the bounding disc. We have our choice here, but
  // the midpoint is the best option and is easy to compute.
  BigVec2 bound_center(
      (disc.center.x + tx).Midpoint(),
      (disc.center.y + ty).Midpoint());

  // Now compute a radius that's sufficient to contain the entire
  // roundrect. Using the triangle inequality, the max distance to a
  // corner plus the original radius is an upper bound. All the
  // corners are the same distance from the center:
  BigRat half_w = tx.Width() / 2;
  BigRat half_h = ty.Width() / 2;
  BigRat corner_dist =
    BigRat::SqrtBounds(half_w * half_w + half_h * half_h, inv_epsilon).second;

  BigRat original_radius =
    BigRat::SqrtBounds(disc.radius_sq, inv_epsilon).second;

  // And the original radius.
  BigRat bound_radius = corner_dist + original_radius;

  return Discival(std::move(bound_center),
                  bound_radius * bound_radius);
}

struct Hypersolver {
  enum RejectionReason {
    UNKNOWN = 0,
    OUTSIDE_OUTER_PATCH = 1,
    OUTSIDE_INNER_PATCH = 2,
    OUTSIDE_OUTER_PATCH_BALL = 3,
    OUTSIDE_INNER_PATCH_BALL = 4,
    POINT_OUTSIDE1 = 5,
    POINT_OUTSIDE2 = 6,
    POINT_OUTSIDE3 = 7,
    POINT_OUTSIDE4 = 8,
    POINT_OUTSIDE5 = 9,
  };

  std::string_view RejectionReasonString(RejectionReason r) {
    switch (r) {
    default:
    case UNKNOWN:
      return ARED("MISSING?");
    case OUTSIDE_OUTER_PATCH:
      return "OUT_PATCH";
    case OUTSIDE_INNER_PATCH:
      return "IN_PATCH";
    case OUTSIDE_OUTER_PATCH_BALL:
      return "OUT_PATCH_B";
    case OUTSIDE_INNER_PATCH_BALL:
      return "IN_PATCH_B";
    case POINT_OUTSIDE1:
      return "PT1";
    case POINT_OUTSIDE2:
      return "PT2";
    case POINT_OUTSIDE3:
      return "PT3";
    case POINT_OUTSIDE4:
      return "PT4";
    case POINT_OUTSIDE5:
      return "PT5";
    };
  }

  struct Split {
    ParameterSet parameters;
    Split() : parameters(ParameterSet::All()) {}
    explicit Split(ParameterSet params) : parameters(params) {}
    // With a list of dimensions to allow.
    explicit Split(const std::initializer_list<int> &dimensions) :
      parameters(ParameterSet(dimensions)) {
      CHECK(!parameters.Empty());
    }
  };

  struct Impossible {
    RejectionReason reason = UNKNOWN;
    Impossible(RejectionReason r = UNKNOWN) : reason(r) {}
    // For POINT_OUTSIDE, maybe could also record the point and
    // edge?
  };

  // Rotate the point v_in by rot and translate it by tx,ty.
  // Since v_in is an AABB and rot is an interval, the resulting
  // shape here is a rectangle swept along a circular arc. We
  // will call this the "swept shape."
  //
  // Since the precision of the result's representation is important
  // for efficiency in the search, we allow the result to be a
  // union of a set of AABBs. (However, it currently always returns
  // one AABB.)
  std::vector<Vec2ival>
  GetBoundingComplex(const Vec2ival &v_in, const Bigival &angle,
                     const BigInt &inv_epsilon,
                     const Bigival &tx, const Bigival &ty) {

    auto Translate = [&](const Vec2ival &v) {
        return Vec2ival(v.x + tx, v.y + ty);
      };

    Bigival sin_a = angle.Sin(inv_epsilon);
    Bigival cos_a = angle.Cos(inv_epsilon);
    Vec2ival loose_box(Bigival(v_in.x) * cos_a - Bigival(v_in.y) * sin_a,
                       Bigival(v_in.x) * sin_a + Bigival(v_in.y) * cos_a);

    return {Translate(loose_box)};

    // The idea is to compute a bounding volume for the shape swept by
    // rotating the AABB containing v_in by the angle (and then translate
    // it by tx, ty). The volume is represented by a set of AABBs.
    //
    // If the sweep might cross an axis, it gets complicated because
    // this is where sin/cos change direction. As an extreme example,
    // if angle can take on all values [0, 2π] then computing the
    // endpoints of the swept shape is degenerate (they are the same)
    // but obviously the sweep covers the whole circular region. So if
    // the loose bounds cross an axis, we just use those loose bounds.
    // The calling code might need to subdivide. (But also note that
    // near the axes is where AABBs are more likely to be tight
    // bounds, so it may also not matter!)

    if (loose_box.x.ContainsOrApproachesZero() ||
        loose_box.y.ContainsOrApproachesZero()) {
      return {Translate(loose_box)};
    } else {
      // Otherwise, we can compute some tighter bounds.

      // HERE: Ways to compute a complex of AABBs that more tightly
      // bounds the swept AABB. Currently we just do the same as above,
      // which is pointless!


      // PERF: We can do much better than to use a union of AABBs
      // here! The key thing is that when the angle is near 45°, the
      // shape of the sweep is more like a diagonal line. So AABBs are
      // inefficient in the worst way for our problem (the inner
      // corner of the AABB is unoccupied, and appears to intersect
      // the similarly-angled edge that we test against).
      //
      // Note: Currently we document the meaning of the return value
      // here as "every point in the swept shape is in the union of
      // the AABBs," which is nice and simple. But when we actually
      // test this against the outer hull below, we only use a weaker
      // implication: "If every AABB is certainly on the wrong side of
      // the line, then all the points in the swept shape are on the
      // wrong side of the line."

      return {Translate(loose_box)};
    }
  }

  struct ProcessResult {
    std::variant<Split, Impossible> result;

    // Debug info / stats when requested.

    // The bounds for the inner points. We only have these if we
    // make it to a certain point in the test.
    std::vector<Vec2ival> inner;
    // Experimental: Bounding complexes for the rotated inner points.
    // A bounding complex is a union of some AABBs.
    // For this problem, this should be interpreted as "if all of
    // these AABBs are outside an edge, then the point is definitely
    // outside the edge."
    std::vector<std::vector<Vec2ival>> complexes;

    std::vector<Discival> discs;
  };


  ProcessResult ProcessOne(const Volume &volume, bool get_stats) {
    const Bigival &outer_azimuth = volume[OUTER_AZIMUTH];
    const Bigival &outer_angle = volume[OUTER_ANGLE];
    const Bigival &inner_azimuth = volume[INNER_AZIMUTH];
    const Bigival &inner_angle = volume[INNER_ANGLE];
    const Bigival &inner_rot = volume[INNER_ROT];
    const Bigival &inner_x = volume[INNER_X];
    const Bigival &inner_y = volume[INNER_Y];

    // To start with, we require the width of the intervals describing
    // the spherical coordinates to be modest. The main issue is that
    // when we convert this to a view position, we need that vector
    // (now represented three-dimensional axis-aligned bounding box) to
    // not include something degenerate like the origin, or else
    // we won't even be able to compute intervals to later reject it
    // as too coarse.

    // 2/5 is about π/8, which should be enough to avoid degeneracy.
    // We don't want to go overboard with this gridding here, because
    // we have n^4 cells just to start...
    BigRat MIN_ANGLE = BigRat(2, 5);
    BigRat oz_width = outer_azimuth.Width();
    BigRat oa_width = outer_angle.Width();

    BigRat iz_width = inner_azimuth.Width();
    BigRat ia_width = inner_angle.Width();

    const BigRat min_width =
      BigRat::Min(
          BigRat::Min(
              BigRat::Min(oz_width, oa_width),
              BigRat::Min(inner_x.Width(),
                          inner_y.Width())),
          BigRat::Min(
              inner_rot.Width(),
              BigRat::Min(iz_width, ia_width)));

    {
      ParameterSet must_split;
      if (oz_width > MIN_ANGLE) must_split.Add(OUTER_AZIMUTH);
      if (oa_width > MIN_ANGLE) must_split.Add(OUTER_ANGLE);

      if (!must_split.Empty()) {
        return ProcessResult{.result = Split(must_split)};
      }
    }

    // This is enough to test whether we're in the outer patch;
    // we'd like to exclude large regions ASAP (without e.g. forcing
    // splits on the inner parameters), so compute and test that
    // now.

    // TODO: Should have some principled derivation of epsilon here.
    // There's no correctness problem, but I just pulled
    // one one-millionth out of thin air and maybe it should be
    // much smaller (or larger?).
    // It does need to at least get smaller as the intervals get
    // smaller.
    BigInt inv_epsilon = min_width.Denominator() * 1024 * 1024;

    Bigival osina = outer_angle.Sin(inv_epsilon);
    Vec3ival oviewpos = Vec3ival(
        osina * outer_azimuth.Cos(inv_epsilon),
        osina * outer_azimuth.Sin(inv_epsilon),
        outer_angle.Cos(inv_epsilon));

    if (!MightHaveCode(outer_code, outer_mask, oviewpos)) {
      return ProcessResult{.result = Impossible(OUTSIDE_OUTER_PATCH)};
    }

    {
      Ballival oviewposball = SphericalPatchBall(outer_azimuth,
                                                 outer_angle,
                                                 inv_epsilon);
      if (!MightHaveCodeWithBall(outer_code, outer_mask, oviewposball)) {
        return ProcessResult{.result = Impossible(OUTSIDE_OUTER_PATCH_BALL)};
      }
    }

    // Generating the frames has a precondition that the z-axis not
    // contained in the view interval. Since we know that none of the
    // canonical patches contain the z axis, we just subdivide if it
    // might be included. Do this ASAP so that we don't have to keep
    // encountering this after splitting other params.
    if (oviewpos.x.ContainsOrApproachesZero() &&
        oviewpos.y.ContainsOrApproachesZero()) {
      return ProcessResult{.result = Split({OUTER_AZIMUTH, OUTER_ANGLE})};
    }

    // TODO: if it's not the case that we're entirely within the outer
    // patch, prioritize splitting outer angle/azimuth, unless the
    // cells are tiny. We could maybe get into a situation where we
    // are unable to make progress because we are not actually in the
    // patch, and the hull is no longer even close to an attainable
    // shape. In that case, there may be actual solutions, and we
    // might keep trying to bisect to rule them out (but can't).

    // Now the same idea for the inner patch.

    {
      ParameterSet must_split;
      if (iz_width > MIN_ANGLE) must_split.Add(INNER_AZIMUTH);
      if (ia_width > MIN_ANGLE) must_split.Add(INNER_ANGLE);

      if (!must_split.Empty()) {
        return ProcessResult{.result = Split(must_split)};
      }
    }

    Bigival isina = inner_angle.Sin(inv_epsilon);
    Vec3ival iviewpos = Vec3ival(
        isina * inner_azimuth.Cos(inv_epsilon),
        isina * inner_azimuth.Sin(inv_epsilon),
        inner_angle.Cos(inv_epsilon));

    if (!MightHaveCode(inner_code, inner_mask, iviewpos)) {
      return {Impossible(OUTSIDE_INNER_PATCH)};
    }

    {
      Ballival iviewposball = SphericalPatchBall(inner_azimuth,
                                                 inner_angle,
                                                 inv_epsilon);
      if (!MightHaveCodeWithBall(inner_code, inner_mask, iviewposball)) {
        return ProcessResult{.result = Impossible(OUTSIDE_INNER_PATCH_BALL)};
      }
    }

    // As above: Can't contain the z axis.
    if (iviewpos.x.ContainsOrApproachesZero() &&
        iviewpos.y.ContainsOrApproachesZero()) {
      return ProcessResult{.result = Split({INNER_AZIMUTH, INNER_ANGLE})};
    }


    // Now, our interval overlaps the patches. But further
    // subdivide unless it is entirely within the patch, or
    // is small. (Heuristic)
    if (!VolumeInsidePatches(volume)) {
      // About one degree.
      BigRat MIN_SMALL_ANGLE(3, 172);

      ParameterSet must_split;
      if (oz_width > MIN_SMALL_ANGLE) must_split.Add(OUTER_AZIMUTH);
      if (oa_width > MIN_SMALL_ANGLE) must_split.Add(OUTER_ANGLE);
      if (iz_width > MIN_SMALL_ANGLE) must_split.Add(INNER_AZIMUTH);
      if (ia_width > MIN_SMALL_ANGLE) must_split.Add(INNER_ANGLE);

      if (!must_split.Empty()) {
        return ProcessResult{.result = Split(must_split)};
      }
    }


    // Get outer and inner frame.
    Frame3ival outer_frame = FrameFromViewPos(oviewpos, inv_epsilon);
    Frame3ival inner_frame = FrameFromViewPos(iviewpos, inv_epsilon);

    // Compute the hulls. Because we're using interval arithmetic,
    // the specifics of the algebra can be quite important. First,
    // the outer hull, whose vertices we call va, vb, etc.

    // The raw rotated vertices of the hull; va.
    // We don't actually use these now!
#if 0
    std::vector<Vec2ival> outer_shadow;
    outer_shadow.reserve(outer_hull.size());
    for (int vidx : outer_hull) {
      const BigVec3 &pa = scube.vertices[vidx];
      outer_shadow.push_back(
          TransformPointTo2D(outer_frame, pa));
    }
#endif

    // The hull edge vb-va, rotated by the outer frame.
    // We already precomputed the exact 3D vectors, so we
    // are just rotating and projecting those to 2D here.
    std::vector<Vec2ival> outer_edge;
    outer_edge.reserve(outer_hull.size());
    for (int idx = 0; idx < outer_hull.size(); idx++) {
      const BigVec3 &edge_3d = outer_edge3d[idx];
      outer_edge.push_back(TransformPointTo2D(outer_frame, edge_3d));
    }

    // The cross product va × vb.
    std::vector<Bigival> outer_cross_va_vb;
    outer_cross_va_vb.reserve(outer_hull.size());
    for (int idx = 0; idx < outer_hull.size(); idx++) {
      // Can derive this from the original 3D edge, and we've
      // precomputed its 3D cross product.
      const BigVec3 &edge_cross = outer_cx3d[idx];
      outer_cross_va_vb.push_back(Dot(oviewpos, edge_cross));
    }

    std::vector<Vec2ival> inner;
    if (get_stats) {
      inner.reserve(inner_hull.size());
    }

    std::vector<std::vector<Vec2ival>> complexes;
    if (get_stats) {
      complexes.reserve(inner_hull.size());
    }

    std::vector<Discival> discs;
    if (get_stats) {
      discs.reserve(inner_hull.size());
    }

    // Compute the inner hull point-by-point, which we call v. We can
    // exit early if any of these points are definitely outside the
    // outer hull.
    std::optional<RejectionReason> proved = std::nullopt;
    for (int idx : inner_hull) {
      const BigVec3 &original_v = scube.vertices[idx];
      Vec2ival proj_v = TransformPointTo2D(inner_frame, original_v);

      // The simpler thing here would be to compute bounds on the point
      // v as a Vec2ival. These are axis-aligned bounding boxes. But
      // since they are axis-aligned, rotating (especially by e.g. 45°)
      // and creating a new AABB will lose information. This is especially
      // pernicious for these line-side tests, since it's easy to have a
      // situation where the corner of the AABB intersects an edge, but
      // none of the bounded points would have.
      //
      // Instead we allow the possibility of a union of multiple AABBs
      // to represent the possible locations of the point. We call
      // this a "complex."
      std::vector<Vec2ival> complex =
        GetBoundingComplex(proj_v, inner_rot, inv_epsilon, inner_x, inner_y);

      // XXX bias: BigRat(11, 10)
      Discival disc_in(proj_v);
      Discival rot_disc = RotateDiscInnerBias(
          disc_in, inner_rot, BigRat(3, 2), inv_epsilon);
      Discival disc = TranslateDisc(rot_disc, inner_x, inner_y, inv_epsilon);

      if (get_stats) {
        // PERF copying!
        complexes.push_back(complex);

        // Also rotate and translate it.
        Vec2ival rot_v = Rotate2D(proj_v, inner_rot, inv_epsilon);
        Vec2ival v(rot_v.x + inner_x, rot_v.y + inner_y);
        inner.push_back(v);
        discs.push_back(disc);
      }

      // If we already succeeded, the only thing we need to do is
      // compute the inputs for get_stats. We shouldn't even get
      // here unless get_stats is on.
      if (proved.has_value()) {
        CHECK(get_stats) << "Bug";
        continue;
      }

      // Now, we reject this cell if the point is definitely outside
      // the outer hull. Since the outer hull is a convex hull
      // containing the origin, in screen clockwise (cartesian ccw)
      // winding order, we can just do this as a series of line-side
      // tests.
      // CHECK(outer_hull.size() == outer_shadow.size());
      CHECK(outer_hull.size() == outer_edge.size());
      CHECK(outer_hull.size() == outer_cross_va_vb.size());
      for (int start = 0; start < outer_hull.size(); start++) {
        [[maybe_unused]] int end = (start + 1) % outer_hull.size();

        // Do line-side test. Specifically, we can assume the origin
        // is screen-clockwise from the edge va->vb. Then we want to ask if
        // point v's interval is definitely completely on the other
        // side of the edge.

        // There are multiple ways to test this, and they could have
        // different types of over-conservativity (from dependency problem)
        // in different situations. So we perform multiple tests to
        // try to reject the cell.
        // (Many of these are redundant and the first few are pretty
        // insensitive because of many reuses of dependent terms. Should
        // clean this up!)

        // The naive test would be a simple cross product.
        // Note dependency problem: va.x and va.y both appear twice.
        // vb and va depend on one another because they are the result
        // of a 2D rotation (and of course they both depend on the
        // outer orientation).
        // Bigival cross_product1 =
        //   (vb.x - va.x) * (v.y - va.y) - (vb.y - va.y) * (v.x - va.x);

        // We can rearrange this so that only the term Cross(va, vb) has
        // the dependency problem. The edge and point are independent
        // because they only depend on the outer and inner parameters,
        // respectively.
        // Bigival cross_product2 =
        //   Cross(va, vb) + cross_vb_v_plus_cross_v_va;

        // Even better is to use some precomputed values, which is
        // both faster and more accurate. The idea here is that we can
        // compute the cross product for the 3D triangle (origin, pa,
        // pb) ahead of time (no intervals; just the exact vector).
        // This vector is like a representation of the triangle's
        // area. The area of that triangle's 2D shadow is related to
        // the cross product we want, and we can directly compute it
        // from the view position, because
        //  * oviewpos is exactly a unit vector (even though the
        //    interval representation will be inexact)
        //  * outer_frame here is the rotation that aligns oviewpos
        //    with the z axis
        //  * the projection is orthographic (ignoring z).
        const Bigival &cross_va_vb = outer_cross_va_vb[start];

        // We also have a better way of computing the
        // other terms. Rather than rotate the endpoints and
        // then subtract them, we can subtract first and
        // then rotate the resulting edge vector.
        // This has two advantages:
        //   * The original edge is exact, and we can precompute
        //     the vector.
        //   * This avoids subtracting two points that both
        //     depend on the same rotation, which would incur the
        //     dependency problem.
        const Vec2ival &edge = outer_edge[start];


        // Now we test every AABB in the complex.
        bool entire_complex_definitely_outside = true;
        for (const Vec2ival &aabb : complex) {
          Bigival cross_product4 =
            edge.x * aabb.y - edge.y * aabb.x + cross_va_vb;

          if (!cross_product4.MightBePositive()) {
            // ok
          } else {
            entire_complex_definitely_outside = false;
            break;
          }
        }

        if (entire_complex_definitely_outside) {
          proved = {POINT_OUTSIDE4};
          // We only need to finish all the points if we are getting
          // stats!
          if (!get_stats) break;
        }

        // Or is the disc outside?
        if (IsDiscOutsideEdge(disc, edge, cross_va_vb)) {
          proved = {POINT_OUTSIDE5};
          if (!get_stats) break;
        }
      }

      if (proved.has_value() && !get_stats) break;
    }

    ProcessResult res;
    // Always include these if we have them.
    res.inner = std::move(inner);
    res.complexes = std::move(complexes);
    res.discs = std::move(discs);

    if (proved.has_value()) {
      res.result = Impossible(proved.value());
    } else {
      // Failed to rule out this cell. Perform any split.
      res.result = Split();
    }

    return res;
  }

  std::mutex mu;
  ArcFour rc;
  std::unordered_map<RejectionReason, int64_t> rejection_count;
  int64_t times_split[NUM_DIMENSIONS] = {};

  // The hypervolume now done (this includes regions that we
  // determined are out of scope). Compare against the full volume.
  double volume_done = 0.0;
  // The volume that is excluded for being out of scope. Compare
  // against the full volume.
  double volume_outscope = 0.0;
  // The hypervolume where we definitively ruled out a solution.
  // Can compare this against (full - volume_outscope).
  double volume_proved = 0.0;

  int RandomParameterFromSet(ParameterSet params) {
    CHECK(!params.Empty()) << "No dimensions to split on?";
    const int num = params.Size();
    MutexLock ml(&mu);
    int idx = RandTo(&rc, num);
    return params[idx];
  }

  // Choose the parameter that has the largest absolute width.
  int BestParameterFromSet(const Volume &volume, ParameterSet params) {
    CHECK(!params.Empty());
    if (params.Size() == 1) return params[0];

    int best_d = params[0];
    BigRat best_w = volume[best_d].Width();
    for (int d = best_d + 1; d < NUM_DIMENSIONS; d++) {
      if (params.Contains(d)) {
        BigRat w = volume[d].Width();
        if (w > best_w) {
          best_d = d;
          best_w = std::move(w);
        }
      }
    }

    return best_d;
  }

  std::array<double, NUM_DIMENSIONS> SampleFromVolume(const Volume &volume) {
    // Access random
    MutexLock ml(&mu);
    std::array<double, NUM_DIMENSIONS> sample;
    for (int d = 0; d < NUM_DIMENSIONS; d++) {
      double w = volume[d].Width().ToDouble();
      // If the widths are too small to even be distinct doubles,
      // we probably should just avoid sampling?
      if (w == 0.0) {
        sample[d] = volume[d].LB().ToDouble();
      } else {
        sample[d] = volume[d].LB().ToDouble() +
          w * RandDouble(&rc);
      }
    }
    return sample;
  }


  // Corner of the hypervolume indicated by bitmask.
  std::array<double, NUM_DIMENSIONS> VolumeCorner(const Volume &volume,
                                                  uint32_t corner) {
    std::array<double, NUM_DIMENSIONS> sample;
    for (int d = 0; d < NUM_DIMENSIONS; d++) {
      if (corner & (1 << d)) {
        sample[d] = volume[d].LB().ToDouble();
      } else {
        sample[d] = volume[d].UB().ToDouble();
      }
    }
    return sample;
  }

  vec3 ViewFromSpherical(double angle, double azimuth) {
    double sina = std::sin(angle);
    vec3 view(
        sina * std::cos(azimuth),
        sina * std::sin(azimuth),
        std::cos(angle));
    return view;
  }

  struct Shadows {
    std::vector<vec2> outer;
    std::vector<vec2> inner;
    bool opatch = false, ipatch = false;
    bool corner = false;
  };

  static constexpr int N_SAMPLES = 512;
  std::vector<Shadows> SampleShadows(const Volume &volume,
                                     const ProcessResult &pr) {

    auto TransformHull = [this](const std::vector<int> &hull,
                                const frame3 &f) -> std::vector<vec2> {
        std::vector<vec2> shadow;
        for (int idx : hull) {
          const vec3 &pt = small_scube.vertices[idx];
          vec3 v = transform_point(f, pt);
          shadow.push_back(vec2(v.x, v.y));
        }
        return shadow;
      };

    auto RotateAndTranslate = [](double alpha, double tx, double ty,
                                 std::vector<vec2> *shadow) {

        double sina = std::sin(alpha);
        double cosa = std::cos(alpha);

        for (int i = 0; i < shadow->size(); i++) {
          vec2 v = (*shadow)[i];
          vec2 r(v.x * cosa - v.y * sina,
                 v.x * sina + v.y * cosa);
          r.x += tx;
          r.y += ty;
          (*shadow)[i] = r;
        }
      };

    std::vector<Shadows> shadows;

    // PERF: Could compute the double-based intervals once
    for (int s = 0; s < N_SAMPLES; s++) {
      std::array<double, NUM_DIMENSIONS> sample;
      // Sample the extremities (corners) exhaustively,
      // then some random samples.
      const bool corner = s < (1 << NUM_DIMENSIONS);
      if (corner) {
        sample = VolumeCorner(volume, s);
      } else {
        sample = SampleFromVolume(volume);
      }

      vec3 oview = ViewFromSpherical(
          sample[OUTER_ANGLE],
          sample[OUTER_AZIMUTH]);
      frame3 oviewpos = FrameFromViewPos(oview);
      const bool opatch = boundaries.GetCode(oview) == outer_code;

      vec3 iview = ViewFromSpherical(
          sample[INNER_ANGLE],
          sample[INNER_AZIMUTH]);
      frame3 iviewpos = FrameFromViewPos(iview);
      const bool ipatch = boundaries.GetCode(iview) == inner_code;

      // Plot hulls.
      std::vector<vec2> outer_shadow =
        TransformHull(outer_hull, oviewpos);

      std::vector<vec2> inner_shadow =
        TransformHull(inner_hull, iviewpos);

      // And rotate.
      RotateAndTranslate(sample[INNER_ROT],
                         sample[INNER_X], sample[INNER_Y],
                         &inner_shadow);


      shadows.push_back(Shadows{
          .outer = outer_shadow,
          .inner = inner_shadow,
          .opatch = opatch,
          .ipatch = ipatch,
          .corner = corner});
    }

    return shadows;
  }


  // Get the average efficiency of intervals (this just looks at
  // the inner intervals right now) if possible. Stats must have been
  // computed and the result has to be Impossible (point_outside).
  std::optional<double> ComputeEfficiency(const Volume &volume,
                                          const ProcessResult &pr,
                                          const std::vector<Shadows> &shadows) {
    double efficiency_numer = 0.0;
    int efficiency_denom = 0;
    for (int p = 0; p < pr.inner.size(); p++) {
      // We have an AABB to measure against.
      // Get all the sampled points for this vertex.

      std::vector<vec2> sampled_points;
      sampled_points.reserve(N_SAMPLES);
      for (const auto &s : shadows) {
        CHECK(p < s.inner.size());
        sampled_points.push_back(s.inner[p]);
      }

      // This is an estimate of how much area the actual
      // shape takes up. (Some of the shapes are non-convex,
      // like if the rotation angle ranges from 0 to 2π then
      // you get a kind of donut. So to be perfectly clear,
      // effiency here is judged relative to the convex hull
      // of the shape.)
      std::vector<int> sample_hull = QuickHull(sampled_points);
      double hull_area = AreaOfHull(sampled_points, sample_hull);

      const Vec2ival &aabb = pr.inner[p];

      double efficiency = hull_area / aabb.Area().ToDouble();
      efficiency_numer += efficiency;
      efficiency_denom++;
    }

    return efficiency_denom ?
      std::make_optional(efficiency_numer / efficiency_denom) :
      std::nullopt;
  }

  void MakeSampleImage(const Volume &volume, const ProcessResult &pr,
                       std::string_view msg) {
    std::string filename = std::format("inubs/sample-{}-{}.png",
                                       time(nullptr), msg);
    status.Print("Sample volume:\n{}\n",
                 VolumeString(volume, true));


    const int WIDTH = 1024, HEIGHT = 1024;
    ImageRGBA img(WIDTH, HEIGHT);
    img.Clear32(0x000000FF);

    std::vector<Shadows> shadows = SampleShadows(volume, pr);

    Bounds bounds;
    for (const Shadows &s : shadows) {
      for (vec2 v : s.outer)
        bounds.Bound(v.x, v.y);
      for (vec2 v : s.inner)
        bounds.Bound(v.x, v.y);
    }

    Bounds::Scaler scaler =
      bounds.ScaleToFitWithMargin(WIDTH, HEIGHT, 16, true);

    std::optional<double> efficiency =
      ComputeEfficiency(volume, pr, shadows);

    // Stats on patch containment.
    int num_corners = 0;
    // Random samples
    int num_samples = 0;
    int num_corners_in_inner = 0;
    int num_corners_in_outer = 0;
    int num_samples_in_inner = 0;
    int num_samples_in_outer = 0;
    for (const Shadows &s : shadows) {
      if (s.corner) {
        num_corners++;

        if (s.opatch) num_corners_in_outer++;
        if (s.ipatch) num_corners_in_inner++;

      } else {
        num_samples++;

        if (s.opatch) num_samples_in_outer++;
        if (s.ipatch) num_samples_in_inner++;
      }
    }

    // Draw 'em.
    // We don't care about the correspondence between inner/outer;
    // were just trying to visualize the intervals.
    for (int i = 0; i < shadows.size(); i++) {
      const Shadows &s = shadows[i];
      if (i == 0) {
        for (int a = 0; a < s.outer.size(); a++) {
          int b = (a + 1) % s.outer.size();
          const auto &[ax, ay] = scaler.Scale(s.outer[a].x, s.outer[a].y);
          const auto &[bx, by] = scaler.Scale(s.outer[b].x, s.outer[b].y);
          img.BlendLine32(ax, ay, bx, by, 0xFF333388);
        }
      }
      for (const vec2 &v : s.outer) {
        const auto &[sx, sy] = scaler.Scale(v.x, v.y);
        if (s.opatch) {
          img.BlendFilledCircleAA32(sx, sy, 2, 0xFF333399);
        } else {
          // Hollow means we're not actually in the patch.
          img.BlendThickCircleAA32(sx, sy, 3.0, 1.0, 0xFF333399);
        }
      }
    }

    for (int i = 0; i < shadows.size(); i++) {
      const Shadows &s = shadows[i];
      if (i == 0) {
        for (int a = 0; a < s.inner.size(); a++) {
          int b = (a + 1) % s.inner.size();
          const auto &[ax, ay] = scaler.Scale(s.inner[a].x, s.inner[a].y);
          const auto &[bx, by] = scaler.Scale(s.inner[b].x, s.inner[b].y);
          img.BlendLine32(ax, ay, bx, by, 0x33FF3388);
        }
      }

      for (const vec2 &v : s.inner) {
        const auto &[sx, sy] = scaler.Scale(v.x, v.y);
        if (s.ipatch) {
          img.BlendFilledCircleAA32(sx, sy, 2, 0x33FF3399);
        } else {
          img.BlendThickCircleAA32(sx, sy, 3.0, 1.0, 0x33FF3399);
        }
      }
    }

    auto DrawAABB = [&](const Vec2ival &v, uint32_t color) {
        double x0 = v.x.LB().ToDouble();
        double x1 = v.x.UB().ToDouble();
        double y0 = v.y.LB().ToDouble();
        double y1 = v.y.UB().ToDouble();

        // Draw AABB. We should include this in the bounds above
        // or at least indicate if it's going off-screen?
        const auto &[sx0, sy0] = scaler.Scale(x0, y0);
        const auto &[sx1, sy1] = scaler.Scale(x1, y1);
        img.BlendBox32(sx0, sy0, sx1 - sx0, sy1 - sy0, color, {});
      };

    auto DrawCircle = [&](const Discival &disc, uint32_t color) {
        const auto &[sx, sy] = scaler.Scale(disc.center.x.ToDouble(),
                                            disc.center.y.ToDouble());
        // assume 1:1 aspect ratio

        double r = std::sqrt(disc.radius_sq.ToDouble());

        double sr = scaler.ScaleX(disc.center.x.ToDouble() + r) - sx;

        /*
        // XXX can go back to std::sqrt(rsq.todouble)
        const auto &[slb, sub] =
          BigRat::SqrtBounds(disc.radius_sq,
                             BigInt(1024 * 1024 * 1024) * 1024 * 1024);
        double r1 = scaler.ScaleX(slb.ToDouble());
        double r2 = scaler.ScaleY(slb.ToDouble());
        img.BlendCircle32(sx, sy, r1, color);
        img.BlendCircle32(sx, sy, r2, color | 0xFF000000);
        */

        img.BlendCircle32(sx, sy, sr, color);
      };

    for (const Vec2ival &v : pr.inner) {
      DrawAABB(v, 0x33FF3366);
    }

    // And the complexes.
    for (const std::vector<Vec2ival> &complex : pr.complexes) {
      for (const Vec2ival &v : complex) {
        DrawAABB(v, 0xAAFF3366);
      }
    }

    for (const Discival &disc : pr.discs) {
      DrawCircle(disc, 0xCCFF3366);
    }

    std::vector<std::string> vs =
      Util::SplitToLines(VolumeString(volume, true));
    int yy = 8;
    for (const std::string &v : vs) {
      img.BlendText32(8, yy, 0xAAAAAAFF, v);
      yy += ImageRGBA::TEXT_HEIGHT + 2;
    }

    auto PctString = [](std::string_view label, int n, int d){
        return
          std::format("{}: {}" AGREY("/") "{} "
                      AGREY("(") "{:.1f}%" AGREY(")"),
                      label,
                      n, d,
                      (100.0 * n) / d);
      };

    yy += ImageRGBA::TEXT_HEIGHT + 2;
    img.BlendText32(
        8, yy, 0xFFCCCCFF,
        PctString("outer corners", num_corners_in_outer, num_corners));
    yy += ImageRGBA::TEXT_HEIGHT + 2;
    img.BlendText32(
        8, yy, 0xFFCCCCFF,
        PctString("outer sample", num_samples_in_outer, num_samples));
    yy += ImageRGBA::TEXT_HEIGHT + 2;
    img.BlendText32(
        8, yy, 0xCCFFCCFF,
        PctString("inner corners", num_corners_in_inner, num_corners));
    yy += ImageRGBA::TEXT_HEIGHT + 2;
    img.BlendText32(
        8, yy, 0xCCFFCCFF,
        PctString("inner sample", num_samples_in_inner, num_samples));

    if (efficiency.has_value()) {
      yy += 2 * (ImageRGBA::TEXT_HEIGHT + 2);
      img.BlendText32(
          8, yy, 0x33CCFFFF,
          std::format("inner AABB efficiency: " AWHITE("{:.2f}") "%",
                      efficiency.value() * 100.0));
    }

    yy += 2 * (ImageRGBA::TEXT_HEIGHT + 2);
    if (const Impossible *imp = std::get_if<Impossible>(&pr.result)) {
      img.BlendText32(8, yy, 0xFFFF33FF,
                      std::format("Result: Impossible! " ACYAN("{}"),
                                  RejectionReasonString(imp->reason)));
    } else if (const Split *split = std::get_if<Split>(&pr.result)) {
      std::string par;
      for (int p = 0; p < NUM_DIMENSIONS; p++) {
        if (split->parameters.Contains(p)) {
          if (!par.empty()) par += ", ";
          par += ParameterName(p);
        }
      }
      img.BlendText32(8, yy, 0x33FFFFFF,
                      std::format("Result: Split ({})", par));
    }


    img.Save(filename);
    status.Print("Wrote " AGREEN("{}"), filename);
  }

  std::optional<uint64_t> GetCornerCode(const Volume &volume,
                                        int angle, int azimuth) {
    CHECK(angle >= 0 && angle < NUM_DIMENSIONS);
    CHECK(azimuth >= 0 && azimuth < NUM_DIMENSIONS);
    const Bigival &angle_ival = volume[angle];
    const Bigival &azimuth_ival = volume[azimuth];

    uint64_t all_code = 0;
    for (int b = 0; b < 0b11; b++) {
      double angle = (b & 0b01) ? angle_ival.LB().ToDouble() :
        angle_ival.UB().ToDouble();
      double azimuth = (b & 0b10) ? azimuth_ival.LB().ToDouble() :
        azimuth_ival.UB().ToDouble();

      vec3 view = ViewFromSpherical(angle, azimuth);
      uint64_t code = boundaries.GetCodeSloppy(view);
      if (b == 0 || code == all_code) {
        all_code = code;
      } else {
        return {};
      }
    }

    return {all_code};
  }

  // Assumes double precision works, but is otherwise exact.
  bool VolumeInsidePatches(const Volume &volume) {
    std::optional<uint64_t> oc =
      GetCornerCode(volume, OUTER_ANGLE, OUTER_AZIMUTH);
    if (!oc.has_value() || oc.value() != outer_code)
      return false;

    std::optional<uint64_t> ic =
      GetCornerCode(volume, INNER_ANGLE, INNER_AZIMUTH);
    if (!ic.has_value() || ic.value() != inner_code)
      return false;

    return true;
  }

  double efficiency_total = 0.0;
  int64_t efficiency_count = 0;

  void Expand() {
    // Get all the unsolved leaves.
    std::deque<std::pair<Volume, std::shared_ptr<Hypercube::Node>>> q;

    for (auto &p : hypercube->GetLeaves(&volume_done)) {
      q.emplace_back(std::move(p));
    }

    status.Print("Start Expand. Remaining leaves: {}\n", q.size());
    Timer run_timer;

    Periodically status_per(1);
    Periodically save_per(15 * 60);
    Periodically sample_per(60 * 10);
    Periodically sample_proved_per(60 * 1);
    Periodically render_per(60 * 1.1); // 0.1);

    BigRat full_volume = Hypervolume(hypercube->bounds);
    double full_volume_d = full_volume.ToDouble();

    bool get_stats_next = false;
    while (!q.empty()) {
      Volume volume;
      std::shared_ptr<Hypercube::Node> node;

      if (q.size() > 8192) {
        std::tie(volume, node) = q.back();
        q.pop_back();
      } else {
        std::tie(volume, node) = q.front();
        q.pop_front();
      }

      CHECK(node.get() != nullptr);

      status_per.RunIf([&]() {
          std::string rr;
          for (const auto &[reason, count] : rejection_count) {
            AppendFormat(&rr, ACYAN("{}") ": {}  ",
                         RejectionReasonString(reason), count);
          }

          std::string splitcount;
          for (int d = 0; d < NUM_DIMENSIONS; d++) {
            AppendFormat(&splitcount, "{} ", times_split[d]);
          }

          double time_each =
            run_timer.Seconds() / counter_processed.Read();

          double done_pct = (volume_done * 100.0) /
            full_volume_d;

          double in_volume_d = (full_volume_d - volume_outscope);

          // Proved percentage is provide volume over the
          // amount that is in scope.
          double proved_pct = (volume_proved * 100.0) /
            in_volume_d;

          // Progress bar wants integer fraction.
          const uint64_t denom = int64_t{1'000'000'000'000};
          const uint64_t numer = (proved_pct / 100.0) * denom;

          ANSI::ProgressBarOptions opt;
          opt.include_frac = false;
          opt.include_percent = false;
          std::string progress =
            ANSI::ProgressBar(
                numer, denom,
                std::format(
                    "Done: {:.8g} {:.2f}% Proved: {:.8g} {:.6f}%",
                    volume_done, done_pct,
                    volume_proved, proved_pct),
                run_timer.Seconds(),
                opt);

          std::string eff_str =
            efficiency_count > 0 ?
            std::format(APURPLE("{:.4f}") "% " AGREY("({})"),
                        (efficiency_total * 100.0) / efficiency_count,
                        efficiency_count) :
            ARED("??");

          status.Status(
              AWHITE("—————————————————————————————————————————") "\n"
              // "Put volume information here!\n"
              "Split count: {}\n"
              "{}\n"
              "{}\n"
              "Full vol: {:.5f}  in vol: {:.5f}  out vol: {:.5f}\n"
              "{} processed, "
              "{} " AGREEN("✔") ", "
              "{} " AORANGE("⊹") ". "
              "{} queued, {} ea.\n"
              AORANGE("X") "mid: {}  "
              AORANGE("X") "disc: {}  "
              "In: {}   AABB efficiency: {}\n"
              "{}\n" // bar
              ,
              splitcount,
              VolumeString(volume, true),
              rr,
              full_volume_d, in_volume_d, volume_outscope,
              counter_processed.Read(),
              counter_completed.Read(),
              counter_split.Read(),
              q.size(),
              ANSI::Time(time_each),
              counter_bad_midpoint.Read(),
              counter_degenerate_disc.Read(),
              counter_inside.Read(),
              eff_str,
              progress);
        });

      save_per.RunIf([&](){
          status.Print("Saving to {}...\n", filename);
          hypercube->ToDisk(filename);
        });

      // Periodically turn on stats gathering until we get some
      // data.
      get_stats_next = get_stats_next || (counter_processed.Read() % 64) == 0;

      ProcessResult res = ProcessOne(volume, get_stats_next);
      counter_processed++;

      if (get_stats_next && render_per.ShouldRun()) {
        MakeSampleImage(volume, res, "any");
      }

      if (!res.inner.empty()) {
        // Got data to compute stats.
        get_stats_next = false;
        if (std::optional<double> efficiency =
            ComputeEfficiency(volume, res, SampleShadows(volume, res))) {
          efficiency_count++;
          efficiency_total += efficiency.value();
        }
      }

      if (VolumeInsidePatches(volume)) {
        counter_inside++;
        // Sometimes also sample.
        sample_per.RunIf([&]() {
            MakeSampleImage(volume, res, "inside");
          });
      }

      if (Impossible *imp = std::get_if<Impossible>(&res.result)) {
        (void)imp;

        // Then mark the node as a leaf that has been ruled out.
        Hypercube::Leaf *leaf = std::get_if<Hypercube::Leaf>(node.get());
        CHECK(leaf != nullptr) << "Bug: We only expand leaves!";
        leaf->completed = time(nullptr);

        counter_completed++;

        double v = Hypervolume(volume).ToDouble();
        volume_done += v;
        switch (imp->reason) {
        case OUTSIDE_OUTER_PATCH:
        case OUTSIDE_INNER_PATCH:
        case OUTSIDE_OUTER_PATCH_BALL:
        case OUTSIDE_INNER_PATCH_BALL:
          volume_outscope += v;
          break;
        default:
          volume_proved += v;
          sample_proved_per.RunIf([&]() {
              MakeSampleImage(volume, res, "proved");
            });
          break;
        }

        /*
        status.Print(AGREEN("Success!") " Excluded cell (" ACYAN("{}") "). "
                     " Now {}.\n",
                     RejectionReasonString(imp->reason),
                     counter_completed.Read());
        */
        rejection_count[imp->reason]++;

      } else if (Split *split = std::get_if<Split>(&res.result)) {
        // Can't rule it out. So split. We use a random direction here (in
        // accordance with the split's mask) but we should consider being
        // systematic about it (e.g. split the longest dimension)?

        // int dim = RandomParameterFromSet(split->parameters);
        int dim = BestParameterFromSet(volume, split->parameters);
        CHECK(dim >= 0 && dim < NUM_DIMENSIONS);
        times_split[dim]++;
        Hypercube::Split split_node;
        split_node.axis = dim;

        const Bigival &oldival = volume[dim];
        Volume left = volume, right = volume;
        BigRat mid = SplitInterval(oldival);

        // left side is <, right side is >=.
        left[dim] = Bigival(oldival.LB(), mid, oldival.IncludesLB(), false);
        right[dim] = Bigival(mid, oldival.UB(), true, oldival.IncludesUB());
        split_node.split = std::move(mid);

        // Pending leaves.
        split_node.left = std::make_shared<Hypercube::Node>(
            Hypercube::Leaf());
        split_node.right = std::make_shared<Hypercube::Node>(
            Hypercube::Leaf());

        // Enqueue the leaves.
        q.emplace_back(std::move(left), split_node.left);
        q.emplace_back(std::move(right), split_node.right);

        *node = Hypercube::Node(std::move(split_node));

        counter_split++;

      } else {
        LOG(FATAL) << "Bad processresult";
      }
    }

    hypercube->ToDisk(filename);
    printf("Success " AGREEN(":)") "\n");
  }

  // Note we perform all the plane-side tests, even though only the
  // ones in the mask are sufficient. Because of idiosyncracies of how
  // the AABBs might intersect planes, though, we may get less
  // conservative rejection if we test all the planes. This is more
  // work up front, but it'd definitely be worth it to be able to save
  // all the work across the remaining dimensions!
  static constexpr bool TEST_ALL_PLANES = true;
  bool MightHaveCode(
      uint64_t code, uint64_t mask,
      const Vec3ival &v) const {
    for (int i = 0; i < boundaries.big_planes.size(); i++) {
      uint64_t pos = uint64_t{1} << i;
      if (TEST_ALL_PLANES || !!(pos & mask)) {
        const BigVec3 &normal = boundaries.big_planes[i];
        Bigival d = Dot(v, normal);
        if (pos & code) {
          // Must include positive region.
          if (!d.MightBePositive()) return false;
        } else {
          if (!d.MightBeNegative()) return false;
        }
      }
    }
    return true;
  }

  // Same idea, but with the view position bounded by a ball instead
  // of an AABB.
  bool MightHaveCodeWithBall(
      uint64_t code, uint64_t mask,
      const Ballival &ball) {

    for (int i = 0; i < boundaries.big_planes.size(); i++) {
      const uint64_t pos = uint64_t{1} << i;
      if (TEST_ALL_PLANES || !!(pos & mask)) {
        const BigVec3 &normal = boundaries.big_planes[i];

        // The center needs to be on the correct side
        // of the plane; the sign of the dot product gives us the
        // side.
        BigRat d_center = dot(ball.center, normal);

        // The ball's radius must be smaller than the distance to
        // the plane, or else it crosses the plane and we cannot
        // produce a definitive result here.
        //
        //   |dot(c, n)| / |n| > R
        //
        // We'll actually test the squared distances:
        //   dot(c, n)² / |n|² > R²
        //   dot(c, n)² > R² * |n|²

        BigRat margin_sq = ball.radius_sq * length_squared(normal);

        if (d_center * d_center > margin_sq) {
          // So the ball is entirely on one side or the other.
          // If it's on the wrong side, it cannot be in this patch.
          if (!!(pos & code)) {
            // Want positive dot product.
            if (BigRat::Sign(d_center) == -1) return false;
          } else {
            // Want negative dot product.
            if (BigRat::Sign(d_center) == 1) return false;
          }
        }

      }
    }

    return true;
  }

  Hypersolver() : rc("hyper"), scube(BigScube(SCUBE_DIGITS)),
    boundaries(scube) {
    patch_info = LoadPatchInfo("scube-patchinfo.txt");
    // We don't want every volume's endpoints to involve some
    // subdivision of an extremely accurate pi, and we don't need them
    // to; we just need the starting interval to *cover* [0, π] (or
    // 2π). So we use a simple rational upper bound to π.
    // Slightly larger than π. Accurate to 16 digits.
    // (Actually this is 3.1416...!)
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

    hypercube.reset(new Hypercube(bounds));

    // Test this out with a single pair to start.
    std::vector<std::pair<uint64_t, PatchInfo::CanonicalPatch>> canonical;
    for (const auto &[cc, p] : patch_info.canonical) {
      canonical.emplace_back(cc, p);
    }
    std::sort(canonical.begin(), canonical.end(),
              [](const auto &a,
                 const auto &b) {
                return a.first < b.first;
              });
    CHECK(canonical.size() >= 2);

    const PatchInfo::CanonicalPatch &outer = canonical[0].second;
    const PatchInfo::CanonicalPatch &inner = canonical[1].second;

    small_scube = SmallPoly(scube);

    outer_code = outer.code;
    inner_code = inner.code;
    outer_mask = outer.mask;
    inner_mask = inner.mask;

    outer_hull = outer.hull;
    inner_hull = inner.hull;

    // This program assumes the hulls have screen-clockwise (cartesian
    // ccw) winding order when viewed from within the patch, and that
    // they contain the origin.
    CheckHullRepresentation(outer_code, outer_mask, outer_hull);
    CheckHullRepresentation(inner_code, inner_mask, inner_hull);

    outer_cx3d.reserve(outer_hull.size());
    outer_edge3d.reserve(outer_hull.size());
    for (int n = 0; n < outer_hull.size(); n++) {
      const BigVec3 &va = scube.vertices[outer_hull[n]];
      const BigVec3 &vb =
        scube.vertices[outer_hull[(n + 1) % outer_hull.size()]];

      outer_cx3d.push_back(cross(va, vb));
      outer_edge3d.push_back(vb - va);
    }

    filename = std::format("hc-{}-{}.cube", outer_code, inner_code);

    if (Util::ExistsFile(filename)) {
      status.Print("Continuing from {}", filename);
      hypercube->FromDisk(filename);
    }
  }

  void CheckHullRepresentation(uint64_t code, uint64_t mask,
                               const std::vector<int> &hull) {
    vec3 view = GetVec3InPatch(&rc, boundaries, code, mask);
    frame3 view_frame = FrameFromViewPos(view);
    Mesh2D mesh = RotateAndProject(view_frame, small_scube);

    // Want screen clockwise (cartesian ccw) winding order. This
    // means positive area.
    CHECK(SignedAreaOfHull(mesh, hull) > 0.0);
    // Of course convex hull should be convex.
    CHECK(IsHullConvex(mesh.vertices, hull));
    // Must contain the origin.
    CHECK(PointInPolygon(vec2{0, 0}, mesh.vertices, hull));

    printf("Hull:\n");
    for (int p : hull) {
      vec2 v = mesh.vertices[p];
      printf("  (%.4f, %.4f),\n", v.x, v.y);
    }
  }

  Polyhedron small_scube;
  BigPoly scube;
  Boundaries boundaries;
  PatchInfo patch_info;
  std::unique_ptr<Hypercube> hypercube;
  std::string filename;
  uint64_t outer_code = 0, outer_mask = 0;
  uint64_t inner_code = 0, inner_mask = 0;

  std::vector<int> outer_hull, inner_hull;

  // Some expressions can equivalently be written in terms of cross
  // products of the original exact coordinates. This is much better
  // since we don't get propagated error from the dependency problem.
  // outer_cx[n] is cross(va, vb)
  // where va is scube.vertices[outer_hull[n]]
  //   and vb is scube.vertices[outer_hull[(n + 1) % outer_hull.size()]],
  // that is, an edge of the outer hull starting at the nth vertex.
  std::vector<BigVec3> outer_cx3d;

  // The vector vb-va, with va,vb as in the previous.
  std::vector<BigVec3> outer_edge3d;
};

int main(int argc, char **argv) {
  ANSI::Init();

  Hypersolver solver;
  solver.Expand();

  return 0;
}
