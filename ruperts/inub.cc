
#include <algorithm>
#include <array>
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
#include "auto-histo.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "big-interval.h"
#include "big-polyhedra.h"
#include "bignum/big-overloads.h"
#include "bignum/big.h"
#include "bounds.h"
#include "hypercube.h"
#include "image.h"
#include "intervals.h"
#include "lastn-buffer.h"
#include "nice.h"
#include "patches.h"
#include "periodically.h"
#include "polyhedra.h"
#include "randutil.h"
#include "small-int-set.h"
#include "status-bar.h"
#include "threadutil.h"
#include "timer.h"
#include "util.h"
#include "yocto_matht.h"

// TODO:
//  - try initial_disc method again
//  - air gapped work queue
//  - stats for rational size
//  - more timing stats
//  - split out core to library for verifier
//  - note plane for out patch
//  - try NiceSin/NiceCos for outer loops
//  - try multiple bias parameters, or optimize
//  - max chord method

DECLARE_COUNTERS(counter_processed, counter_completed, counter_split,
                 counter_bad_midpoint, counter_inside,
                 counter_no_edges, counter_sausage_negative);

static constexpr bool SELF_CHECK = false;

// Don't change this!
static constexpr int SCUBE_DIGITS = 24;

static constexpr int NUM_WORK_THREADS = 14;

using vec2 = yocto::vec<double, 2>;

static StatusBar status = StatusBar(16);

using Volume = Hypercube::Volume;
using Pt4Data = Hypercube::Pt4Data;
using Pt5Data = Hypercube::Pt5Data;
using Rejection = Hypercube::Rejection;

// Adaptation of Jason's idea.
// Take a pair of patches, one for outer and one for inner.
// We know what the hulls are; each a function of the view position.

// We'd get the fewest parameters as:
//  - outer view position (azimuth, angle)
//  - inner view position (azimuth, angle)
//  - inner rotation (theta)
//  - inner translation (x, y)

// This is 7 parameters.

// We have a 7-hypercube for those parameters (hypercube.h).
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

// A set of those 7 parameters. Value semantics.
using ParameterSet = SmallIntSet<NUM_DIMENSIONS>;

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

// Tom's Sausage Roll.
// A swept disc creates a sausage shape. We try rejecting these
// as outside by creating another bounding disc that includes
// the entire sausage. But we only try this if both endpoint
// discs are outside the edge, since computing the disc is
// somewhat expensive. This also allows us to try a few different
// bias parameters.
struct Sausage {
  Discival *disc = nullptr;

  // The untranslated start and end-point disc centers. They have the
  // same radius as the original disc.
  Vec2ival center_lb, center_ub;

  // Both discs are translated by the same amount, so we keep this
  // separate to avoid dependency issues if we use the discs together.
  Vec2ival translation;

  // center + translation. Used when we are testing the discs
  // independently.
  Vec2ival tcenter_lb, tcenter_ub;
};

// Experimental; incomplete.
std::optional<Sausage> GetSausage(
    Discival *disc,
    const RotTrig &rot_trig,
    Vec2ival translation,
    const BigInt &inv_epsilon) {

  if (rot_trig.angle.Width() > BigRat(3)) {
    return std::nullopt;
  }

  // The AABBs for the disc's center rotated to the angle's endpoints.
  // PERF: These should be very tight intervals, but we could probably
  // do better here with a routine that computes a disc for a rotated
  // point. It'd also make TryCorners test cheaper, since there is
  // just one radius.
  auto RotatePt = [&](const SinCos &endpoint, const BigVec2 &p) {
      return Vec2ival(p.x * endpoint.cosine - p.y * endpoint.sine,
                      p.x * endpoint.sine + p.y * endpoint.cosine);
    };

  Sausage sausage;
  sausage.disc = disc;
  // Very tight AABBs bounding the centers of the rotated disc at
  // the angle lower bound and upper bound.
  sausage.center_lb = RotatePt(rot_trig.lower, disc->center);
  sausage.center_ub = RotatePt(rot_trig.upper, disc->center);
  sausage.tcenter_lb = sausage.center_lb + translation;
  sausage.tcenter_ub = sausage.center_ub + translation;
  sausage.translation = std::move(translation);

  return std::make_optional(std::move(sausage));
}

