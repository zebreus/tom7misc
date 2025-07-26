
#include <algorithm>
#include <bit>
#include <cstdio>
#include <ctime>
#include <format>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "ansi.h"
#include "arcfour.h"
#include "atomic-util.h"
#include "base/logging.h"
#include "big-interval.h"
#include "big-polyhedra.h"
#include "bignum/big-overloads.h"
#include "bignum/big.h"
#include "patches.h"
#include "periodically.h"
#include "randutil.h"
#include "status-bar.h"
#include "threadutil.h"

DECLARE_COUNTERS(counter_processed, counter_completed, counter_split);

static constexpr int SCUBE_DIGITS = 24;

StatusBar status = StatusBar(8);

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

inline constexpr int OUTER_AZIMUTH = 0;
inline constexpr int OUTER_ANGLE = 1;
inline constexpr int INNER_AZIMUTH = 2;
inline constexpr int INNER_ANGLE = 3;
inline constexpr int INNER_ROT = 4;
inline constexpr int INNER_X = 5;
inline constexpr int INNER_Y = 6;

inline constexpr int NUM_DIMENSIONS = 7;

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

using Volume = std::vector<Bigival>;

// Hypercube using big rationals.
struct Hypercube {

  Hypercube(const Volume &bounds) :
    bounds(std::move(bounds)) {
    root.reset(new Node(Leaf{.completed = 0}));
  }

  // TODO: Serialize, deserialize

  struct Split;
  struct Leaf;
  using Node = std::variant<Split, Leaf>;

  struct Leaf {
    // If 0, no info yet. Otherwise, the timestamp when it was completed.
    // Solved means we've determined that this cell cannot contain
    // a solution.
    int64_t completed = 0;
  };

  struct Split {
    // Which axis?
    int axis = 0;
    // left side is <, right side is >=.
    BigRat split;
    std::unique_ptr<Node> left, right;
  };

  static std::pair<Volume, Volume>
  SplitVolume(const Volume &volume, int axis, const BigRat &split) {
    Volume left = volume;
    Volume right = volume;
    left[axis] = Bigival(left[axis].LB(), split, left[axis].IncludesLB(), false);
    right[axis] = Bigival(split, right[axis].UB(), true, right[axis].IncludesUB());
    return std::make_pair(left, right);
  }

  std::vector<std::pair<Volume, std::unique_ptr<Hypercube::Node> *>> GetLeaves() {
    std::vector<std::pair<Volume, std::unique_ptr<Hypercube::Node> *>> leaves;

    std::vector<std::pair<Volume, std::unique_ptr<Hypercube::Node> *>> stack = {
      {bounds, &root}
    };

    while (!stack.empty()) {
      Volume volume;
      std::unique_ptr<Hypercube::Node> *node;
      std::tie(volume, node) = std::move(stack.back());
      stack.pop_back();

      if (Leaf *leaf = std::get_if<Leaf>(node->get())) {
        if (leaf->completed == 0) {
          leaves.emplace_back(std::move(volume), node);
        }
      } else {
        Split *split = std::get_if<Split>(node->get());
        CHECK(split != nullptr) << "Must be leaf or split.";

        std::pair<Volume, Volume> vols = SplitVolume(volume,
                                                     split->axis, split->split);

        stack.emplace_back(vols.first, &split->left);
        stack.emplace_back(vols.second, &split->right);
      }
    }

    return leaves;
  }

