
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
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
#include <thread>
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

// TODO:
//  - air gapped work queue
//  - stats for rational size
//  - more timing stats
//  - note plane for out patch
//  - try NiceSin/NiceCos for outer loops

DECLARE_COUNTERS(counter_processed, counter_completed, counter_split,
                 counter_bad_midpoint, counter_inside,
                 counter_degenerate_disc);

static constexpr bool SELF_CHECK = false; // true;

static constexpr int SCUBE_DIGITS = 24;

using vec2 = yocto::vec<double, 2>;

StatusBar status = StatusBar(15);

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

struct Rejection {
  RejectionReason reason = REJECTION_UNKNOWN;
  // When the rejection reason is PT4 or PT5, then we found a point
  // that is definitely on the wrong side of some edge.
  // This is the index of that edge (start endpoint) and point inside
  // the outer and inner hulls, respectively. (NOT a vertex index.)
  std::optional<std::pair<int8_t, int8_t>> edge_point;
};

static std::string SerializeRejection(const Rejection &rej) {
  std::string ret = std::format("{}", (uint8_t)rej.reason);
  if (rej.edge_point.has_value()) {
    AppendFormat(&ret, " {} {}",
                 rej.edge_point.value().first,
                 rej.edge_point.value().second);
  }
  return ret;
}