// Experimental; incomplete.
// Make a bounding disc from a sausage, given the bias.
static Discival SausageDisc(
    const Sausage &sausage,
    const RotTrig &rot_trig,
    // A factor > 1 pushes the center away from the origin
    // to create a tighter inner bound. 1.0 is unbiased.
    const BigRat &bias,
    const BigInt &inv_epsilon) {

  CHECK(rot_trig.angle.Width() <= BigRat(3)) << "We could return "
    "a degenerate disc here, but you should only call this with "
    "a proper sausage!";

  // The center of the disc will be on the same vector as the center
  // of the arc, just further out (according to the bias parameter).
  // Using the exact center would be nice here (the distance to the
  // arc endpoints is equal on the perpendicular bisector) but we
  // can't compute it precisely since we have the transcendentals.
  // We'll just commit to a point decently close to the geometric center,
  // and then compute a radius that definitely includes the sweep
  // for the chosen point.

  // TODO: Could be cached in sausage, making multiple bias tests
  // cheaper.
  // Use precomputed midpoint.
  Vec2ival arc_center_ival(
      sausage.disc->center.x * rot_trig.mid.cosine -
      sausage.disc->center.y * rot_trig.mid.sine,
      sausage.disc->center.x * rot_trig.mid.sine +
      sausage.disc->center.y * rot_trig.mid.cosine);
  BigVec2 arc_center = {arc_center_ival.x.Midpoint(),
                        arc_center_ival.y.Midpoint()};

  // Push this center away from the origin by the bias.
  BigVec2 bounding_center = arc_center * bias;

  // Radius for the bounding disc.
  // The radius must be large enough to contain the furthest point on
  // the swept shape, which will be on the circumference of one of the
  // endpoint discs. We use the triangle inequality: The distance
  // from the bounding center to the most distant endpoint's most
  // distant corner, plus the radius of the swept disc.

  // First find the maximum squared distance to the two rotated
  // endpoints (centers). These should be almost the same except for
  // the small amount of error from estimating Sin and Cos. But we
  // need to get a result that is correct, so we need to incorporate
  // the error in the radius. This requires picking the corner of the
  // AABB that is furthest.

  // PERF: A cheaper approximation here would be to take the distance to
  // the center of the disc center's AABB, and then use manhattan distance
  // to a corner (they are all the same distance). This is an upper
  // bound, but pretty tight because the center AABBs only have
  // uncertainty from approximating the trig functions. Aside from
  // skipping the loop, we would have a radius (bound) instead of
  // a squared radius, which may save a sqrt.
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

  TryCorners(sausage.center_lb);
  TryCorners(sausage.center_ub);

  // Use the triangle inequality to compute a good radius for the
  // disc. We use the sum of the radius from the center to the
  // most distant corner (c) plus the input disc's radius (r).
  // We need a squared radius, which is
  //   (c + r)^2 = c^2 + r^2 + 2cr
  // We already have c^2 and r^2, and cr = sqrt(cr * cr) = sqrt(c^2 * r^2).

  // We have a few different ways to compute an upper bound here.
  enum class Method {
    EUCLIDEAN,
    EXPANDED,
    EXPANDED_ALT,
  };

  constexpr Method method = Method::EXPANDED_ALT;

  Discival result;

  if constexpr (method == Method::EUCLIDEAN) {
    // Simple, but with several square roots.

    // Upper bound on the input disc's actual radius.
    const BigRat &in_r = sausage.disc->Radius(inv_epsilon);

    BigRat center_r = BigRat::SqrtBounds(max_arc_dist_sq, inv_epsilon).second;
    // The radius is bounded by the sum of the distance to the arc and the
    // distance from the arc to the arc's circumference (input disc's radius),
    // because of the triangle inequality.
    BigRat bounding_radius = center_r + in_r;

    BigRat radius_sq = bounding_radius * bounding_radius;

    result = Discival(std::move(bounding_center),
                      std::move(radius_sq),
                      std::move(bounding_radius));

  } else if constexpr (method == Method::EXPANDED) {
    // This turns out to be bad, because Sqrt(c^2 * r^2) is expensive.
    BigRat radius_sq = max_arc_dist_sq + sausage.disc->radius_sq +
      BigRat::SqrtBounds(max_arc_dist_sq * sausage.disc->radius_sq,
                         inv_epsilon).second * 2;
    result = Discival(std::move(bounding_center), std::move(radius_sq));

  } else {
    CHECK(method == Method::EXPANDED_ALT);
    // Upper bound on the input disc's actual radius.
    const BigRat &in_r = sausage.disc->Radius(inv_epsilon);

    // Better to take the square root of just the max_arc_dist_sq.
    BigRat radius_sq =
      max_arc_dist_sq + sausage.disc->radius_sq +
      BigRat::SqrtBounds(max_arc_dist_sq, inv_epsilon).second * in_r * 2;
    result = Discival(std::move(bounding_center), std::move(radius_sq));
  }

  return TranslateDisc(&result,
                       sausage.translation.x,
                       sausage.translation.y,
                       inv_epsilon);
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

struct Hypersolver {

  static std::string_view RejectionReasonString(RejectionReason r) {
    switch (r) {
    default:
    case REJECTION_UNKNOWN:
      return ARED("MISSING?");
    case OUTSIDE_OUTER_PATCH:
      return "OUTP";
    case OUTSIDE_INNER_PATCH:
      return "INP";
    case OUTSIDE_OUTER_PATCH_BALL:
      return "OUTP_B";
    case OUTSIDE_INNER_PATCH_BALL:
      return "INP_B";
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
    case POINT_OUTSIDE6:
      return "PT6";
    case POLY_AREA:
      return "AREA";
    case DIAMETER:
      return "DIAM";
    case CLOSE_TO_DIAGONAL:
      return "DIAG";
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

    case CLOSE_TO_DIAGONAL:
      return std::format(ACYAN("{}"), RejectionReasonString(rej.reason));

    case POLY_AREA:
      return std::format(ACYAN("{}"), RejectionReasonString(rej.reason));

    case DIAMETER:
      return std::format(ACYAN("{}"), RejectionReasonString(rej.reason));

    case POINT_OUTSIDE4: {
      const Pt4Data *data = std::get_if<Pt4Data>(&rej.data);
      if (data == nullptr) return ARED("MISSING METADATA");
      return std::format(ACYAN("PT4")
                         AGREY("(")
                         ARED("{}") AGREY(", ")
                         AGREEN("{}") AGREY(")"),
                         data->edge, data->point);
    }

    case POINT_OUTSIDE6:
    case POINT_OUTSIDE5: {
      const Pt5Data *data = std::get_if<Pt5Data>(&rej.data);
      if (data == nullptr) return ARED("MISSING METADATA");
      return std::format(ACYAN("{}")
                         AGREY("(")
                         ARED("{}") AGREY(", ")
                         AGREEN("{}") AGREY(", ")
                         AYELLOW("{}") AGREY(")"),
                         RejectionReasonString(rej.reason),
                         data->edge, data->point,
                         data->bias.ToString());
    }
    }
  }

  struct Split {
    ParameterSet parameters;
    Split() : parameters(ParameterSet::Top()) {}
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

  double SampleInterval(ArcFour *rc, const Bigival &ival) {
    double lb = ival.LB().ToDouble();
    double ub = ival.UB().ToDouble();
    double width = ub - lb;
    return std::clamp(lb + RandDouble(rc) * width, lb, ub);
  }

  struct EdgePointMask {
    // Vector is size of inner hull (points to test).
    // Each set contains the edges to test for that point (often empty).
    std::vector<SmallIntSet<64>> point_edges;
  };

  EdgePointMask GetEdgePointMask(ArcFour *rc, const Volume &volume) {
    // n.b. we run this test in the opposite direction (outer loop is edges)
    // than the data structure wee're generating.
    //
    // Start with universal set and remove ones that we can rule out.
    SmallIntSet<64> all_points;
    for (int i = 0; i < inner_hull.size(); i++)
      all_points.Add(i);
    std::vector<SmallIntSet<64>> edge_points;
    edge_points.reserve(outer_hull.size());
    for (int i = 0; i < outer_hull.size(); i++)
      edge_points.push_back(all_points);

    // PERF: use corners of volume, since they are more likely to be
    // extremes.
    static constexpr int NUM_MASK_SAMPLES = 6;
    for (int s = 0; s < NUM_MASK_SAMPLES; s++) {
      // PERF: Detect if we have eliminated everything?

      const double outer_azimuth = SampleInterval(rc, volume[OUTER_AZIMUTH]);
      const double outer_angle = SampleInterval(rc, volume[OUTER_ANGLE]);
      const double inner_azimuth = SampleInterval(rc, volume[INNER_AZIMUTH]);
      const double inner_angle = SampleInterval(rc, volume[INNER_ANGLE]);
      const double inner_rot = SampleInterval(rc, volume[INNER_ROT]);
      const double inner_x = SampleInterval(rc, volume[INNER_X]);
      const double inner_y = SampleInterval(rc, volume[INNER_Y]);

      const vec3 oviewpos = ViewFromSpherical(outer_azimuth, outer_angle);
      const vec3 iviewpos = ViewFromSpherical(inner_azimuth, inner_angle);

      const frame3 outer_frame = FrameFromViewPos(oviewpos);
      const frame3 inner_frame = FrameFromViewPos(iviewpos);

      // Plot hulls.
      std::vector<vec2> outer_shadow =
        TransformHull(outer_hull, outer_frame);

      std::vector<vec2> inner_shadow =
        TransformHull(inner_hull, inner_frame);

      // And rotate.
      RotateAndTranslate(inner_rot, inner_x, inner_y,
                         &inner_shadow);

      // Now check each remaining outer edge against each remaining
      // sampled inner hull point.
      for (int start = 0; start < edge_points.size(); start++) {
        if (edge_points[start].Empty()) continue;

        const vec2 &va = outer_shadow[start];
        const vec2 &vb = outer_shadow[(start + 1) % outer_shadow.size()];

        // Normalize the edge so that epsilon below does not depend on
        // the edge's length.
        vec2 edge = vb - va;
        const double edge_len = length(edge);

        // For tiny edges, always run the high-precision routine.
        if (edge_len < 1.0e-6) [[unlikely]] {
          // (nothing is ruled out)

        } else {

          edge /= edge_len;

          for (int idx : edge_points[start]) {
            const vec2 &v = inner_shadow[idx];

            // Signed distance to edge.
            const double dist = cross(edge, v - va);

            // For our CCW hulls, a negative cross product means the
            // point is on the "outside" of the edge, and so we should
            // try to run the full test on that specific edge/point pair
            // to prove this volume is impossible. We also include
            // points that are close to the edge but appear (with
            // floating point inaccuracy) on the inside.
            if (dist <= 1.0e-9) {
              // ok. Keep it.
            } else {
              edge_points[start].Remove(idx);
            }
          }
        }
      }
    }

    // Now transpose.
    EdgePointMask ret;
    ret.point_edges.resize(inner_hull.size());
    for (int idx = 0; idx < inner_hull.size(); idx++) {
      for (int edge = 0; edge < outer_hull.size(); edge++) {
        if (edge_points[edge].Contains(idx)) {
          CHECK(idx < ret.point_edges.size());
          ret.point_edges[idx].Add(edge);
        }
      }
    }

    return ret;
  }

  struct Edge3D {
    // Indices into hull.
    int start = 0, end = 0;
    // vb - va
    BigVec3 vec;
    BigRat length_sq;
    // Cross product with each vertex in the inner hull, in hull
    // order.
    std::vector<BigVec3> v_cx3d;
  };

  // Return a lower bound on the inner polygon's minimum width. If the
  // polygon's minimum width wouldn't even fit in the outer polygon's
  // maximum diameter, then we can rule this configuration out (for
  // any angle/translation). Might return 0 (a valid but useless bound)
  // if the intervals include non-convex polygons.
  //
  // TODO: Need to compute the interval for the minimum (squared) width,
  // so that we can do the symmetric test with diameter.
  BigRat MinSquaredWidth(const ViewBoundsTrig &trig) {

    std::optional<BigRat> min_width_sq;

    // Intuitive but non-obvious fact: The minimum width is realized
    // between an edge and a vertex. (Theorem 2.1, Houle & Toussaint
    // "Computing the width of a set," 1988.) Moreover, the edge and
    // vertex must be antipodal.

    // Here we are using the edges as candidate directions for the
    // parallel lines of support. We then compute the separation
    // between the furthest away vertex (in the positive direction)
    // and this edge (which is the furthest away vertex in the
    // negative direction; but is just 0 because of convexity).

    for (const Edge3D &edge : inner_edges) {

      // Project the edge using the view.
      // Bigival dot_prod_edge_view = Dot(viewpos, edge.vec);
      Bigival dot_prod_edge_view = DotProductWithView(trig, edge.vec);
      BigRat max_proj_edge_len_sq =
        edge.length_sq - dot_prod_edge_view.Squared().LB();

      CHECK(BigRat::Sign(max_proj_edge_len_sq) != -1) << "Negative would "
        "imply a bug in the math since this is a squared length. Zero "
        "would be possible for a singular view position that is exactly "
        "parallel to the edge, but the view position is not singular. " <<
        trig.azimuth.ToString() << "\n" << trig.angle.ToString() <<
        "\n" << edge.start;

      // Project the 3d cross products (vec, edge) using the view.
      // This is computing cross(proj(vec, view), proj(edge, view)),
      // which the cross product of the projected vectors (area of 2d
      // parallelogram), by instead doing the cross product in 3D and
      // then projecting. (This avoids some substantial dependency
      // problems and also allows us to precompute.)
      std::vector<BigRat> area_lbs;
      area_lbs.reserve(inner_hull.size());
      for (const BigVec3 &cx3d : edge.v_cx3d) {
        BigRat area = DotProductWithView(trig, cx3d).LB();
        area_lbs.push_back(std::move(area));
      }

      // The distance between the parallel lines of separation is
      // the distance between this edge (one parallel line) and a
      // most distant vertex (no other vertex can be antipodal).
      // The height's numerator will be the area, so find the
      // maximum area.
      CHECK(!area_lbs.empty());
      BigRat max_lb = area_lbs[0];
      for (int k = 1; k < area_lbs.size(); k++) {
        max_lb = BigRat::Max(std::move(max_lb), area_lbs[k]);
      }

      if (BigRat::Sign(max_lb) != 1) {
        // This cannot happen for convex polygons, but if we have
        // enough uncertainty on the vertex positions that it includes
        // non-convex polygons, we won't be able to compute a valid
        // bound on the width. So just return a trivial lower bound.
        return BigRat(0);
      }

      // In this direction, we have the minimum squared separation.
      BigRat h_sq = (max_lb * max_lb) / max_proj_edge_len_sq;
      if (!min_width_sq.has_value() ||
          h_sq < min_width_sq.value()) {
        min_width_sq = std::move(h_sq);
      }
    }

    if (min_width_sq.has_value()) {
      return std::move(min_width_sq.value());
    } else {
      return BigRat(0);
    }
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

    // Disc bounds for the inner points. We compute this
    // sparsely, so each one comes with its hull point index.
    std::vector<std::pair<int, Discival>> discs;
  };

  // Process one volume, either proving it impossible or requesting
  // a split. If get_stats is true, it computes some additional data
  // for visualization or estimating AABB efficiency (more expensive).
  // This is where all the work happens. Thread safe and only takes
  // locks for really fast stuff (accumulating stats).
  ProcessResult ProcessOne(ArcFour *rc, const Volume &volume, bool get_stats) {
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
    BigInt inv_epsilon = min_width.Denominator() * (1024 * 1024);

    Timer otrig_timer;
    ViewBoundsTrig outer_trig(outer_azimuth, outer_angle, inv_epsilon);
    const double otrig_time = otrig_timer.Seconds();
    {
      MutexLock ml(&mu);
      trig_time += otrig_time;
    }

    // This is enough to test whether we're in the outer patch;
    // we'd like to exclude large regions ASAP (without e.g. forcing
    // splits on the inner parameters), so compute and test that
    // now.
    Vec3ival oviewpos = Vec3ival(
        outer_trig.an.sine * outer_trig.az.cosine,
        outer_trig.an.sine * outer_trig.az.sine,
        outer_trig.an.cosine);

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

    Timer itrig_timer;
    ViewBoundsTrig inner_trig(inner_azimuth, inner_angle, inv_epsilon);
    const double itrig_time = itrig_timer.Seconds();
    {
      MutexLock ml(&mu);
      trig_time += itrig_time;
    }

    Vec3ival iviewpos = Vec3ival(
        inner_trig.an.sine * inner_trig.az.cosine,
        inner_trig.an.sine * inner_trig.az.sine,
        inner_trig.an.cosine);

    if (!MightHaveCode(inner_code, inner_mask, iviewpos)) {
      return {Impossible(Rejection(OUTSIDE_INNER_PATCH))};
    }


    // Unlike the oviewposball, we use this again to compute
    // the smear radius.
    Ballival iviewposball = SphericalPatchBall(inner_trig,
                                               inv_epsilon);
    if (!MightHaveCodeWithBall(inner_code, inner_mask, iviewposball)) {
      return ProcessResult{.result =
        Impossible(Rejection(OUTSIDE_INNER_PATCH_BALL))};
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

    // The diagonal: When the two view positions are equal and the
    // translation and rotation are zero, it is not a solution (no
    // *strict* containment). We call this the "diagonal."
    // The interval arithmetic approach will not be able to rule
    // it out, though, because all of the volumes are nonzero size.
    // So we will use a different approach to prove impossibility
    // when close to the diagonal.
    //
    // (TODO: We should extend this to include symmetry since we also
    // need to deal with it near boundaries shared by two patches, but
    // they might only be "adjacent" because of the symmetry
    // operations.)

    if ((outer_azimuth - inner_azimuth).Abs() <= DIAG_THRESHOLD_AZ ==
        Bigival::MaybeBool::True &&
        (outer_angle - inner_angle).Abs() <= DIAG_THRESHOLD_AN ==
        Bigival::MaybeBool::True) {

      // Note that we actually only test that the view positions are
      // close. That's because when the view positions are close,
      // translating by a larger amount certainly isn't going to help.
      // We also think that rotating around z (more than epsilon) will
      // not help.
      static constexpr bool LOOSER_CONDITION = true;

      if (LOOSER_CONDITION ||
          (inner_x.Abs() <= DIAG_THRESHOLD_T == Bigival::MaybeBool::True &&
           inner_y.Abs() <= DIAG_THRESHOLD_T == Bigival::MaybeBool::True &&
           // Any multiple of 2pi, but our interval only includes 0..2pi+e.
           // So we just check these two points.
           // Note if we have radially symmetric hulls, we would need to
           // handle some other divisors here.
           (inner_rot.Abs() <= DIAG_THRESHOLD_R == Bigival::MaybeBool::True ||
            (inner_rot - TWO_PI).Abs() <= DIAG_THRESHOLD_R ==
            Bigival::MaybeBool::True))) {
        ProcessResult res;
        res.result = Impossible(Rejection(CLOSE_TO_DIAGONAL));
        return res;
      }
    }

    {
      // Area test. If the inner shadow's area is definitely bigger than
      // the outer's, then it cannot fit (regardless of angle / translation).
      // This is a very cheap test.
      Timer area_timer;
      // Area of the shadow is just the dot product of the view with the
      // 3d area vector.
      Bigival outer_area2 = DotProductWithView(outer_trig, outer_area_3d);
      Bigival inner_area2 = DotProductWithView(inner_trig, inner_area_3d);

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
    }

    // PERF: Don't check this (and area!) unless we have split on one
    // of the parameters it depends on.

    {
      // Another way that we can prove the inner cannot fit in the outer
      // (regardless of angle and translation) is to show that the
      // diameter of the inner poly is bigger than the diameter of the
      // outer poly. If that's true, then the inner diameter cannot fit
      // anywhere in the outer shape, since the diameter is its maximum.
      Bigival inner_dia = SquaredDiameterFromChords(inner_chords, inner_trig);
      Bigival outer_dia = SquaredDiameterFromChords(outer_chords, outer_trig);

      // Greater-or-equal is correct here because the inner poly must be
      // strictly contained.
      if (inner_dia >= outer_dia == Bigival::MaybeBool::True) {
        // Cannot fit.
        ProcessResult res;
        res.result = Impossible(Rejection(DIAMETER));
        return res;
      }
    }

    // TODO: Also test min widths.

    // Now we are going to enter the full loop and do point-edge tests.

    // Experimental:
    // When we are working with balls or discs, many operations preserve
    // the radius (rotation, translation, orthographic projection). One
    // nice thing about this is that we can compute the radius of the 3D
    // ball that arises from the azimuth/angle intervals up front; this
    // is independent of the vertex position (just its distance from
    // the origin, which just results in a scale).
    //
    // We can't just compute this from the az/an interval widths. Note
    // that an arcsecond of azimuth (east-west direction) has a much
    // larger effect on the bounding ball near the equator than it does
    // near the poles.
    //
    // But the good news is that we already almost have a good bounding
    // ball, which is the ballival that contains the inner patch for
    // the view position. See the called function for more details.

    // Smear radius for a point on the unit sphere, based on the azimuth
    // and angle intervals.
    /*
    const BigRat smear_radius_sq =
      SmearRadiusSq(iviewposball, inv_epsilon);
    */

    // The tests below are expensive because we're working with
    // intervals on arbitrary-precision rationals. We save ourselves
    // some time by first checking (heuristically) whether we should
    // bother with a given edge/point pair, by sampling from the
    // corresponding parameter space and seeing whether our sample
    // would pass the test. (Of course we won't be able to prove that
    // the parameterized point is outside the parameterized edge if
    // we have a concrete instantiation of each that fails the test.)
    // We use double-precision for speed; neither false negatives
    // nor false positives are a serious issue for this test. The
    // main risk is that we get to a subdivision so fine that
    // we are unable to accurately do the test with doubles. So
    // we allow a little slop.

    Timer filter_timer;
    EdgePointMask mask = GetEdgePointMask(rc, volume);
    const double filter_time_here = filter_timer.Seconds();

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
          DotProductWithView(outer_trig, edge_cross));
    }

    std::vector<Vec2ival> inner;
    if (get_stats) {
      inner.reserve(inner_hull.size());
    }

    std::vector<std::pair<int, Discival>> discs;
    if (get_stats) {
      // Note: This is sparse, but is probably still better to
      // avoid copying on resize?
      discs.reserve(inner_hull.size());
    }

    RotTrig rot_trig(inner_rot, inv_epsilon);

    double disc_time_here = 0.0;
    double sausage_full_time_here = 0.0;
    double sausage_endpoint_time_here = 0.0;
    double pt4_time_here = 0.0;
    double v_time_here = 0.0;
    Timer loop_timer;
    // Compute the inner hull point-by-point, which we call v. We can
    // exit early if any of these points are definitely outside the
    // outer hull.
    std::optional<Rejection> proved = std::nullopt;
    CHECK(mask.point_edges.size() == inner_hull.size());
    for (int inner_hull_idx = 0;
         inner_hull_idx < mask.point_edges.size();
         inner_hull_idx++) {
      const SmallIntSet<64> edge_indices = mask.point_edges[inner_hull_idx];
      if (edge_indices.Empty()) {
        counter_no_edges++;
        continue;
      }

      [[maybe_unused]]
      const BigVec3 &original_v =
        scube.vertices[inner_hull[inner_hull_idx]];
      // Vec2ival proj_v = TransformPointTo2D(inner_frame, original_v);

      Timer v_timer;
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
        GetBoundingAABB2(proj_v, rot_trig, inv_epsilon, inner_x, inner_y);
      v_time_here += v_timer.Seconds();

      // status.Print("proj_v: {}", proj_v.ToString());

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
      // (void)disc_in.Radius(inv_epsilon);
      // (Experimental: We are not using this yet!)
      /*
      Discival disc_in_new = GetInitialDisc(original_v, inner_trig,
                                            smear_radius_sq, inv_epsilon);
      {
        std::string color = disc_in_new.radius_sq < disc_in.radius_sq ?
          ANSI_GREEN : ANSI_RED;
        status.Print("{}orig radius_sq: {}. new radius_sq: {}" ANSI_RESET,
                     color,
                     disc_in.radius_sq.ToDouble(),
                     disc_in_new.radius_sq.ToDouble());
      }
      */

      /*
      // double disc_in_time = disc_timer.Seconds();
      Discival rot_disc = RotateDiscInnerBias(
          &disc_in, rot_trig, BIAS, inv_epsilon);
      */
      std::optional<Sausage> sausage =
        GetSausage(&disc_in, rot_trig,
                   Vec2ival(inner_x, inner_y), inv_epsilon);
      // disc_rot_time_here += (disc_timer.Seconds() - disc_in_time);
      // Discival disc =
      //   TranslateDisc(&rot_disc, inner_x, inner_y, inv_epsilon);
      disc_time_here += disc_timer.Seconds();

      if (get_stats) {
        inner.push_back(v_aabb);
        // discs.push_back(disc);
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
      CHECK(!edge_indices.Empty()) << "Should not do the work to prep then.";
      for (int start : edge_indices) {

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

        Timer pt4_timer;
        // Now test the inner point AABB.
        Bigival cross_product4 =
          edge.x * v_aabb.y - edge.y * v_aabb.x + cross_va_vb;

        const bool is_aabb_outside = !cross_product4.MightBePositive();
        pt4_time_here += pt4_timer.Seconds();
        if (is_aabb_outside) {
          Rejection pt4;
          pt4.reason = POINT_OUTSIDE4;
          pt4.data = {Pt4Data({
                .edge = (int8_t)start,
                .point = (int8_t)inner_hull_idx
              })};
          proved = {pt4};
          // We only need to finish all the points if we are getting
          // stats!
          if (!get_stats) break;
        }

        // Or can we find a disc that's outside?
        if (sausage.has_value()) {
          const Sausage &s = sausage.value();
          Timer sausage_endpoint_timer;

          // First, check this necessary condition that is a very
          // good filter. Both endpoints of the sausage need to be
          // outside the edge for the entire thing to be.
          const bool endpoints_outside =
            IsDiscOutsideEdge(s.tcenter_lb, s.disc->radius_sq,
                              edge, cross_va_vb) &&
            IsDiscOutsideEdge(s.tcenter_ub, s.disc->radius_sq,
                              edge, cross_va_vb);
          sausage_endpoint_time_here += sausage_endpoint_timer.Seconds();


          if (endpoints_outside) {
            // Compute full sausage disc.

            const BigRat BIAS = BigRat(3, 2);

            // XXX try multiple bias values!
            Timer sausage_full_timer;
            Discival disc = SausageDisc(
                s, rot_trig, BIAS, inv_epsilon);

            const bool is_disc_outside =
              IsDiscOutsideEdge(disc, edge, cross_va_vb);
            sausage_full_time_here += sausage_full_timer.Seconds();

            if (is_disc_outside) {
              Rejection pt6;
              pt6.reason = POINT_OUTSIDE6;
              // Note: This reuses pt5 payload for now; it is the same.
              pt6.data = {Pt5Data({
                    .edge = (int8_t)start,
                    .point = (int8_t)inner_hull_idx,
                    .bias = BIAS,
                  })};
              proved = {std::move(pt6)};

              if (!get_stats) break;
            } else {
              // Failed to find a bias despite the endpoints being
              // outside!
              counter_sausage_negative++;
            }

            if (get_stats) {
              // Do this last so we can consume the disc.
              discs.emplace_back(inner_hull_idx, std::move(disc));
            }


          }
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


    const int64_t ie_digits =
      get_stats ? inv_epsilon.ToString().size() : 0;

    {
      MutexLock ml(&mu);
      loop_time += loop_timer.Seconds();
      v_time += v_time_here;
      disc_time += disc_time_here;
      sausage_full_time += sausage_full_time_here;
      sausage_endpoint_time += sausage_endpoint_time_here;
      pt4_time += pt4_time_here;
      filter_time += filter_time_here;
      if (ie_digits > 0) {
        inv_epsilon_dig_total += ie_digits;
        inv_epsilon_dig_count++;
      }
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

  struct Shadows {
    std::vector<vec2> outer;
    std::vector<vec2> inner;
    bool opatch = false, ipatch = false;
    bool corner = false;
  };

  std::vector<vec2> TransformHull(const std::vector<int> &hull,
                                  const frame3 &f) const {
    std::vector<vec2> shadow;
    for (int idx : hull) {
      const vec3 &pt = small_scube.vertices[idx];
      vec3 v = transform_point(f, pt);
      shadow.push_back(vec2(v.x, v.y));
    }
    return shadow;
  }

  static void RotateAndTranslate(double alpha, double tx, double ty,
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
  }

  // Used for visualization and efficiency estimate.
  static constexpr int N_SAMPLES = 512;
  std::vector<Shadows> SampleShadows(ArcFour *rc,
                                     const Volume &volume,
                                     const ProcessResult &pr) const {

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

  static std::optional<double> ComputeDiscDigits(const ProcessResult &pr) {
    int64_t numer = 0;
    int64_t denom = 0;
    for (const auto &[idx, d] : pr.discs) {
      BigInt max_denom(1);
      max_denom = BigInt::Max(std::move(max_denom), d.radius_sq.Denominator());
      max_denom = BigInt::Max(std::move(max_denom), d.center.x.Denominator());
      max_denom = BigInt::Max(std::move(max_denom), d.center.y.Denominator());

      // In base 10.
      numer += max_denom.ToString().size();
      denom++;
    }

    if (denom > 0) {
      return {numer / double(denom)};
    } else {
      return std::nullopt;
    }
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
                 Hypercube::VolumeString(volume));


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

    std::optional<double> disc_mag =
      ComputeDiscDigits(pr);

    int highlight_edge = -1;
    int highlight_point = -1;
    if (const Impossible *imp = std::get_if<Impossible>(&pr.result)) {
      if (const Pt4Data *pt4 = std::get_if<Pt4Data>(&imp->rejection.data)) {
        highlight_edge = pt4->edge;
        highlight_point = pt4->point;
      } else if (const Pt5Data *pt5 =
                 std::get_if<Pt5Data>(&imp->rejection.data)) {
        highlight_edge = pt5->edge;
        highlight_point = pt5->point;
      }
    }

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
          if (a == highlight_edge) {
            img.BlendThickLine32(ax, ay, bx, by, 2, 0xFF6666AA);
          } else {
            img.BlendLine32(ax, ay, bx, by, 0xFF333388);
          }
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

      for (int idx = 0; idx < s.inner.size(); idx++) {
        const vec2 &v = s.inner[idx];
        const auto &[sx, sy] = scaler.Scale(v.x, v.y);
        const uint32_t color =
          (idx == highlight_point) ? 0xCCFF99CC : 0x33BB3388;
        if (s.ipatch) {
          img.BlendFilledCircleAA32(sx, sy, 2, color);
        } else {
          img.BlendThickCircleAA32(sx, sy, 3.0, 1.0, color);
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

    for (const auto &[idx, disc] : pr.discs) {
      // XXX bold if implicated?
      DrawCircle(disc, 0xCCFF3366);
    }

    int yy = 8;
    auto WriteLine = [&img, &yy](uint32_t color, std::string_view line) {
        img.BlendText32(8, yy, color, line);
        yy += ImageRGBA::TEXT_HEIGHT + 2;
      };

    for (const std::string &d :
           Util::SplitToLines(Hypercube::VolumeString(volume))) {
      WriteLine(0xAAAAAAFF, d);
    }

    auto PctString = [](std::string_view label, int n, int d){
        return
          std::format("{}: {}" AGREY("/") "{} "
                      AGREY("(") "{:.1f}%" AGREY(")"),
                      label,
                      n, d,
                      (100.0 * n) / d);
      };

    {
      const Bigival &outer_azimuth = volume[OUTER_AZIMUTH];
      const Bigival &outer_angle = volume[OUTER_ANGLE];
      const Bigival &inner_azimuth = volume[INNER_AZIMUTH];
      const Bigival &inner_angle = volume[INNER_ANGLE];

      Bigival daz = (outer_azimuth - inner_azimuth).Abs();
      Bigival dan = (outer_angle - inner_angle).Abs();

      bool diag_daz = (daz <= DIAG_THRESHOLD_AZ) == Bigival::MaybeBool::True;
      bool diag_dan = (dan <= DIAG_THRESHOLD_AN) == Bigival::MaybeBool::True;

      // TODO: Output in image!
    }


    WriteLine(0, "");

    WriteLine(
        0xFFCCCCFF,
        PctString("outer corners", num_corners_in_outer, num_corners));
    WriteLine(
        0xFFCCCCFF,
        PctString("outer sample", num_samples_in_outer, num_samples));
    WriteLine(
        0xCCFFCCFF,
        PctString("inner corners", num_corners_in_inner, num_corners));
    WriteLine(
        0xCCFFCCFF,
        PctString("inner sample", num_samples_in_inner, num_samples));

    if (efficiency.has_value()) {
      WriteLine(0, "");
      WriteLine(0x33CCFFFF,
                std::format("inner AABB eff.: " AWHITE("{:.2f}") "%",
                            efficiency.value() * 100.0));
    }

    if (disc_mag.has_value()) {
      WriteLine(0, "");
      WriteLine(
          0x33CCFFFF,
          std::format("disc digits: " AWHITE("{:.2f}"),
                      disc_mag.value()));
    }

    WriteLine(0, "");
    if (const Impossible *imp = std::get_if<Impossible>(&pr.result)) {
      WriteLine(0xFFFF33FF,
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
      WriteLine(0x33FFFFFF,
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

  // Split on the chosen dimension.
  BigRat SplitOn(const Volume &volume, int dim) const {
    const Bigival &oldival = volume[dim];

    // TODO:
    // Heuristic: When the volume contains the "diagonal," we have small
    // volumes all over the place that are treated differently. We would
    // like to split on the boundaries of this for efficiency. Alas,
    // they are not rectangular boundaries (defined by a-b). But if the
    // volume contains the diagonal, we can choose a split such that one
    // side will not contain any diagonal (and in fact is exactly
    // DIAG_THRESH away from it).

    // Only if we are splitting on one of the involved parameters:
    switch (dim) {
    case OUTER_AZIMUTH:
    case OUTER_ANGLE:
    case INNER_AZIMUTH:
    case INNER_ANGLE:
      // Handled below.
      break;
    default:
      return SplitInterval(oldival);
    }

    #if 0
    const Bigival &outer_azimuth = volume[OUTER_AZIMUTH];
    const Bigival &outer_angle = volume[OUTER_ANGLE];
    const Bigival &inner_azimuth = volume[INNER_AZIMUTH];
    const Bigival &inner_angle = volume[INNER_ANGLE];

    // We could consider only applying this heuristic when
    // the translation and rotation are small? We are sacrificing
    // some divide-and-conquer efficiency by splitting away from
    // the middle, but we also find ourselves back here after
    // splitting those other dimensions.

    // If the current volume contains the diagonal, ...

    if ((outer_azimuth - inner_azimuth).Abs() <= DIAG_THRESHOLD_AZ ==
        Bigival::MaybeBool::True &&
        (outer_angle - inner_angle).Abs() <= DIAG_THRESHOLD_AN ==
        Bigival::MaybeBool::True) {

    }
    #endif

    BigRat mid = SplitInterval(oldival);
    return mid;
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
                       RejectionReasonString(reason),
                       FormatNum(count));
        }

        std::string splitcount;
        for (int d = 0; d < NUM_DIMENSIONS; d++) {
          AppendFormat(&splitcount, "{} ", FormatNum(times_split[d]));
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

        // Get proof/sec.
        // The number of seconds since the first retained tick.
        double recent_time = run_timer.Seconds() - volume_progress[0].first;
        // The volume proved since the first retained tick.
        double recent_proved = 0.0;
        for (const auto &[timestamp_, vol] : volume_progress) {
          recent_proved += vol;
        }
        double recent_pps = recent_proved / recent_time;
        std::string ppm = std::format("{:.8g}", recent_pps * 60.0);

        // Progress bar wants integer fraction.
        const uint64_t denom = int64_t{1'000'000'000'000};
        const uint64_t numer = (proved_pct / 100.0) * denom;

        const double prove_volume_left = in_volume_d - volume_proved;
        double eta_sec = prove_volume_left / recent_pps;

        ANSI::ProgressBarOptions opt;
        opt.include_frac = false;
        opt.include_percent = false;
        std::string progress =
          ANSI::ProgressBar(
              numer, denom,
              std::format(
                  "Done: {:.8g} {:.2f}% Proved: {:.8g} {:.6f}%  {}",
                  volume_done, done_pct,
                  volume_proved, proved_pct, ANSI::Time(eta_sec)),
              run_timer.Seconds(),
              opt);

        std::string eff_str =
          efficiency_count > 0 ?
          std::format(APURPLE("{:.2f}") "% " AGREY("({})"),
                      (efficiency_total * 100.0) / efficiency_count,
                      efficiency_count) :
          ARED("??");

        std::string dd_str =
          disc_dig_count > 0 ?
          std::format(AWHITE("{:.1f}") AGREY("/") ACYAN("{}") " "
                      AGREY("({})"),
                      disc_dig_total / disc_dig_count,
                      inv_epsilon_dig_total / inv_epsilon_dig_count,
                      disc_dig_count) :
          ARED("??");

        std::string self_check_warning =
          SELF_CHECK ? ABGCOLOR(180, 0, 0, AWHITE("SELF CHECK ON")) " " : "";

        // TODO: Add (recent?) vol/sec
        auto ColorPct = [](double d) -> std::string {
            if (std::isnan(d)) return AORANGE("?");
            if (d > 1.0 || d < 0.0)
              return std::format(ARED("bad pct: {}"), d);
            // Compact is important here!
            std::string s = std::format("{:.3f}", d * 100.0);
            auto dot = s.find('.');
            if (dot == std::string::npos) return ARED("??????");
            std::string ipart = s.substr(0, dot);
            std::string fpart = s.substr(dot + 1, std::string::npos);
            // TODO: Can use heatmap color scale
            if (ipart == "0") ipart = "";
            // Three total digits are allowed, plus the dot.
            if (ipart == "100") return ABLUE("100") AGREY("%");
            int fdigits = 3 - ipart.size();
            CHECK(fdigits >= 0);
            if (fpart.size() > fdigits) fpart.resize(fdigits);
            return std::format(ABLUE("{}") AWHITE(".") ABLUE("{}")
                               AGREY("%"),
                               ipart, fpart);
          };


        std::string bias_histo = [&]{
            AutoHisto::Histo h = bias_count.GetHisto(12);
            return std::format("{}|{}|{}",
                               h.min,
                               bias_count.UnlabeledHoriz(12),
                               h.max);
          }();

        status.Status(
            AWHITE("——————————[")
            ACYAN("{} {}")
            AWHITE("]——————————————————————") "\n"
            // "Put volume information here!\n"
            "Split count: {}\n"
            // Recent interval
            "{}\n"
            // Rejection reason histo
            "{}\n"
            // Bias histo
            "Bias: {}\n"
            // Volume-based progress
            "{}Full vol: {:.5f}  in vol: {:.5f}  out vol: {:.5f}  ppm: {}\n"
            // Basic counts
            "{} processed, "
            "{} " AGREEN("✔") ", "
            "{} " ARED("⊹") ". "
            "{} " AORANGE("q") "  "
            "{} " AWHITE("no/") "\n"
            // Timing
            "{} ea. "
            "{} trig "
            "{} filt "
            // "{} area "
            "{} v "
            "{} pt4 "
            "{} ∫: "
            "{} ∫f "
            "{} pt6 "
            "{} loop\n"
            // Quality stats
            AORANGE("⊗") "mid: {}  "
            AORANGE("⊗") "∫: {}  "
            "In: {}   AABB eff.: {}  dd: {}\n"
            "{}\n", // bar
            outer_index, inner_index,
            splitcount,
            Hypercube::VolumeString(volume),
            rr,
            bias_histo,
            self_check_warning,
            full_volume_d, in_volume_d, volume_outscope, ppm,
            FormatNum(counter_processed.Read()),
            FormatNum(counter_completed.Read()),
            FormatNum(counter_split.Read()),
            FormatNum(node_queue.size()),
            FormatNum(counter_no_edges.Read()),
            ANSI::Time(time_each),
            ColorPct(trig_time / process_time),
            ColorPct(filter_time / process_time),
            // ColorPct(area_time / process_time),
            ColorPct(v_time / process_time),
            ColorPct(pt4_time / process_time),
            ColorPct(disc_time / process_time),
            ColorPct(sausage_endpoint_time / process_time),
            ColorPct(sausage_full_time / process_time),
            ColorPct(loop_time / process_time),
            FormatNum(counter_bad_midpoint.Read()),
            FormatNum(counter_sausage_negative.Read()),
            FormatNum(counter_inside.Read()),
            eff_str, dd_str,
            progress);
      });
  }

  bool Init() {
    filename = Hypercube::StandardFilename(outer_code, inner_code);

    if (Util::ExistsFile(filename)) {
      Timer load_timer;
      std::string contents = Util::ReadFile(filename);
      if (Hypercube::IsComplete(contents)) {
        status.Print("(" ACYAN("{}-{}") ")" " "
                     AWHITE("{}") " is already complete!",
                     outer_index, inner_index,
                     filename);
        return false;
      }

      status.Print("Continuing from incomplete {}", filename);
      hypercube->FromString(contents);
    }

    full_volume = Hypercube::Hypervolume(hypercube->bounds);
    full_volume_d = full_volume.ToDouble();


    // Initialize queue.
    {
      auto leaves = hypercube->GetLeaves(&volume_outscope, &volume_proved);

      for (auto &p : leaves)
        node_queue.emplace_back(std::move(p));

      volume_done = volume_outscope + volume_proved;
    }

    status.Print("Initialized. Remaining leaves: {}\n", node_queue.size());

    // Don't count hypercube loading or GetLeaves towards "runtime".
    run_timer.Reset();
    return true;
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
          if (hypercube->Empty()) {
            status.Print("Not writing empty cube.\n");
          } else {
            status.Print("Saving to {}...\n", filename);
            hypercube->ToDisk(filename);
          }
        });

      recent_shift_per.RunIf([&](){
          MutexLock ml(&mu);
          volume_progress.push_back(std::make_pair(run_timer.Seconds(), 0.0));
        });

      // Periodically turn on stats gathering for this thread until we
      // get some data.
      get_stats_next = get_stats_next || (counter_processed.Read() % 64) == 0;

      Timer process_timer;
      ProcessResult res = ProcessOne(&rc, volume, get_stats_next);
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

        if (std::optional<double> disc_dig =
            ComputeDiscDigits(res)) {
          MutexLock ml(&mu);
          disc_dig_count++;
          disc_dig_total += disc_dig.value();
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

        const double vol = Hypercube::Hypervolume(volume).ToDouble();

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

          case POINT_OUTSIDE5:
          case POINT_OUTSIDE6:
            if (const Pt5Data *pt5 =
                std::get_if<Pt5Data>(&imp->rejection.data)) {
              bias_count.Observe(pt5->bias.ToDouble());
            }
            break;

          default:
            volume_proved += vol;
            // Accumulate in place, since we get many small ticks.
            volume_progress.back().second += vol;
            maybe_save_image = true;
            break;
          }

          rejection_count[imp->rejection.reason]++;
        }

        if (maybe_save_image) {
          sample_proved_per.RunIf([&]() {
              MakeSampleImage(&rc, volume, res, "proved");
            });
        }

      } else if (Split *split = std::get_if<Split>(&res.result)) {
        // Can't rule it out. So split.

        // Normally we split along the longest dimension (in the split
        // mask), and near its center, but we can apply heuristics here.

        // int dim = RandomParameterFromSet(&rc, split->parameters);
        int dim = BestParameterFromSet(*hypercube, volume, split->parameters);
        CHECK(dim >= 0 && dim < NUM_DIMENSIONS);

        BigRat mid = SplitOn(volume, dim);

        // status.Print("Split dim {}, which is {}", dim, ParameterName(dim));
        Hypercube::Split split_node;
        split_node.axis = dim;

        const Bigival &oldival = volume[dim];
        Volume left = volume, right = volume;

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

    if (!Init())
      return;

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


    if (hypercube->Empty()) {
      status.Print("Not writing empty cube!", filename);
    } else {
      status.Print("Writing complete cube: {}", filename);
      hypercube->ToDisk(filename);
      status.Print("Success " AGREEN(":)") "\n");
    }
  }

  Hypersolver(int outer_idx, int inner_idx) :
    scube(BigScube(SCUBE_DIGITS)),
    boundaries(scube) {
    patch_info = LoadPatchInfo("scube-patchinfo.txt");

    ArcFour rc("hyper-init");

    hypercube.reset(new Hypercube);

    // Order the canonical patches by their codes, so that we can
    // select them by index from the command line.
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

    const PatchInfo::CanonicalPatch &outer = canonical[outer_idx].second;
    const PatchInfo::CanonicalPatch &inner = canonical[inner_idx].second;

    outer_index = outer_idx;
    inner_index = inner_idx;

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

    // Precomputation for the inner polygon's width calculation.
    inner_edges.reserve(inner_hull.size());
    for (int start = 0; start < inner_hull.size(); start++) {
      Edge3D edge;
      edge.start = start;
      edge.end = (start + 1) % inner_hull.size();
      const BigVec3 &va = scube.vertices[inner_hull[start]];
      const BigVec3 &vb = scube.vertices[inner_hull[edge.end]];
      edge.vec = vb - va;
      edge.v_cx3d.reserve(inner_hull.size());
      for (int vidx = 0; vidx < inner_hull.size(); vidx++) {
        const BigVec3 &v = scube.vertices[inner_hull[vidx]];
        edge.v_cx3d.push_back(cross(v - va, vb - va));
      }
      inner_edges.push_back(std::move(edge));
    }


    outer_chords = ComputeChords(scube, outer_hull);
    inner_chords = ComputeChords(scube, inner_hull);

    inubdir = std::format("inubs-{}-{}", outer_code, inner_code);
    (void)Util::MakeDir(inubdir);
  }

  void CheckHullRepresentation(ArcFour *rc, uint64_t code, uint64_t mask,
                               const std::vector<int> &hull) const {
    CHECK(hull.size() <= 64) << "Want to represent these with SmallIntSet, "
      "so we have a hard requirement on hull size. But this would be "
      "easy to increase if we needed.";

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

  int outer_index = 0, inner_index = 0;
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

  // Parallel to inner_hull (index = start). Precomputation for
  // minimum width calculation. For each edge, we precompute its 3D
  // cross product with every vertex of the hull. This allows us to
  // efficiently calculate projected 2D areas later via a dot product.
  std::vector<Edge3D> inner_edges;


  std::vector<Chord3D> outer_chords;
  std::vector<Chord3D> inner_chords;

  // 1/512 radians is a little more than 0.1 degrees.
  // 1/56 radians is a little more than 1 degree.
  const BigRat DIAG_THRESHOLD_AZ = BigRat(1, 56);
  const BigRat DIAG_THRESHOLD_AN = BigRat(1, 56);
  const BigRat DIAG_THRESHOLD_R = BigRat(1, 56);
  const BigRat DIAG_THRESHOLD_T = BigRat(1, 56);
  const Bigival TWO_PI = Bigival::Pi(BigInt(10000000)) * BigRat(2);

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
  Periodically save_per = Periodically(10 * 60, false);
  Periodically sample_per = Periodically(60 * 14.9);
  Periodically sample_proved_per = Periodically(60 * 9.1);
  Periodically render_per = Periodically(60 * 15.1);
  Periodically recent_shift_per = Periodically(1);

  std::unordered_map<RejectionReason, int64_t> rejection_count;
  int64_t times_split[NUM_DIMENSIONS] = {};

  AutoHisto bias_count = AutoHisto(10000);

  // Pair of (volume proved, timestamp) for recent ticks.
  // Timestamp is from run_timer.
  // Volume proved may be zero.
  LastNBuffer<std::pair<double, double>> volume_progress =
    LastNBuffer<std::pair<double, double>>(256, std::make_pair(0.0, 0.0));

  // Stats counters.
  double efficiency_total = 0.0;
  int64_t efficiency_count = 0;

  double disc_dig_total = 0.0;
  int64_t disc_dig_count = 0;
  int64_t inv_epsilon_dig_total = 0;
  int64_t inv_epsilon_dig_count = 0;


  double trig_time = 0.0, loop_time = 0.0, process_time = 0.0;
  double filter_time = 0.0;
  double disc_time = 0.0, sausage_full_time = 0.0, sausage_endpoint_time = 0.0;
  double area_time = 0.0, v_time = 0.0;
  double pt4_time = 0.0;

  // The hypervolume now done (this includes regions that we
  // determined are out of scope). Compare against the full volume.
  double volume_done = 0.0;
  // The volume that is excluded for being out of scope. Compare
  // against the full volume.
  double volume_outscope = 0.0;
  // The hypervolume where we definitively ruled out a solution.
  // Can compare this against (full - volume_outscope).
  double volume_proved = 0.0;

 private:
  static std::vector<Chord3D> ComputeChords(const BigPoly &poly,
                                            const std::vector<int> &hull) {
    std::vector<Chord3D> ret;
    ret.reserve((hull.size() * (hull.size() - 1)) / 2);
    for (int i = 0; i < hull.size(); i++) {
      for (int j = i + 1; j < hull.size(); j++) {
        BigVec3 v = poly.vertices[hull[j]] - poly.vertices[hull[i]];
        BigRat len_sq = dot(v, v);
        ret.push_back(Chord3D{
              .start = i,
              .end = j,
              .vec = std::move(v),
              .length_sq = std::move(len_sq),
            });
      }
    }

    return ret;
  }
};

static std::string Usage() {
  return "./inub.exe outer_idx inner_idx\n"
    "Each idx is a canonical patch index (when ordered by code).\n";
}

int main(int argc, char **argv) {
  ANSI::Init();
  Nice::SetLowPriority();

  CHECK(argc == 3) << Usage();

  std::optional<int> outer_idx = Util::ParseInt64Opt(argv[1]);
  std::optional<int> inner_idx = Util::ParseInt64Opt(argv[2]);

  CHECK(outer_idx.has_value() &&
        inner_idx.has_value()) << Usage();

  Hypersolver solver(outer_idx.value(), inner_idx.value());
  solver.Run();

  return 0;
}