  Volume bounds;
  std::unique_ptr<Node> root;
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
// zero. Can save ourselves some pointless addition.
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
struct Vec2ival {
  Bigival x, y;
  Vec2ival(Bigival xx, Bigival yy) :
    x(std::move(xx)), y(std::move(yy)) {}
  Vec2ival() : x(0), y(0) {}
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

// This can certainly work on Vec3ival, but our input data at this point
// is a single point and this is in inner loops.
static Vec2ival TransformPointTo2D(const Frame3ival &frame,
                                   const BigVec3 &v) {
  // PERF don't even compute z component!
  Vec3ival v3 = frame.x * v.x + frame.y * v.y + frame.z * v.z + frame.o;
  return Vec2ival(std::move(v3.x), std::move(v3.y));
}


// view pos must be "normal"
// This also requires that the z-axis is not included in the view interval.
Frame3ival FrameFromViewPos(const Vec3ival &view, const BigInt &inv_epsilon) {
  const Vec3ival &frame_z = view;

  status.Print("Frame for view: {}", view.ToString());

  Vec3ival up_z = {BigRat(0), BigRat(0), BigRat(1)};
  Vec3ival frame_x = Normalize(Cross(up_z, frame_z), inv_epsilon);
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

std::string VolumeString(const Volume &volume, bool multiline = false) {
  auto DimString = [](const Bigival &bi) {
      double lb = bi.LB().ToDouble();
      double ub = bi.UB().ToDouble();
      // TODO: Use dynamic precision here.
      return std::format(AGREY("[") "{:.8g}" AGREY(",") "{:.8g}" AGREY("]"),
                         lb, ub);
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

struct Hypersolver {
  enum RejectionReason {
    UNKNOWN = 0,
    OUTSIDE_OUTER_PATCH = 1,
    OUTSIDE_INNER_PATCH = 2,
    POINT_OUTSIDE1 = 3,
    POINT_OUTSIDE2 = 4,
    POINT_OUTSIDE3 = 5,
  };

  std::string_view RejectionReasonString(RejectionReason r) {
    switch (r) {
    default:
    case UNKNOWN:
      return ARED("MISSING?");
    case OUTSIDE_OUTER_PATCH:
      return "OUTSIDE_OUTER_PATCH";
    case OUTSIDE_INNER_PATCH:
      return "OUTSIDE_INNER_PATCH";
    case POINT_OUTSIDE1:
      return "POINT_OUTSIDE1";
    case POINT_OUTSIDE2:
      return "POINT_OUTSIDE2";
    case POINT_OUTSIDE3:
      return "POINT_OUTSIDE3";
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

  using ProcessResult = std::variant<Split, Impossible>;

  ProcessResult ProcessOne(const Volume &volume,
                           std::unique_ptr<Hypercube::Node> *node) {
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
    ParameterSet must_split;
    if (oz_width > MIN_ANGLE) must_split.Add(OUTER_AZIMUTH);
    if (oa_width > MIN_ANGLE) must_split.Add(OUTER_ANGLE);

    // Actually should first check if we are in outer patch?

    BigRat iz_width = inner_azimuth.Width();
    BigRat ia_width = inner_angle.Width();
    if (iz_width > MIN_ANGLE) must_split.Add(INNER_AZIMUTH);
    if (ia_width > MIN_ANGLE) must_split.Add(INNER_ANGLE);

    if (!must_split.Empty()) {
      return {Split(must_split)};
    }

    const BigRat min_width =
      BigRat::Min(
          BigRat::Min(
              BigRat::Min(oz_width, oa_width),
              BigRat::Min(inner_x.Width(),
                          inner_y.Width())),
          BigRat::Min(
              inner_rot.Width(),
              BigRat::Min(iz_width, ia_width)));

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
      return {Impossible(OUTSIDE_OUTER_PATCH)};
    }

    // TODO: if it's not the case that we're entirely within the outer
    // patch, prioritize splitting outer angle/azimuth, unless the
    // cells are tiny. We could maybe get into a situation where we
    // are unable to make progress because we are not actually in the
    // patch, and the hull is no longer even close to an attainable
    // shape (there may actually be true solutions out there!).

    Bigival isina = inner_angle.Sin(inv_epsilon);
    Vec3ival iviewpos = Vec3ival(
        isina * inner_azimuth.Cos(inv_epsilon),
        isina * inner_azimuth.Sin(inv_epsilon),
        inner_angle.Cos(inv_epsilon));

    if (!MightHaveCode(inner_code, inner_mask, iviewpos)) {
      return {Impossible(OUTSIDE_INNER_PATCH)};
    }

    // Get outer and inner frame.
    // TODO: This has a precondition that the z-axis not contained in
    // the view interval. Since we know that none of the canonical
    // patches contain the z axis, we should just subdivide if that's
    // the case. But we probably do need to detect it, in case these
    // intervals are too conservative.
    if (oviewpos.x.ContainsZero() && oviewpos.y.ContainsZero()) {
      return {Split({OUTER_AZIMUTH, OUTER_ANGLE})};
    }

    if (iviewpos.x.ContainsZero() && iviewpos.y.ContainsZero()) {
      return {Split({INNER_AZIMUTH, INNER_ANGLE})};
    }

    Frame3ival outer_frame = FrameFromViewPos(oviewpos, inv_epsilon);
    Frame3ival inner_frame = FrameFromViewPos(iviewpos, inv_epsilon);

    // Compute the initial hulls. This is the final outer hull, but
    // the inner hull still needs to be rotated and translated.
    std::vector<Vec2ival> outer_shadow;
    outer_shadow.reserve(outer_hull.size());
    // TODO: Probably much better to get the edge (pb-pa) first and then
    // rotate that!
    for (int idx : outer_hull) {
      const BigVec3 &original_v = scube.vertices[idx];
      outer_shadow.push_back(
          TransformPointTo2D(outer_frame, original_v));
    }

    // TODO: If the outer shadow has edges where the endpoint
    // intervals are not disjoint, then we might just want to bisect.
    // I think in this case we couldn't say anything useful about the
    // cross product (the edge could be in any direction) and it might
    // even lead to degeneracy. (This may be too conservative with the
    // new calculation based on the original 3D cross product, though.)

    // Compute the inner hull point-by-point. We can exit early
    // if any of these points are definitely outside the inner hull.
    for (int idx : inner_hull) {
      const BigVec3 &original_v = scube.vertices[idx];
      Vec2ival proj_v = TransformPointTo2D(inner_frame, original_v);
      // Also rotate and translate it.
      Vec2ival rot_v = Rotate2D(proj_v, inner_rot, inv_epsilon);
      Vec2ival v(rot_v.x + inner_x, rot_v.y + inner_y);

      // Now, we reject this cell if the point is definitely outside
      // the outer hull. Since the outer hull is a convex hull
      // containing the origin, in clockwise winding order, we can
      // just do this as a series of line-side tests.
      for (int start = 0; start < outer_shadow.size(); start++) {
        int end = (start + 1) % outer_shadow.size();
        const Vec2ival &va = outer_shadow[start];
        const Vec2ival &vb = outer_shadow[end];

        // Do line-side test. Specifically, we can assume the origin
        // is clockwise from the edge va->vb. Then we want to ask if
        // point v's interval is definitely completely on the other
        // side of the edge.

        // There are multiple ways to test this, and they could have
        // different types of over-conservativity (from dependency problem)
        // in different situations. So we perform multiple tests to
        // try to reject the cell.

        // Note dependency problem: va.x and va.y both appear twice.
        // vb and va depend on one another because they are the result
        // of a 2D rotation (and of course they both depend on the
        // outer orientation).
        Bigival cross_product1 =
          (vb.x - va.x) * (v.y - va.y) - (vb.y - va.y) * (v.x - va.x);

        // The cross product would have to be negative in order
        // to be strictly inside, so if this is not possible, then we
        // know this cell is impossible.
        if (!cross_product1.MightBeNegative()) {
          return {Impossible(POINT_OUTSIDE1)};
        }

        Bigival cross_vb_v_plus_cross_v_va =
          Cross(vb, v) + Cross(v, va);

        // This is arranged so that only the term Cross(va, vb) has
        // the dependency problem. The edge and point are independent
        // because they only depend on the outer and inner parameters,
        // respectively.
        Bigival cross_product2 =
          Cross(va, vb) + cross_vb_v_plus_cross_v_va;

        if (!cross_product2.MightBeNegative()) {
          return {Impossible(POINT_OUTSIDE2)};
        }

        // TODO: Try the trick of deriving terms from the exact
        // 3D cross product.

      }
    }

    // Failed to rule out this cell. Perform any split.
    return {Split()};
  }

  ArcFour rc;
  std::mutex mu;

  int RandomDimensionFromSplitMask(ParameterSet params) {
    CHECK(!params.Empty()) << "No dimensions to split on?";
    const int num = params.Size();
    MutexLock ml(&mu);
    int idx = RandTo(&rc, num);
    return params[idx];
  }

  void Expand() {
    // Get all the unsolved leaves.
    std::vector<std::pair<Volume, std::unique_ptr<Hypercube::Node> *>> leaves =
      hypercube->GetLeaves();

    printf("Remaining leaves: %zu\n", leaves.size());

    Periodically status_per(1);

    while (!leaves.empty()) {
      Volume volume;
      std::unique_ptr<Hypercube::Node> *node;
      std::tie(volume, node) = leaves.back();
      leaves.pop_back();

      status_per.RunIf([&]() {
          status.Status(
              "{}\n"
              "{} processed, {} completed, {} split. {} queued",
              VolumeString(volume, true),
              counter_processed.Read(),
              counter_completed.Read(),
              counter_split.Read(),
              leaves.size());
        });

      ProcessResult res = ProcessOne(volume, node);
      counter_processed++;

      if (Impossible *imp = std::get_if<Impossible>(&res)) {
        (void)imp;

        // Lazily create leaf if needed.
        if (node->get() == nullptr) {
          node->reset(new Hypercube::Node(Hypercube::Leaf()));
        }

        // Then mark the node as a leaf that has been ruled out.
        Hypercube::Leaf *leaf = std::get_if<Hypercube::Leaf>(node->get());
        CHECK(leaf != nullptr) << "Bug: We only expand leaves!";
        leaf->completed = time(nullptr);

        counter_completed++;

        status.Print(AGREEN("Success!") " Excluded cell (" ACYAN("{}") "). "
                     " Now {}.\n",
                     RejectionReasonString(imp->reason),
                     counter_completed.Read());


      } else if (Split *split = std::get_if<Split>(&res)) {
        // Can't rule it out. So split. We use a random direction here (in
        // accordance with the split's mask) but we should consider being
        // systematic about it (e.g. split the longest dimension)?

        int dim = RandomDimensionFromSplitMask(split->parameters);
        CHECK(dim >= 0 && dim < NUM_DIMENSIONS);
        Hypercube::Split split_node;
        split_node.axis = dim;

        const Bigival &oldival = volume[dim];
        Volume left = volume, right = volume;
        BigRat mid = (oldival.LB() + oldival.UB()) / 2;

        // TODO: Choose a split point that's close to half way, but prefer
        // simple fractions! The endpoints form the basis of the intervals
        // that we calculate with, so higher quality fractions are much
        // preferable (and we don't really care about where we split as
        // long as we are getting logarithmic size reduction).

        // left side is <, right side is >=.
        left[dim] = Bigival(oldival.LB(), mid, oldival.IncludesLB(), false);
        right[dim] = Bigival(mid, oldival.UB(), true, oldival.IncludesUB());
        split_node.split = std::move(mid);
        // Leave left and right child as nullptr; they get filled in
        // when we explore them.

        // But enqueue these.
        leaves.emplace_back(std::move(left), &split_node.left);
        leaves.emplace_back(std::move(right), &split_node.right);

        node->reset(new Hypercube::Node(std::move(split_node)));

        counter_split++;

      } else {
        LOG(FATAL) << "Bad processresult";
      }
    }
  }

  // PERF: Might actually make sense to do all of the plane-side
  // tests; it's more work, but when we can exclude a point then
  // it saves us work across all dimensions!
  bool MightHaveCode(
      uint64_t code, uint64_t mask,
      const Vec3ival &v) const {
    for (int i = 0; i < boundaries.big_planes.size(); i++) {
      uint64_t pos = uint64_t{1} << i;
      if (pos & mask) {
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


  Hypersolver() : rc("hyper"), scube(BigScube(SCUBE_DIGITS)),
    boundaries(scube) {
    patch_info = LoadPatchInfo("scube-patchinfo.txt");
    // We don't want every volume's endpoints to involve some subdivision
    // of an extremely accurate pi, and we don't need them to; we just
    // need the starting interval to cover [0, π]. So we use a simple
    // rational upper bound to π.
    // Slightly larger than π. Accurate to 16 digits.
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

    // TODO: Verify that the hull contains the origin.

    outer_code = outer.code;
    inner_code = inner.code;
    outer_mask = outer.mask;
    inner_mask = inner.mask;

    outer_hull = outer.hull;
    inner_hull = inner.hull;

    outer_cx.reserve(outer_hull.size());
    for (int n = 0; n < outer_hull.size(); n++) {
      const BigVec3 &va = scube.vertices[outer_hull[n]];
      const BigVec3 &vb = scube.vertices[outer_hull[(n + 1) % outer_hull.size()]];

      outer_cx.push_back(cross(va, vb));
    }
  }

  BigPoly scube;
  Boundaries boundaries;
  PatchInfo patch_info;
  std::unique_ptr<Hypercube> hypercube;
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
  std::vector<BigVec3> outer_cx;
};

int main(int argc, char **argv) {
  ANSI::Init();

  Hypersolver solver;
  solver.Expand();

  return 0;
}