// Must be a proper rejection (not UNKNOWN).
// Doesn't check the case that point/edge indices are too large.
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

  case POLY_AREA:
    // No metadata.
    break;

  case POINT_OUTSIDE1:
  case POINT_OUTSIDE2:
  case POINT_OUTSIDE3:
  case POINT_OUTSIDE4:
  case POINT_OUTSIDE5: {
    int64_t eidx = Util::ParseInt64(Util::Chop(&s), -1);
    int64_t pidx = Util::ParseInt64(Util::Chop(&s), -1);
    if (eidx < 0 || pidx < 0) return std::nullopt;
    ret.edge_point = std::make_optional(
        std::make_pair((int8_t)eidx, (int8_t)pidx));
    break;
  }
  }

  return ret;
}

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
    // L reason completed   (completed leaf)
    // or
    // E                    (empty leaf)
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

          char cmd = line[0];
          line.remove_prefix(1);
          if (!line.empty() && line[0] == ' ')
            line.remove_prefix(1);

          if (cmd == 'E') {
            auto leaf =
              std::make_shared<Hypercube::Node>(Leaf{
                  .completed = 0,
                  .rejection = {.reason = REJECTION_UNKNOWN},
                });
            stack.push_back(std::move(leaf));

          } else if (cmd == 'L') {
            int64_t comp = Util::ParseInt64(Util::Chop(&line), -1);
            CHECK(comp >= 0) << line;

            std::optional<Rejection> rej = ParseRejection(line);
            CHECK(rej.has_value()) << raw_line;

            auto leaf =
              std::make_shared<Hypercube::Node>(Leaf{
                  .completed = comp,
                  .rejection = rej.value(),
                });
            stack.push_back(std::move(leaf));

          } else if (cmd == 'S') {
            int axis = Util::ParseInt64(Util::Chop(&line), -1);
            CHECK(axis >= 0 && axis < NUM_DIMENSIONS) << raw_line;
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
            LOG(FATAL) << "Bad line in cube file: " << raw_line;
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
          if (leaf->completed) {
            std::string line =
              std::format("L {} {}\n",
                          leaf->completed,
                          SerializeRejection(leaf->rejection));
            fprintf(f, "%s", line.c_str());
          } else {
            fprintf(f, "E\n");
          }
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
      double *volume_outscope, double *volume_proved) {
    *volume_outscope = 0.0;
    *volume_proved = 0.0;

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

// We work with spherical coordinates (azimuth/angle intervals) to
// represent the bounds on the outer and inner view positions.
// Various operations will want to have Sin/Cos of these angles,
// so we compute those up front. We can spend some more time getting
// high quality approximations since we will reuse them.
struct ViewBoundsTrig {

  ViewBoundsTrig(Bigival azimuth_in, Bigival angle_in,
                 BigInt inv_epsilon_in) :
    azimuth(std::move(azimuth_in)), angle(std::move(angle_in)),
    inv_epsilon(std::move(inv_epsilon_in)) {

    // Since we use these a lot of times, we spend extra time up front
    // to compute higher quality bounds (simpler rationals). We can
    // still stay approximately within the inv_epsilon target.
    sin_az = NiceSin(azimuth, inv_epsilon);
    cos_az = NiceCos(azimuth, inv_epsilon);

    sin_an = NiceSin(angle, inv_epsilon);
    cos_an = NiceCos(angle, inv_epsilon);
  }

  Bigival azimuth;
  Bigival angle;

  BigInt inv_epsilon;

  Bigival sin_az, cos_az;
  Bigival sin_an, cos_an;
};

// Same idea, for the 2D rotation of the inner hull.
// Sin and Cos of the angle interval are used multiple times, as
// is the Sin and Cos of the angle's midpoint.
struct RotTrig {

  RotTrig(Bigival angle_in,
          BigInt inv_epsilon_in) :
    angle(std::move(angle_in)),
    inv_epsilon(std::move(inv_epsilon_in)) {

    // Get high quality intervals since these are used many
    // times.
    cos_a = NiceCos(angle, inv_epsilon);
    sin_a = NiceSin(angle, inv_epsilon);

    // TODO: Midpoint.
  }

  Bigival angle;
  Bigival cos_a, sin_a;
  BigInt inv_epsilon;
};

// Compute a bounding ball for the patch on the unit sphere
// given by the azimuth and angle (this is the view position).
// The patch must be smaller than a hemisphere or you will
// get a degenerate (but correct) result.
static Ballival SphericalPatchBall(const ViewBoundsTrig &trig,
                                   const BigInt &inv_epsilon) {

  // We need the chosen center to be in the convex hull of the patch.
  // This is easy for small patches, but like elsewhere if the patch
  // is a whole hemisphere we'd need to start checking other points.
  // Just return a conservative but degenerate ball (full unit ball)
  // if the intervals are too wide.
  if (trig.azimuth.Width() > BigRat(1) || trig.angle.Width() > BigRat(1)) {
    return Ballival(BigVec3(BigRat(0), BigRat(0), BigRat(0)),
                    BigRat(1));
  }

  // Compute a good center.
  BigRat mid_azimuth = trig.azimuth.Midpoint();
  BigRat mid_angle = trig.angle.Midpoint();

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
  //
  // Informally, this point is on the unit sphere, and then we
  // are guaranteed that the ball contains the entire patch
  // (because it is not hemispherical).
  // This will also be true if we are reasonably close to the
  // sphere (which this will be) but there's a proof obligation
  // to revisit here.
  BigVec3 center = BigVec3(
      (mid_sina * mid_cosz).Midpoint(),
      (mid_sina * mid_sinz).Midpoint(),
      (mid_cosa).Midpoint());

  // Find a squared radius that will include all the corners.
  BigRat max_sqdist(0);

  // Compute corners for the patch. These are tight bounds
  // on the sine and cosine of each corner, ordered as lb, ub.
  std::array<std::pair<Bigival, Bigival>, 2> sin_cos_az;
  std::array<std::pair<Bigival, Bigival>, 2> sin_cos_an;

  sin_cos_az[0] =
    std::make_pair(Bigival::Sin(trig.azimuth.LB(), inv_epsilon),
                   Bigival::Cos(trig.azimuth.LB(), inv_epsilon));
  sin_cos_az[1] =
    std::make_pair(Bigival::Sin(trig.azimuth.UB(), inv_epsilon),
                   Bigival::Cos(trig.azimuth.UB(), inv_epsilon));

  sin_cos_an[0] =
    std::make_pair(Bigival::Sin(trig.angle.LB(), inv_epsilon),
                   Bigival::Cos(trig.angle.LB(), inv_epsilon));
  sin_cos_an[1] =
    std::make_pair(Bigival::Sin(trig.angle.UB(), inv_epsilon),
                   Bigival::Cos(trig.angle.UB(), inv_epsilon));

  // The corners of the patch are the furthest away from the
  // chosen center. The furthest of these will determine the
  // radius.
  for (const auto &[c_sinz, c_cosz] : sin_cos_az) {
    for (const auto &[c_sina, c_cosa] : sin_cos_an) {
      // The location of the corner.
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
  // Upper bound on the radius. We sometimes have this anyway,
  // in which case we can save the trouble of iteratively
  // approximating the square root. Note that this does not
  // need to be the eact sxquare root of radius_sq, but both
  // need to be upper bounds on the interval represented.
  std::optional<BigRat> radius;
  Discival(BigVec2 c, BigRat r_sq) : center(std::move(c)),
                                     radius_sq(std::move(r_sq)) {
    CHECK(BigRat::Sign(radius_sq) != -1) << radius_sq.ToString();
  }

  Discival(BigVec2 c, BigRat r_sq, BigRat r) :
    center(std::move(c)),
    radius_sq(std::move(r_sq)),
    radius(std::make_optional(std::move(r))) {
    CHECK(BigRat::Sign(radius_sq) != -1) << radius_sq.ToString();
    CHECK(BigRat::Sign(radius.value()) != -1);
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
  }

  // Upper bound on the radius. Prefer this one, as it computes
  // and saves the radius, and also avoids copying it.
  // Note that inv_epsilon might be ignored if we already have a
  // value for the square root. But it will always be a correct
  // upper bound.
  const BigRat &Radius(const BigInt &inv_epsilon) {
    if (!radius.has_value()) {
      radius =
        std::make_optional(BigRat::SqrtBounds(radius_sq, inv_epsilon).second);
    }
    return radius.value();
  }

  // As above, but for a const object. Copies and does not cache.
  BigRat ConstRadius(const BigInt &inv_epsilon) const {
    if (radius.has_value()) {
      return radius.value();
    } else {
      return BigRat::SqrtBounds(radius_sq, inv_epsilon).second;
    }
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
[[maybe_unused]]
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

// Like TransformPointTo2D(
//    FrameFromViewPos(ViewFromSpherical(azimuth, angle)), v)
// but producing a tighter AABB.
static Vec2ival TransformVec(const ViewBoundsTrig &trig,
                             const BigVec3 &v) {
  // x = dot(v, view_frame_x_axis)
  // view_frame_x_axis = (-sin(az), cos(az), 0)
  Bigival px = -v.x * trig.sin_az + v.y * trig.cos_az;

  // y = dot(v, view_frame_y_axis)
  // view_frame_y_axis = (-cos(an)cos(az), -cos(an)sin(az), sin(an))
  Bigival py = -trig.cos_an * (v.x * trig.cos_az + v.y * trig.sin_az) +
    v.z * trig.sin_an;

  return Vec2ival(std::move(px), std::move(py));
}

// Like Dot(ViewFromSpherical(azimuth, angle), v) but gives
// tighter bounds. As above, the angle intervals must both
// be less than 3 radians.
static Bigival BoundDotProductWithView(
    const ViewBoundsTrig &trig,
    const BigVec3 &v) {
  CHECK(trig.azimuth.Width() < BigRat(3));
  CHECK(trig.angle.Width() < BigRat(3));

  // viewpos = (sin(an)cos(az), sin(an)sin(az), cos(an))
  // dot(viewpos, v) = sin(an) * (v.x*cos(az) + v.y*sin(az)) + v.z*cos(an)

  return trig.sin_an * (v.x * trig.cos_az + v.y * trig.sin_az) +
    v.z * trig.cos_an;
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

// Produces 7-line string.
std::string VolumeString(const Volume &volume) {
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

// Note: Here we have the dependency problem. This one might be
// solvable with somewhat straightforward analysis. I think what we
// want to do is think of this as rotating an axis-aligned rectangle
// (the 2D bounds) and then getting the axis-aligned rectangle that
// contains that.
// This is also one of the very last things we do, so it's plausible
// that we could just do some geometric reasoning about the actual
// rotated rectangle at this point.
[[maybe_unused]]
static Vec2ival Rotate2D(const Vec2ival &v, const Bigival &angle,
                         const BigInt &inv_epsilon) {
  Bigival sin_a = angle.Sin(inv_epsilon);
  Bigival cos_a = angle.Cos(inv_epsilon);

  return Vec2ival(v.x * cos_a - v.y * sin_a,
                  v.x * sin_a + v.y * cos_a);
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
    // Mutable because we might calculate and cache radius.
    Discival *disc,
    const RotTrig &rot_trig,
    // A factor > 1 pushes the center away from the origin
    // to create a tighter inner bound. 1.0 is unbiased.
    const BigRat &bias,
    const BigInt &inv_epsilon_orig) {

  // Use a much smaller epsilon to assess the hypothesis.
  // BigInt inv_epsilon = inv_epsilon_orig * inv_epsilon_orig;
  const BigInt &inv_epsilon = inv_epsilon_orig;

  if (rot_trig.angle.Width() > BigRat(3)) {
    // We can't get a good disc with this method. Just return
    // something correct. A really simple choice is just a
    // disc centered at the origin, whose radius is as though
    // we sweep the input disc over the entire circle.
    counter_degenerate_disc++;
    BigRat center_dist = BigRat::SqrtBounds(dot(disc->center, disc->center),
                                            inv_epsilon).second;
    BigRat radius = disc->Radius(inv_epsilon);

    BigRat bounding_radius = center_dist + radius;
    BigRat radius_sq = bounding_radius * bounding_radius;
    return Discival(BigVec2(BigRat(0), BigRat(0)),
                    std::move(radius_sq),
                    std::move(bounding_radius));
  }


  // The center of the disc will be on the same vector as the center
  // of the arc, just further out (according to the bias parameter).
  // Using the exact center would be nice here (the distance to the
  // arc endpoints is equal on the perpendicular bisector) but we
  // can't compute it precisely since we have the transcendentals.
  // We'll just commit to a point decently close to the geometric center,
  // and then compute a radius that definitely includes the sweep
  // for the chosen point.

  // PERF precompute it!
  BigRat mid_angle = rot_trig.angle.Midpoint();
  Bigival sin_ma = Bigival::Sin(mid_angle, inv_epsilon);
  Bigival cos_ma = Bigival::Cos(mid_angle, inv_epsilon);
  Vec2ival arc_center_ival(
      disc->center.x * cos_ma - disc->center.y * sin_ma,
      disc->center.x * sin_ma + disc->center.y * cos_ma);
  BigVec2 arc_center = {arc_center_ival.x.Midpoint(),
                        arc_center_ival.y.Midpoint()};

  // Push this center away from the origin by the bias.
  BigVec2 bounding_center = arc_center * bias;

  // Radius for the bounding disc.
  // The radius must be large enough to contain the furthest point on
  // the swept shape, which will be on the circumference of one of the
  // endpoint discs. We use the triangle inequality:

  // Upper bound on the input disc's actual radius.
  const BigRat &in_r = disc->Radius(inv_epsilon);

  // The AABBs for the disc's center rotated to the angle's endpoints.
  // PERF: These should be very tight intervals, but we could probably
  // do better here with a routine that computes a disc for a rotated
  // point.
  // PERF: This is not the same as bounds on the sin and cos of
  // the angle interval, but we could still precompute it (and it
  // can share some computations).
  auto RotatePt = [&](const BigRat &angle, const BigVec2 &p) {
      Bigival sina = Bigival::Sin(angle, inv_epsilon);
      Bigival cosa = Bigival::Cos(angle, inv_epsilon);
      return Vec2ival(p.x * cosa - p.y * sina,
                      p.x * sina + p.y * cosa);
    };

  // Very tight AABBs bounding the centers of the rotated disc at
  // the angle lower bound and upper bound.
  Vec2ival center_lb = RotatePt(rot_trig.angle.LB(), disc->center);
  Vec2ival center_ub = RotatePt(rot_trig.angle.UB(), disc->center);

  // Now find the maximum squared distance to the two rotated endpoints.
  // These should be almost the same except for the small amount of error
  // from estimating Sin and Cos. But we need to get a result that is
  // correct, so we need to incorporate the error in the radius.
  // This requires picking the corner of the AABB that is furthest.

  BigRat max_arc_dist_sq(0);
  auto TryCorners = [&](const Vec2ival &c) {
      // Note pointers so that we avoid copying LB and UB to form the
      // initializer list.
      for (const BigRat *x : {&c.x.LB(), &c.x.UB()}) {
        BigRat dx = *x - bounding_center.x;
        BigRat dxx = dx * dx;
        for (const BigRat *y : {&c.y.LB(), &c.y.UB()}) {
          BigRat dy = *y - bounding_center.y;
          BigRat dyy = dy * dy;

          BigRat dist_sq = dxx + dyy;
          if (dist_sq > max_arc_dist_sq)
            max_arc_dist_sq = std::move(dist_sq);
        }
      }
    };

  TryCorners(center_lb);
  TryCorners(center_ub);

  BigRat center_r = BigRat::SqrtBounds(max_arc_dist_sq, inv_epsilon).second;
  // The radius is bounded by the sum of the distance to the arc and the
  // distance from the arc to the arc's circumference (input disc's radius),
  // because of the triangle inequality.
  BigRat bounding_radius = center_r + in_r;

  BigRat radius_sq = bounding_radius * bounding_radius;

  return Discival(std::move(bounding_center),
                  std::move(radius_sq),
                  std::move(bounding_radius));
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
    Discival *disc,
    const Bigival &tx,
    const Bigival &ty,
    const BigInt &inv_epsilon) {

  // Exact center for the bounding disc. We have our choice here, but
  // the midpoint is the best option and is easy to compute.
  BigVec2 bound_center(
      (disc->center.x + tx).Midpoint(),
      (disc->center.y + ty).Midpoint());

  // Now compute a radius that's sufficient to contain the entire
  // roundrect. Using the triangle inequality, the max distance to a
  // corner plus the original radius is an upper bound. All the
  // corners are the same distance from the center:
  BigRat half_w = tx.Width() / 2;
  BigRat half_h = ty.Width() / 2;
  BigRat corner_dist =
    BigRat::SqrtBounds(half_w * half_w + half_h * half_h, inv_epsilon).second;

  const BigRat &original_radius = disc->Radius(inv_epsilon);

  // And the original radius.
  BigRat bound_radius = corner_dist + original_radius;

  BigRat radius_sq = bound_radius * bound_radius;

  return Discival(std::move(bound_center),
                  std::move(radius_sq),
                  std::move(bound_radius));
}

struct Hypersolver {

  static std::string_view RejectionReasonString(RejectionReason r) {
    switch (r) {
    default:
    case REJECTION_UNKNOWN:
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
    case POLY_AREA:
      return "AREA";
    }
  }

  static std::string ColorRejection(const Rejection &rej) {
    switch (rej.reason) {
    default:
    case REJECTION_UNKNOWN:
      return ARED("MISSING?");
    case POINT_OUTSIDE1:
    case POINT_OUTSIDE2:
    case POINT_OUTSIDE3:
      return ARED("DEPRECATED");

    case OUTSIDE_OUTER_PATCH:
    case OUTSIDE_INNER_PATCH:
    case OUTSIDE_OUTER_PATCH_BALL:
    case OUTSIDE_INNER_PATCH_BALL:
      return std::format(ACYAN("{}"), RejectionReasonString(rej.reason));

    case POLY_AREA:
      return std::format(ACYAN("{}"), RejectionReasonString(rej.reason));

    case POINT_OUTSIDE4:
    case POINT_OUTSIDE5:
      if (!rej.edge_point.has_value()) return ARED("MISSING METADATA");
      return std::format(ACYAN("{}")
                         AGREY("(")
                         ARED("{}") AGREY(", ")
                         AGREEN("{}") AGREY(")"),
                         RejectionReasonString(rej.reason),
                         rej.edge_point.value().first,
                         rej.edge_point.value().second);
    }
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
    Rejection rejection;
    explicit Impossible(Rejection r) : rejection(r) {}
  };

  // Rotate the point v_in by rot and translate it by tx,ty.
  // Since v_in is an AABB and rot is an interval, the resulting
  // shape here is a rectangle swept along a circular arc. We
  // will call this the "swept shape."
  //
  // This always returns a single AABB, though we previously tried
  // producing a "bounding complex" of multiple AABBs that cover
  // the swept shape. That eventually became the disc approach, but
  // it might make sense to reconsider non-rectangular bounding
  // regions here again.
  Vec2ival GetBoundingAABB(const Vec2ival &v_in,
                           const RotTrig &rot_trig,
                           const BigInt &inv_epsilon,
                           const Bigival &tx, const Bigival &ty) {

    auto Translate = [&](Vec2ival &&v) {
        return Vec2ival(std::move(v.x) + tx, std::move(v.y) + ty);
      };

    // Bigival sin_a = angle.Sin(inv_epsilon);
    // Bigival cos_a = angle.Cos(inv_epsilon);
    Vec2ival loose_box(v_in.x * rot_trig.cos_a - v_in.y * rot_trig.sin_a,
                       v_in.x * rot_trig.sin_a + v_in.y * rot_trig.cos_a);

    return {Translate(std::move(loose_box))};
  }

  struct ProcessResult {
    std::variant<Split, Impossible> result;

    // Debug info / stats when requested.

    // The bounds for the inner points. We only have these if we
    // make it to a certain point in the test.
    std::vector<Vec2ival> inner;

    // AABBs for the outer hull points. We don't even use these
    // directly, but they can be useful for debugging.
    std::vector<Vec2ival> outer;

    // Disc bounds for the inner points.
    std::vector<Discival> discs;
  };

  // Process one volume, either proving it impossible or requesting
  // a split. If get_stats is true, it computes some additional data
  // for visualization or estimating AABB efficiency (more expensive).
  // This is where all the work happens. Thread safe and only takes
  // locks for really fast stuff (accumulating stats).
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

    // We'll use the sin and cos of the azimuth/angle bounds
    // many times, and the trig functions are expensive. Compute
    // those once up front.

    // TODO: Should have some principled derivation of epsilon here.
    // There's no correctness problem, but I just pulled
    // one one-millionth out of thin air and maybe it should be
    // much smaller (or larger?).
    // It does need to at least get smaller as the intervals get
    // smaller.
    BigInt inv_epsilon = min_width.Denominator() * 1024 * 1024;

    Timer trig_timer;
    ViewBoundsTrig outer_trig(outer_azimuth, outer_angle, inv_epsilon);
    // Note: this is computed eagerly for timing stats purposes, but
    // we kinda assume that the trig object is only meaningful when
    // the az/an intervals are small (not hemisphere). So for maximum
    // cleanliness, we should move this past the point where we have
    // checked. We don't use it until then
    ViewBoundsTrig inner_trig(inner_azimuth, inner_angle, inv_epsilon);
    {
      MutexLock ml(&mu);
      trig_time += trig_timer.Seconds();
    }

    /*
    status.Print("osin: {} (w: {}) ocos: {} (w: {})\n",
                 outer_trig.sin_cos_az[0].first.ToString(),
                 outer_trig.sin_cos_az[0].first.Width().ToDouble(),
                 outer_trig.sin_cos_az[0].second.ToString(),
                 outer_trig.sin_cos_az[0].second.Width().ToDouble());
    */

    // This is enough to test whether we're in the outer patch;
    // we'd like to exclude large regions ASAP (without e.g. forcing
    // splits on the inner parameters), so compute and test that
    // now.
    Bigival osina = outer_angle.Sin(inv_epsilon);
    Vec3ival oviewpos = Vec3ival(
        // PERF from ViewBoundsTrig
        osina * outer_azimuth.Cos(inv_epsilon),
        osina * outer_azimuth.Sin(inv_epsilon),
        outer_angle.Cos(inv_epsilon));

    if (!MightHaveCode(outer_code, outer_mask, oviewpos)) {
      return ProcessResult{
        .result = Impossible(Rejection(OUTSIDE_OUTER_PATCH))};
    }

    {
      Ballival oviewposball = SphericalPatchBall(outer_trig,
                                                 inv_epsilon);
      if (!MightHaveCodeWithBall(outer_code, outer_mask, oviewposball)) {
        return ProcessResult{
          .result = Impossible(Rejection(OUTSIDE_OUTER_PATCH_BALL))};
      }
    }

    // Generating the frames has a precondition that the z-axis not
    // contained in the view interval. Since we know that none of the
    // canonical patches contain the z axis, we just subdivide if it
    // might be included. Do this ASAP so that we don't have to keep
    // encountering this after splitting other params.
    if (oviewpos.x.ContainsOrApproachesZero() &&
        oviewpos.y.ContainsOrApproachesZero()) {
      return ProcessResult{
        .result = Split({OUTER_AZIMUTH, OUTER_ANGLE})};
    }

    // Now the same idea for the inner patch.

    {
      ParameterSet must_split;
      if (iz_width > MIN_ANGLE) must_split.Add(INNER_AZIMUTH);
      if (ia_width > MIN_ANGLE) must_split.Add(INNER_ANGLE);

      if (!must_split.Empty()) {
        return ProcessResult{.result = Split(must_split)};
      }
    }

    // PERF get from trig
    Bigival isina = inner_angle.Sin(inv_epsilon);
    Vec3ival iviewpos = Vec3ival(
        isina * inner_azimuth.Cos(inv_epsilon),
        isina * inner_azimuth.Sin(inv_epsilon),
        inner_angle.Cos(inv_epsilon));

    if (!MightHaveCode(inner_code, inner_mask, iviewpos)) {
      return {Impossible(Rejection(OUTSIDE_INNER_PATCH))};
    }

    {
      Ballival iviewposball = SphericalPatchBall(inner_trig,
                                                 inv_epsilon);
      if (!MightHaveCodeWithBall(inner_code, inner_mask, iviewposball)) {
        return ProcessResult{.result =
          Impossible(Rejection(OUTSIDE_INNER_PATCH_BALL))};
      }
    }

    // As above: Can't contain the z axis.
    if (iviewpos.x.ContainsOrApproachesZero() &&
        iviewpos.y.ContainsOrApproachesZero()) {
      return ProcessResult{.result = Split({INNER_AZIMUTH, INNER_ANGLE})};
    }


    // Heuristic: Now, our interval overlaps the patches. But further
    // subdivide unless it is entirely within the patch, or
    // is small. The idea behind this is that the problem might actually
    // have (spurious) solutions when we are viewing the hulls from
    // outside the patch, and if this is true then we would endlessly
    // subdivide trying to rule them out. So when we are on an edge,
    // insist that we at least have reasonably fine starting cell sizes.
    if (!VolumeInsidePatches(volume)) {
      // TODO: Tune this?
      // About one degree.
      // BigRat MIN_SMALL_ANGLE(3, 172);
      // About two degrees.
      BigRat MIN_SMALL_ANGLE(6, 172);

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
    // PERF not used!
    Timer frame_timer;
    Frame3ival outer_frame = FrameFromViewPos(oviewpos, inv_epsilon);
    Frame3ival inner_frame = FrameFromViewPos(iviewpos, inv_epsilon);
    double frame_time_here = frame_timer.Seconds();
    {
      MutexLock ml(&mu);
      frame_time += frame_time_here;
    }

    // Area test. If the inner shadow's area is definitely bigger than
    // the outer's, then it cannot fit (regardless of angle / translation).
    // This is a very cheap test.
    Timer area_timer;
    // Area of the shadow is just the dot product of the view with the
    // 3d area vector.
    Bigival outer_area2 = BoundDotProductWithView(outer_trig, outer_area_3d);
    Bigival inner_area2 = BoundDotProductWithView(inner_trig, inner_area_3d);

    const bool impossible_area =
      outer_area2.Less(inner_area2) == Bigival::MaybeBool::True;
    const double area_time_here = area_timer.Seconds();
    {
      MutexLock ml(&mu);
      area_time += area_time_here;
    }

    if (impossible_area) {
      ProcessResult res;
      res.result = Impossible(Rejection(POLY_AREA));
      return res;
    }


    // Compute the hulls. Because we're using interval arithmetic,
    // the specifics of the algebra can be quite important. First,
    // the outer hull, whose vertices we call va, vb, etc.

    // The raw rotated vertices of the hull; va.
    // We don't actually use these now. We only compute them for
    // debug data if that is enabled.
        std::vector<Vec2ival> outer_aabb;
    if (get_stats) {
      outer_aabb.reserve(outer_hull.size());
      for (int vidx : outer_hull) {
        const BigVec3 &pa = scube.vertices[vidx];
        outer_aabb.push_back(TransformVec(outer_trig, pa));
      }
    }

    // The hull edge vb-va, rotated by the outer frame.
    // We already precomputed the exact 3D vectors, so we
    // are just rotating and projecting those to 2D here.
    std::vector<Vec2ival> outer_edge;
    outer_edge.reserve(outer_hull.size());
    for (int idx = 0; idx < outer_hull.size(); idx++) {
      const BigVec3 &edge_3d = outer_edge3d[idx];
      outer_edge.push_back(TransformVec(outer_trig, edge_3d));
    }

    // The cross product va × vb.
    std::vector<Bigival> outer_cross_va_vb;
    outer_cross_va_vb.reserve(outer_hull.size());
    for (int idx = 0; idx < outer_hull.size(); idx++) {
      // Can derive this from the original 3D edge, and we've
      // precomputed its 3D cross product.
      const BigVec3 &edge_cross = outer_cx3d[idx];

      outer_cross_va_vb.push_back(
          BoundDotProductWithView(outer_trig, edge_cross));
    }

    std::vector<Vec2ival> inner;
    if (get_stats) {
      inner.reserve(inner_hull.size());
    }

    std::vector<Discival> discs;
    if (get_stats) {
      discs.reserve(inner_hull.size());
    }

    const BigRat BIAS = BigRat(3, 2);

    RotTrig rot_trig(inner_rot, inv_epsilon);

    double disc_time_here = 0.0;
    double disc_outside_time_here = 0.0;
    Timer loop_timer;
    // Compute the inner hull point-by-point, which we call v. We can
    // exit early if any of these points are definitely outside the
    // outer hull.
    std::optional<Rejection> proved = std::nullopt;
    for (int inner_hull_idx = 0;
         inner_hull_idx < inner_hull.size();
         inner_hull_idx++) {
      [[maybe_unused]]
      const BigVec3 &original_v =
        scube.vertices[inner_hull[inner_hull_idx]];
      // Vec2ival proj_v = TransformPointTo2D(inner_frame, original_v);

      Vec2ival proj_v = TransformVec(inner_trig, original_v);

      // Bounds on the inner point's location. This is an AABB. Note
      // that since it is axis-aligned, rotating (especially by e.g.
      // 45°) loses some information. This is especially pernicious
      // for these line-side tests, since it's easy to have a
      // situation where the corner of the AABB intersects an edge,
      // but none of the bounded points would have. The disc approach
      // is a good complement for this case, but we could consider
      // trying other non-rectangular representations here.
      Vec2ival v_aabb =
        GetBoundingAABB(proj_v, rot_trig, inv_epsilon, inner_x, inner_y);

      // TODO: Tune bias. We can even try more than one, or choose
      // randomly.
      //
      // TODO: Another option here would be to represent the sausage
      // itself (endpoints, original disc radius). If both end discs
      // are outside the edge, then we are likely in a rejection
      // scenario and we could do something like search for a bias
      // parameter.
      Timer disc_timer;
      Discival disc_in(proj_v);
      Discival rot_disc = RotateDiscInnerBias(
          &disc_in, rot_trig, BIAS, inv_epsilon);
      Discival disc = TranslateDisc(&rot_disc, inner_x, inner_y, inv_epsilon);
      disc_time_here = disc_timer.Seconds();

      if (get_stats) {
        inner.push_back(v_aabb);
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

        // There are multiple ways to test this. Even algebraically
        // equivalent ways can have different levels of over-conservativity
        // due to the dependency problem! It might make sense to test
        // in a few different ways, but the current approach does a lot
        // of precomptutation with exact rationals and so it dominated
        // the others that I tried in both performance and accuracy.
        //
        // The naive test would be to check the sign of a simple
        // cross product.
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

        // Now test the inner point AABB.
        Bigival cross_product4 =
          edge.x * v_aabb.y - edge.y * v_aabb.x + cross_va_vb;

        if (!cross_product4.MightBePositive()) {
          Rejection pt4;
          pt4.reason = POINT_OUTSIDE4;
          pt4.edge_point = std::make_optional(
              std::make_pair(start, inner_hull_idx));
          proved = {pt4};
          // We only need to finish all the points if we are getting
          // stats!
          if (!get_stats) break;
        }

        // Or is the disc outside?
        Timer disc_outside_timer;
        const bool is_disc_outside =
          IsDiscOutsideEdge(disc, edge, cross_va_vb);
        disc_outside_time_here += disc_outside_timer.Seconds();
        if (is_disc_outside) {
          Rejection pt5;
          pt5.reason = POINT_OUTSIDE5;
          pt5.edge_point = std::make_optional(
              std::make_pair(start, inner_hull_idx));
          proved = {pt5};

          if (!get_stats) break;
        }
      }

      if (proved.has_value() && !get_stats) break;
    }

    ProcessResult res;
    // Always include these if we have them.
    res.inner = std::move(inner);
    res.discs = std::move(discs);
    res.outer = std::move(outer_aabb);

    if (proved.has_value()) {
      res.result = Impossible(proved.value());
    } else {
      // Failed to rule out this cell. Perform any split.
      res.result = Split();
    }

    {
      MutexLock ml(&mu);
      loop_time += loop_timer.Seconds();
      disc_time += disc_time_here;
      disc_outside_time += disc_outside_time_here;
    }

    return res;
  }

  static int RandomParameterFromSet(ArcFour *rc, ParameterSet params) {
    CHECK(!params.Empty()) << "No dimensions to split on?";
    const int num = params.Size();
    int idx = RandTo(rc, num);
    return params[idx];
  }

  // Choose the parameter that has the largest width relative to
  // that dimension's maximum width.
  static int BestParameterFromSet(const Hypercube &cube,
                                  const Volume &volume, ParameterSet params) {
    CHECK(!params.Empty());
    if (params.Size() == 1) return params[0];

    auto InitialWidth = [&cube](int d) {
        return cube.bounds[d].Width();
      };

    // Only consider dimensions actually in the set. Initialize
    // with the first one:
    int best_d = params[0];
    BigRat best_w = volume[best_d].Width() / InitialWidth(best_d);

    // And then try the remainder (if in the set).
    for (int d = best_d + 1; d < NUM_DIMENSIONS; d++) {
      if (params.Contains(d)) {
        BigRat w = volume[d].Width() / InitialWidth(d);
        if (w > best_w) {
          best_d = d;
          best_w = std::move(w);
        }
      }
    }

    return best_d;
  }

  static std::array<double, NUM_DIMENSIONS>
  SampleFromVolume(ArcFour *rc, const Volume &volume) {
    std::array<double, NUM_DIMENSIONS> sample;
    for (int d = 0; d < NUM_DIMENSIONS; d++) {
      double w = volume[d].Width().ToDouble();
      // If the widths are too small to even be distinct doubles,
      // we probably should just avoid sampling?
      if (w == 0.0) {
        sample[d] = volume[d].LB().ToDouble();
      } else {
        sample[d] = volume[d].LB().ToDouble() +
          w * RandDouble(rc);
      }
    }
    return sample;
  }

  // Corner of the hypervolume indicated by bitmask.
  static std::array<double, NUM_DIMENSIONS> VolumeCorner(const Volume &volume,
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

  static vec3 ViewFromSpherical(double azimuth, double angle) {
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

  // Used for visualization and efficiency estimate.
  static constexpr int N_SAMPLES = 512;
  std::vector<Shadows> SampleShadows(ArcFour *rc,
                                     const Volume &volume,
                                     const ProcessResult &pr) const {

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
    shadows.reserve(N_SAMPLES);
    // PERF: Could compute the double-based intervals once
    for (int s = 0; s < N_SAMPLES; s++) {
      std::array<double, NUM_DIMENSIONS> sample;
      // Sample the extremities (corners) exhaustively,
      // then some random samples.
      const bool corner = s < (1 << NUM_DIMENSIONS);
      if (corner) {
        sample = VolumeCorner(volume, s);
      } else {
        sample = SampleFromVolume(rc, volume);
      }

      vec3 oview = ViewFromSpherical(
          sample[OUTER_AZIMUTH],
          sample[OUTER_ANGLE]);
      frame3 oviewpos = FrameFromViewPos(oview);
      const bool opatch = boundaries.GetCodeSloppy(oview) == outer_code;

      vec3 iview = ViewFromSpherical(
          sample[INNER_AZIMUTH],
          sample[INNER_ANGLE]);
      frame3 iviewpos = FrameFromViewPos(iview);
      const bool ipatch = boundaries.GetCodeSloppy(iview) == inner_code;

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
          .outer = std::move(outer_shadow),
          .inner = std::move(inner_shadow),
          .opatch = opatch,
          .ipatch = ipatch,
          .corner = corner});
    }

    return shadows;
  }


  // Get the average efficiency of intervals (this just looks at
  // the inner intervals right now) if possible. Stats must have been
  // computed and the result has to be Impossible (point_outside).
  std::optional<double> ComputeEfficiency(
      const Volume &volume,
      const ProcessResult &pr,
      const std::vector<Shadows> &shadows) const {
    if (pr.inner.empty()) return std::nullopt;

    double efficiency_numer = 0.0;
    int efficiency_denom = 0;
    for (int p = 0; p < pr.inner.size(); p++) {
      // We have an AABB to measure against.
      const Vec2ival &aabb = pr.inner[p];

      // Get all the sampled points for this vertex.
      std::vector<vec2> sampled_points;
      sampled_points.reserve(N_SAMPLES);
      for (const auto &s : shadows) {
        CHECK(p < s.inner.size());
        sampled_points.push_back(s.inner[p]);
      }

      if (SELF_CHECK) {
        for (const vec2 &sample_v : sampled_points) {
          // Technically the double could be just outside the interval
          // due to floating point inaccuracy.
          CHECK(aabb.x.Contains(BigRat::FromDouble(sample_v.x)) &&
                aabb.y.Contains(BigRat::FromDouble(sample_v.y)))
            << aabb.ToString() << "\nwhich is:\n"
            << "x: [" << aabb.x.LB().ToDouble() << ", " <<
            aabb.x.UB().ToDouble() << "]\n"
            << "y: [" << aabb.y.LB().ToDouble() << ", " <<
            aabb.y.UB().ToDouble() << "]\n"
            "Sample: " << VecString(sample_v);
        }
      }

      // This is an estimate of how much area the actual
      // shape takes up. (Some of the shapes are non-convex,
      // like if the rotation angle ranges from 0 to 2π then
      // you get a kind of donut. So to be perfectly clear,
      // effiency here is judged relative to the convex hull
      // of the shape.)
      std::vector<int> sample_hull = QuickHull(sampled_points);
      double hull_area = AreaOfHull(sampled_points, sample_hull);

      double efficiency = hull_area / aabb.Area().ToDouble();
      efficiency_numer += efficiency;
      efficiency_denom++;
    }

    return efficiency_denom ?
      std::make_optional(efficiency_numer / efficiency_denom) :
      std::nullopt;
  }

  void MakeSampleImage(ArcFour *rc,
                       const Volume &volume, const ProcessResult &pr,
                       std::string_view msg) const {
    std::string filename = std::format("{}/sample-{}-{}.png",
                                       inubdir,
                                       time(nullptr), msg);
    status.Print("Sample volume:\n{}\n",
                 VolumeString(volume));


    const int WIDTH = 1024, HEIGHT = 1024;
    ImageRGBA img(WIDTH, HEIGHT);
    img.Clear32(0x000000FF);

    std::vector<Shadows> shadows = SampleShadows(rc, volume, pr);

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

        img.BlendCircle32(sx, sy, sr, color);
      };

    for (const Vec2ival &v : pr.inner) {
      DrawAABB(v, 0x33FF3366);
    }

    for (const Vec2ival &v : pr.outer) {
      DrawAABB(v, 0xFF333366);
    }

    for (const Discival &disc : pr.discs) {
      DrawCircle(disc, 0xCCFF3366);
    }

    std::vector<std::string> vs =
      Util::SplitToLines(VolumeString(volume));
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
                      std::format("Result: Impossible! {}",
                                  ColorRejection(imp->rejection)));
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
                                        int angle, int azimuth) const {
    CHECK(angle >= 0 && angle < NUM_DIMENSIONS);
    CHECK(azimuth >= 0 && azimuth < NUM_DIMENSIONS);
    const Bigival &angle_ival = volume[angle];
    const Bigival &azimuth_ival = volume[azimuth];

    uint64_t all_code = 0;
    for (int b = 0; b < 0b11; b++) {
      double azimuth = (b & 0b10) ? azimuth_ival.LB().ToDouble() :
        azimuth_ival.UB().ToDouble();
      double angle = (b & 0b01) ? angle_ival.LB().ToDouble() :
        angle_ival.UB().ToDouble();

      vec3 view = ViewFromSpherical(azimuth, angle);
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
  // Only used for heuristics.
  bool VolumeInsidePatches(const Volume &volume) const {
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

  void MaybeStatus(const Volume &volume) {
    status_per.RunIf([&]() {
        MutexLock ml(&mu);
        std::string rr;
        for (const auto &[reason, count] : rejection_count) {
          AppendFormat(&rr, ACYAN("{}") ": {}  ",
                       RejectionReasonString(reason), count);
        }

        std::string splitcount;
        for (int d = 0; d < NUM_DIMENSIONS; d++) {
          AppendFormat(&splitcount, "{} ", times_split[d]);
        }

        double run_time = run_timer.Seconds();

        double time_each =
          run_time / counter_processed.Read();

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

        // TODO: Add (recent?) vol/sec

        status.Status(
            AWHITE("—————————————————————————————————————————") "\n"
            // "Put volume information here!\n"
            "Split count: {}\n"
            // Recent interval
            "{}\n"
            // Rejection reason histo
            "{}\n"
            // Volume-based progress
            "Full vol: {:.5f}  in vol: {:.5f}  out vol: {:.5f}\n"
            // Basic counts
            "{} processed, "
            "{} " AGREEN("✔") ", "
            "{} " ARED("⊹") ". "
            "{} " AORANGE("q") "\n"
            // Timing
            "{} ea. "
            ABLUE("{:.3f}") "% trig "
            ABLUE("{:.3f}") "% area "
            ABLUE("{:.3f}") "% f "
            ABLUE("{:.3f}") "% disc "
            ABLUE("{:.3f}") "% disco "
            ABLUE("{:.3f}") "% loop\n"
            // Quality stats
            AORANGE("⊗") "mid: {}  "
            AORANGE("⊗") "disc: {}  "
            "In: {}   AABB efficiency: {}\n"
            "{}\n", // bar
            splitcount,
            VolumeString(volume),
            rr,
            full_volume_d, in_volume_d, volume_outscope,
            counter_processed.Read(),
            counter_completed.Read(),
            counter_split.Read(),
            node_queue.size(),
            ANSI::Time(time_each),
            (trig_time * 100.0) / process_time,
            (area_time * 100.0) / process_time,
            (frame_time * 100.0) / process_time,
            (disc_time * 100.0) / process_time,
            (disc_outside_time * 100.0) / process_time,
            (loop_time * 100.0) / process_time,
            counter_bad_midpoint.Read(),
            counter_degenerate_disc.Read(),
            counter_inside.Read(),
            eff_str,
            progress);
      });
  }

  void Init() {
    // Initialize queue.
    {
      auto leaves = hypercube->GetLeaves(&volume_outscope, &volume_proved);

      for (auto &p : leaves)
        node_queue.emplace_back(std::move(p));

      volume_done = volume_outscope + volume_proved;
    }

    status.Print("Initialized. Remaining leaves: {}\n", node_queue.size());
  }

  void WorkThread(int thread_idx) {
    ArcFour rc(std::format("{}.{}", thread_idx, time(nullptr)));

    bool get_stats_next = false;

    for (;;) {
      Volume volume;
      std::shared_ptr<Hypercube::Node> node;

      {
        mu.lock();
        if (node_queue.empty()) {
          const int outstanding = num_in_progress;
          if (outstanding == 0) {
            // The queue is empty and won't get any more, so we are done.
            mu.unlock();
            status.Print("Thread " AGREEN("{}") " finished!", thread_idx);
            return;
          }

          // Otherwise, there's no work for us, but there might be
          // work in the future if an outstanding cell has to subdivide.
          // We are probably very nearly done, though, so throttle.
          mu.unlock();
          CHECK(outstanding > 0) << "Bug if the count goes negative!";

          status.Print("Thread " ARED("{}") " idle! "
                       AGREY("({} outstanding)"),
                       thread_idx, outstanding);
          std::this_thread::sleep_for(std::chrono::seconds(5));
          continue;
        } else {
          num_in_progress++;

          // Perhaps select at random?
          if (node_queue.size() > 8192) {
            std::tie(volume, node) = std::move(node_queue.back());
            node_queue.pop_back();
          } else {
            std::tie(volume, node) = std::move(node_queue.front());
            node_queue.pop_front();
          }
        }
        mu.unlock();
      }

      CHECK(node.get() != nullptr);

      MaybeStatus(volume);

      save_per.RunIf([&](){
          MutexLock ml(&mu);
          status.Print("Saving to {}...\n", filename);
          hypercube->ToDisk(filename);
        });

      // Periodically turn on stats gathering for this thread until we
      // get some data.
      get_stats_next = get_stats_next || (counter_processed.Read() % 64) == 0;

      Timer process_timer;
      ProcessResult res = ProcessOne(volume, get_stats_next);
      const double process_sec = process_timer.Seconds();
      counter_processed++;

      {
        MutexLock ml(&mu);
        process_time += process_sec;
      }

      if (get_stats_next && render_per.ShouldRun()) {
        MakeSampleImage(&rc, volume, res, "any");
      }

      if (!res.inner.empty()) {
        // Got data to compute stats.
        get_stats_next = false;
        if (std::optional<double> efficiency =
            ComputeEfficiency(volume, res,
                              SampleShadows(&rc, volume, res))) {
          MutexLock ml(&mu);
          efficiency_count++;
          efficiency_total += efficiency.value();
        }
      }

      if (VolumeInsidePatches(volume)) {
        counter_inside++;
        // Sometimes also sample.
        sample_per.RunIf([&]() {
            MakeSampleImage(&rc, volume, res, "inside");
          });
      }

      // Use the result to update the hypercube and queue.
      if (Impossible *imp = std::get_if<Impossible>(&res.result)) {
        (void)imp;

        bool maybe_save_image = false;

        const double vol = Hypervolume(volume).ToDouble();

        {
          MutexLock ml(&mu);
          // Then mark the node as a leaf that has been ruled out.
          Hypercube::Leaf *leaf = std::get_if<Hypercube::Leaf>(node.get());
          CHECK(leaf != nullptr) << "Bug: We only expand leaves!";
          leaf->completed = time(nullptr);
          leaf->rejection = imp->rejection;

          counter_completed++;

          volume_done += vol;
          switch (imp->rejection.reason) {
          case OUTSIDE_OUTER_PATCH:
          case OUTSIDE_INNER_PATCH:
          case OUTSIDE_OUTER_PATCH_BALL:
          case OUTSIDE_INNER_PATCH_BALL:
            volume_outscope += vol;
            break;
          default:
            volume_proved += vol;
            maybe_save_image = true;
            break;
          }

          /*
            status.Print(AGREEN("Success!") " Excluded cell (" ACYAN("{}") "). "
            " Now {}.\n",
            RejectionReasonString(imp->reason),
            counter_completed.Read());
          */
          rejection_count[imp->rejection.reason]++;
        }

        if (maybe_save_image) {
          sample_proved_per.RunIf([&]() {
              MakeSampleImage(&rc, volume, res, "proved");
            });
        }

      } else if (Split *split = std::get_if<Split>(&res.result)) {
        // Can't rule it out. So split. We use a random direction here (in
        // accordance with the split's mask) but we should consider being
        // systematic about it (e.g. split the longest dimension)?

        // int dim = RandomParameterFromSet(&rc, split->parameters);
        int dim = BestParameterFromSet(*hypercube, volume, split->parameters);
        CHECK(dim >= 0 && dim < NUM_DIMENSIONS);
        // status.Print("Split dim {}, which is {}", dim, ParameterName(dim));
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

        {
          MutexLock ml(&mu);
          times_split[dim]++;

          // Enqueue the leaves.
          node_queue.emplace_back(std::move(left), split_node.left);
          node_queue.emplace_back(std::move(right), split_node.right);

          *node = Hypercube::Node(std::move(split_node));
        }

        counter_split++;

      } else {
        LOG(FATAL) << "Bad processresult";
      }

      // Note that we have finished this work item, so it won't result
      // in the queue growing at this point.
      {
        MutexLock ml(&mu);
        num_in_progress--;
      }
    }
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
      const Ballival &ball) const {

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

  void Run() {

    Init();

    static constexpr int NUM_WORK_THREADS = 12;

    std::vector<std::thread> workers;

    {
      MutexLock ml(&mu);
      for (int i = 0; i < NUM_WORK_THREADS; i++) {
        workers.emplace_back(&Hypersolver::WorkThread, this, i);
      }
    }

    // wait on all threads to finish
    for (std::thread &t : workers) {
      t.join();
    }

    status.Print("All threads finished!");

    status.Print("Writing complete cube: {}", filename);
    hypercube->ToDisk(filename);
    status.Print("Success " AGREEN(":)") "\n");
  }

  Hypersolver() : scube(BigScube(SCUBE_DIGITS)),
                  boundaries(scube) {
    patch_info = LoadPatchInfo("scube-patchinfo.txt");

    ArcFour rc("hyper-init");
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

    const PatchInfo::CanonicalPatch &outer = canonical[1].second;
    const PatchInfo::CanonicalPatch &inner = canonical[0].second;

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
    CheckHullRepresentation(&rc, outer_code, outer_mask, outer_hull);
    CheckHullRepresentation(&rc, inner_code, inner_mask, inner_hull);

    // Precomputation for areas of shadows.
    outer_area_3d = GetArea3D(outer_hull, scube, outer.example);
    inner_area_3d = GetArea3D(inner_hull, scube, inner.example);

    outer_cx3d.reserve(outer_hull.size());
    outer_edge3d.reserve(outer_hull.size());
    for (int n = 0; n < outer_hull.size(); n++) {
      const BigVec3 &va = scube.vertices[outer_hull[n]];
      const BigVec3 &vb =
        scube.vertices[outer_hull[(n + 1) % outer_hull.size()]];

      outer_cx3d.push_back(cross(va, vb));
      outer_edge3d.push_back(vb - va);
    }

    inubdir = std::format("inubs-{}-{}", outer_code, inner_code);
    (void)Util::MakeDir(inubdir);

    filename = std::format("hc-{}-{}.cube", outer_code, inner_code);
    if (Util::ExistsFile(filename)) {
      status.Print("Continuing from {}", filename);
      hypercube->FromDisk(filename);
    }

    full_volume = Hypervolume(hypercube->bounds);
    full_volume_d = full_volume.ToDouble();
  }

  void CheckHullRepresentation(ArcFour *rc, uint64_t code, uint64_t mask,
                               const std::vector<int> &hull) const {
    vec3 view = GetVec3InPatch(rc, boundaries, code, mask);
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

  // For each triangle in the triangle fan (using the vertices at
  // the hull points with the origin), compute the 3d cross product.
  // Return the sum.
  static BigVec3 GetArea3D(const std::vector<int> &hull_indices,
                           const BigPoly &poly,
                           const BigVec3 &example_view) {
    CHECK(hull_indices.size() >= 3);

    BigVec3 sum_3d(BigRat(0), BigRat(0), BigRat(0));
    for (size_t i = 0; i < hull_indices.size(); i++) {
      const BigVec3 &pa = poly.vertices[hull_indices[i]];
      const BigVec3 &pb =
        poly.vertices[hull_indices[(i + 1) % hull_indices.size()]];

      sum_3d = std::move(sum_3d) + cross(pa, pb);
    }

    // Check that areas are positive, as expected.
    CHECK(BigRat::Sign(dot(sum_3d, example_view)) > 0);
    return sum_3d;
  }

  // These members are safe to execute from multiple threads.
  // They should not be modified after initialization.
  Polyhedron small_scube;
  BigPoly scube;
  Boundaries boundaries;
  PatchInfo patch_info;
  std::string filename, inubdir;
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

  // This is the sum of the 3d cross products for the triangle fan
  // that makes up the outer (resp. inner) hull, using the origin
  // as the shared point. Exact. The dot product of a view vector
  // with this vector gives the area of the shadow!
  BigVec3 outer_area_3d;
  BigVec3 inner_area_3d;

  BigRat full_volume;
  double full_volume_d = 0.0;

  // Members below protected by the mutex.
  std::mutex mu;
  std::unique_ptr<Hypercube> hypercube;

  // Work queue. (Could actually use work queue here!)
  std::deque<std::pair<Volume, std::shared_ptr<Hypercube::Node>>> node_queue;
  // Need to keep track of the number that are currently being
  // processed by threads, since processing a node may or may
  // not insert new nodes into the queue. The node queue is only
  // truly "empty" when this is zero.
  int num_in_progress = 0;

  Timer run_timer;
  Periodically status_per = Periodically(1);
  Periodically save_per = Periodically(10 * 60);
  Periodically sample_per = Periodically(60 * 14.9);
  Periodically sample_proved_per = Periodically(60 * 9.1);
  Periodically render_per = Periodically(60 * 15.1);

  std::unordered_map<RejectionReason, int64_t> rejection_count;
  int64_t times_split[NUM_DIMENSIONS] = {};

  // Stats counters.
  double efficiency_total = 0.0;
  int64_t efficiency_count = 0;

  double trig_time = 0.0, loop_time = 0.0, process_time = 0.0;
  double disc_time = 0.0, disc_outside_time = 0.0;
  double area_time = 0.0;
  double frame_time = 0.0;

  // The hypervolume now done (this includes regions that we
  // determined are out of scope). Compare against the full volume.
  double volume_done = 0.0;
  // The volume that is excluded for being out of scope. Compare
  // against the full volume.
  double volume_outscope = 0.0;
  // The hypervolume where we definitively ruled out a solution.
  // Can compare this against (full - volume_outscope).
  double volume_proved = 0.0;
};

int main(int argc, char **argv) {
  ANSI::Init();

  Hypersolver solver;
  solver.Run();

  return 0;
}
