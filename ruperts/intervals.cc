
#include "intervals.h"

#include <array>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ansi.h"
#include "big-interval.h"
#include "big-polyhedra.h"
#include "bignum/big.h"

static constexpr bool SELF_CHECK = false;

std::string Vec3ival::ToString() const {
  return std::format("x: {}, y: {}, z: {}",
                     x.ToString(), y.ToString(), z.ToString());
}

std::string Vec2ival::ToString() const {
  return std::format("(⏹ x: {}, y: {})",
                     x.ToString(), y.ToString());
}


std::string Discival::ToString() const {
  return std::format("(⏺ c: {}, r²: {} " ABLUE("≅ {}") ")",
                     VecString(center), radius_sq.ToString(),
                     radius_sq.ToDouble());
}


std::string Frame3ival::ToString() const {
  return std::format(
      "frame3{{\n"
      "  .x = {}\n"
      "  .y = {}\n"
      "  .z = {}\n"
      "  .o = {}\n"
      "}}",
      x.ToString(),
      y.ToString(),
      z.ToString(),
      o.ToString());
}

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

  // Conceptually we are operating on the unit vectors within the view
  // AABB. Note that since the view position is an AABB, there will be
  // many points inside that are not actually unit-length. The
  // derivation above assumes unit length, so superficially there is a
  // problem here. This optimization is correct due to the inclusion
  // property of interval arithmetic. All we need is that frame_z.z is
  // a superset of all the z components of vectors (the unit-length
  // ones) that we care about. The worst that can happen is that the
  // resulting interval is too loose. (But this formulation actually
  // improves tightness because of the elimination of dependency
  // problems.)
  Bigival len = Bigival::Sqrt(Bigival(1) - frame_z.z.Squared(), inv_epsilon);
  if (SELF_CHECK) {
    CHECK(!len.ContainsOrApproachesZero()) << len.ToString();
  }
  Vec3ival frame_x = Vec3ival(-frame_z.y / len, frame_z.x / len, Bigival(0));
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
    .o = Vec3ival{Bigival(0), Bigival(0), Bigival(0)},
  };
}

Vec3ival ViewFromSpherical(const ViewBoundsTrig &trig) {
  return Vec3ival(trig.an.sine * trig.az.cosine,
                  trig.an.sine * trig.az.sine,
                  trig.an.cosine);
}

// Like Dot(ViewFromSpherical(azimuth, angle), v) but gives
// tighter bounds. As above, the angle intervals must both
// be less than 3 radians.
Bigival DotProductWithView(const ViewBoundsTrig &trig,
                           const BigVec3 &v) {
  CHECK(trig.azimuth.Width() < BigRat(3));
  CHECK(trig.angle.Width() < BigRat(3));

  // viewpos = (sin(an)cos(az), sin(an)sin(az), cos(an))
  // dot(viewpos, v) = sin(an) * (v.x*cos(az) + v.y*sin(az)) + v.z*cos(an)

  return trig.an.sine * (trig.az.cosine * v.x + trig.az.sine * v.y) +
    trig.an.cosine * v.z;
}

// Compute a bounding ball for the patch on the unit sphere
// given by the azimuth and angle (this is the view position).
// The patch must be smaller than a hemisphere or you will
// get a degenerate (but correct) result.
Ballival SphericalPatchBall(const ViewBoundsTrig &trig,
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

  // We have an approximate center (AABB) because of the
  // transcendental functions. We have our choice of center for the
  // bounding ball that we create, though! So we just use the midpoint
  // of this tiny interval. Uncertainty essentially gets transferred
  // into the radius.
  //
  // Informally, this point is on the unit sphere, and then we
  // are guaranteed that the ball contains the entire patch
  // (because it is not hemispherical).
  // This will also be true if we are reasonably close to the
  // sphere (which this will be) but there's a proof obligation
  // to revisit here.
  BigVec3 center = BigVec3(
      (trig.mid_sin_an * trig.mid_cos_az).Midpoint(),
      (trig.mid_sin_an * trig.mid_sin_az).Midpoint(),
      trig.mid_cos_an.Midpoint());

  // Find a squared radius that will include all the corners.
  BigRat max_sqdist(0);

  // Compute corners for the patch. These are tight bounds
  // on the sine and cosine of each corner, ordered as lb, ub.
  std::array<SinCos, 2> sin_cos_az;
  std::array<SinCos, 2> sin_cos_an;

  sin_cos_az[0] =
    SinCos(Bigival::Sin(trig.azimuth.LB(), inv_epsilon),
           Bigival::Cos(trig.azimuth.LB(), inv_epsilon));
  sin_cos_az[1] =
    SinCos(Bigival::Sin(trig.azimuth.UB(), inv_epsilon),
           Bigival::Cos(trig.azimuth.UB(), inv_epsilon));

  sin_cos_an[0] =
    SinCos(Bigival::Sin(trig.angle.LB(), inv_epsilon),
           Bigival::Cos(trig.angle.LB(), inv_epsilon));
  sin_cos_an[1] =
    SinCos(Bigival::Sin(trig.angle.UB(), inv_epsilon),
           Bigival::Cos(trig.angle.UB(), inv_epsilon));

  // The corners of the patch are the furthest away from the
  // chosen center. The furthest of these will determine the
  // radius.
  for (const SinCos &az : sin_cos_az) {
    for (const SinCos &an : sin_cos_an) {
      // The location of the corner.
      Vec3ival corner(an.sine * az.cosine, an.sine * az.sine, an.cosine);

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

Vec2ival GetBoundingAABB2(const Vec2ival &v_in,
                          const RotTrig &rot_trig,
                          const BigInt &inv_epsilon,
                          const Bigival &tx, const Bigival &ty) {

  // Compute the AABB of the four corners of v_in when
  // rotated by a specific angle (represented by its sin/cos).
  auto GetAABBAtAngle = [&](const SinCos &sc) {
    // Each edge is used twice.
    Bigival left_cos = v_in.x.LB() * sc.cosine;
    Bigival right_cos = v_in.x.UB() * sc.cosine;
    Bigival left_sin = v_in.x.LB() * sc.sine;
    Bigival right_sin = v_in.x.UB() * sc.sine;

    Bigival bottom_cos = v_in.y.LB() * sc.cosine;
    Bigival top_cos = v_in.y.UB() * sc.cosine;
    Bigival bottom_sin = v_in.y.LB() * sc.sine;
    Bigival top_sin = v_in.y.UB() * sc.sine;

    // bl = bottom-left, etc.
    Bigival x_bl = left_cos - bottom_sin;
    Bigival x_tl = left_cos - top_sin;
    Bigival x_br = right_cos - bottom_sin;
    Bigival x_tr = right_cos - top_sin;

    Bigival y_bl = left_sin + bottom_cos;
    Bigival y_tl = left_sin + top_cos;
    Bigival y_br = right_sin + bottom_cos;
    Bigival y_tr = right_sin + top_cos;

    // Compute the union of the four rotated corners.
    return Vec2ival(
        Bigival::Union(Bigival::Union(std::move(x_bl), std::move(x_tl)),
                       Bigival::Union(std::move(x_br), std::move(x_tr))),
        Bigival::Union(Bigival::Union(std::move(y_bl), std::move(y_tl)),
                       Bigival::Union(std::move(y_br), std::move(y_tr))));
  };

  // Get the AABB of all four corners rotated to the start and end angles.
  Vec2ival lower_aabb = GetAABBAtAngle(rot_trig.lower);
  Vec2ival upper_aabb = GetAABBAtAngle(rot_trig.upper);
  std::optional<Bigival> x_bounds =
    Bigival::Union(lower_aabb.x, upper_aabb.x);
  std::optional<Bigival> y_bounds =
    Bigival::Union(lower_aabb.y, upper_aabb.y);

  // Are we maybe crossing an axis?
  bool possible_x_extremum =
    (v_in.x * rot_trig.sin_a + v_in.y * rot_trig.cos_a).ContainsZero();
  bool possible_y_extremum =
    (v_in.x * rot_trig.cos_a - v_in.y * rot_trig.sin_a).ContainsZero();

  if (possible_x_extremum || possible_y_extremum) {

    // PERF: Avoid copying
    std::array<BigVec2, 4> corners = {
      BigVec2(v_in.x.LB(), v_in.y.LB()),
      BigVec2(v_in.x.LB(), v_in.y.UB()),
      BigVec2(v_in.x.UB(), v_in.y.LB()),
      BigVec2(v_in.x.UB(), v_in.y.UB()),
    };

    // Determine the sign of cos and sin over the angle interval.
    // 0 means mixed, 1 positive, -1 negative.
    int cos_sign = 0;
    if (BigRat::Sign(rot_trig.cos_a.LB()) == 1) cos_sign = 1;
    else if (BigRat::Sign(rot_trig.cos_a.UB()) == -1) cos_sign = -1;

    int sin_sign = 0;
    if (BigRat::Sign(rot_trig.sin_a.LB()) == 1) sin_sign = 1;
    else if (BigRat::Sign(rot_trig.sin_a.UB()) == -1) sin_sign = -1;

    // There are four possible axis crossings (+/- x, +/- y). Whenever
    // we might cross one, we save the max squared radius of the point,
    // which we can then use to extend the bounding box in that direction.
    // The goal is to delay sqrts.

    if (possible_y_extremum) {
      BigRat max_r_sq_pos_x(0), max_r_sq_neg_x(0);
      for (const BigVec2 &c : corners) {
        BigRat r_sq = dot(c, c);
        if (BigRat::Sign(r_sq) == 0) continue;

        // The x-coordinate is extremal when the y-coordinate is zero.
        Bigival y_prime = c.y * rot_trig.cos_a + c.x * rot_trig.sin_a;
        if (y_prime.ContainsZero()) {
          if ((BigRat::Sign(c.x) == cos_sign || cos_sign == 0) &&
              (BigRat::Sign(-c.y) == sin_sign || sin_sign == 0)) {
            max_r_sq_pos_x = BigRat::Max(std::move(max_r_sq_pos_x), r_sq);
          }
          if ((BigRat::Sign(-c.x) == cos_sign || cos_sign == 0) &&
              (BigRat::Sign(c.y) == sin_sign || sin_sign == 0)) {
            max_r_sq_neg_x = BigRat::Max(std::move(max_r_sq_neg_x), r_sq);
          }
        }
      }

      CHECK(x_bounds.has_value());
      // After checking all corners, compute sqrt for the largest radii found.
      if (BigRat::Sign(max_r_sq_pos_x) == 1) {
        BigRat r = BigRat::SqrtBounds(max_r_sq_pos_x, inv_epsilon).second;
        x_bounds.value().Accumulate(r);
      }
      if (BigRat::Sign(max_r_sq_neg_x) == 1) {
        BigRat r = BigRat::SqrtBounds(max_r_sq_neg_x, inv_epsilon).second;
        x_bounds.value().Accumulate(-r);
      }
    }

    if (possible_x_extremum) {
      // The y-coordinate is extremal when the x-coordinate is zero.
      BigRat max_r_sq_pos_y(0), max_r_sq_neg_y(0);
      for (const BigVec2 &c : corners) {
        BigRat r_sq = dot(c, c);
        if (BigRat::Sign(r_sq) == 0) continue;

        Bigival x_prime = c.x * rot_trig.cos_a - c.y * rot_trig.sin_a;
        if (x_prime.ContainsZero()) {
          if ((BigRat::Sign(c.y) == cos_sign || cos_sign == 0) &&
              (BigRat::Sign(c.x) == sin_sign || sin_sign == 0)) {
            max_r_sq_pos_y = BigRat::Max(std::move(max_r_sq_pos_y), r_sq);
          }
          if ((BigRat::Sign(-c.y) == cos_sign || cos_sign == 0) &&
              (BigRat::Sign(-c.x) == sin_sign || sin_sign == 0)) {
            max_r_sq_neg_y = BigRat::Max(std::move(max_r_sq_neg_y), r_sq);
          }
        }
      }

      CHECK(y_bounds.has_value());
      if (BigRat::Sign(max_r_sq_pos_y) == 1) {
        BigRat r = BigRat::SqrtBounds(max_r_sq_pos_y, inv_epsilon).second;
        y_bounds.value().Accumulate(r);
      }
      if (BigRat::Sign(max_r_sq_neg_y) == 1) {
        BigRat r = BigRat::SqrtBounds(max_r_sq_neg_y, inv_epsilon).second;
        y_bounds.value().Accumulate(-r);
      }
    }
  }

  return Vec2ival(std::move(x_bounds.value()) + tx,
                  std::move(y_bounds.value()) + ty);
}

bool MightOverlap(const Discival &disc, const Vec2ival &aabb) {
  // We check for overlap by finding the minimum possible squared distance
  // between the disc's center and any point in the AABB. If this distance
  // is less than or equal to the disc's squared radius, they overlap.

  // The closest point in the AABB to the disc's center can be found
  // by considering each axis independently.
  Bigival dx = aabb.x - disc.center.x;
  Bigival dy = aabb.y - disc.center.y;

  // Find the minimum squared distance along each axis. If the interval
  // of differences [aabb.x.LB() - center.x, aabb.x.UB() - center.x]
  // contains zero, it means the center's coordinate is within the AABB's
  // range for that axis, so the minimum distance contribution is zero.
  // Otherwise, the minimum distance is to one of the endpoints.
  BigRat min_dx_sq = dx.ContainsZero() ?
    BigRat(0) :
    BigRat::Min(dx.LB() * dx.LB(), dx.UB() * dx.UB());

  BigRat min_dy_sq = dy.ContainsZero() ?
    BigRat(0) :
    BigRat::Min(dy.LB() * dy.LB(), dy.UB() * dy.UB());

  // The total minimum squared distance is the sum of the components.
  const BigRat min_dist_sq = std::move(min_dx_sq) + min_dy_sq;

  return min_dist_sq <= disc.radius_sq;
}

BigRat ExpandSquaredRadius(const BigRat &radius_sq,
                           const BigRat &error_term,
                           const BigInt &inv_epsilon) {
  CHECK(BigRat::Sign(error_term) != -1) << "Precondition.";

  // The simple upper bound is correct when:
  //   r^2 + e >= (r + e)^2
  //   r^2 + e >= r^2 + e^2 + 2re
  //   e >= e^2 + 2re
  // e is non-negative, so we can divide:
  //   1 >= e + 2r
  //   1 - e >= 2r
  //   2r <= 1 - e
  // if 1-e is negative, we can't use the fast path.
  // otherwise square both sides, since we have r^2 as input
  //   4r^2 <= (1 - e)^2
  BigRat ome = BigRat(1) - error_term;
  if (BigRat::Sign(ome) != -1 && radius_sq * BigRat(4) <= ome * ome) {
    // When the error term approaches zero, this bound is
    // safe for a radius less than 1/2.
    return radius_sq + error_term;
  } else {
    // General-purpose Euclidean version.
    BigRat r = BigRat::SqrtBounds(radius_sq, inv_epsilon).second + error_term;
    return r * r;
  }
}

BigRat MaxSquaredDiameter(const std::vector<Vec2ival> &vs) {
  CHECK(vs.size() >= 2) << "Precondition";

  BigRat max_sq_dist(0);

  for (size_t i = 0; i < vs.size(); ++i) {
    const Vec2ival &va = vs[i];
    for (size_t j = i + 1; j < vs.size(); ++j) {
      const Vec2ival &vb = vs[j];

      BigRat current_max_sq =
        (va.x - vb.x).Squared().UB() +
        (va.y - vb.y).Squared().UB();

      max_sq_dist = BigRat::Max(std::move(max_sq_dist),
                                std::move(current_max_sq));
    }
  }

  return max_sq_dist;
}

Discival RotateDiscInnerBias(
    // Mutable because we might calculate and cache radius.
    Discival *disc,
    const RotTrig &rot_trig,
    // A factor > 1 pushes the center away from the origin
    // to create a tighter inner bound. 1.0 is unbiased.
    const BigRat &bias,
    const BigInt &inv_epsilon) {

  if (rot_trig.angle.Width() > BigRat(3)) {
    // We can't get a good disc with this method. Just return
    // something correct. A really simple choice is just a
    // disc centered at the origin, whose radius is as though
    // we sweep the input disc over the entire circle.
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

  // Use precomputed midpoint.
  Vec2ival arc_center_ival(
      disc->center.x * rot_trig.mid.cosine -
      disc->center.y * rot_trig.mid.sine,
      disc->center.x * rot_trig.mid.sine +
      disc->center.y * rot_trig.mid.cosine);
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
  // point. It'd also make TryCorners test cheaper, since there is
  // just one radius.
  auto RotatePt = [&](const SinCos &endpoint, const BigVec2 &p) {
      return Vec2ival(p.x * endpoint.cosine - p.y * endpoint.sine,
                      p.x * endpoint.sine + p.y * endpoint.cosine);
    };

  // Very tight AABBs bounding the centers of the rotated disc at
  // the angle lower bound and upper bound.
  Vec2ival center_lb = RotatePt(rot_trig.lower, disc->center);
  Vec2ival center_ub = RotatePt(rot_trig.upper, disc->center);

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

  if constexpr (method == Method::EUCLIDEAN) {
    // Simple, but with several square roots.
    BigRat center_r = BigRat::SqrtBounds(max_arc_dist_sq, inv_epsilon).second;
    // The radius is bounded by the sum of the distance to the arc and the
    // distance from the arc to the arc's circumference (input disc's radius),
    // because of the triangle inequality.
    BigRat bounding_radius = center_r + in_r;

    BigRat radius_sq = bounding_radius * bounding_radius;

    return Discival(std::move(bounding_center),
                    std::move(radius_sq),
                    std::move(bounding_radius));

  } else if constexpr (method == Method::EXPANDED) {
    // This turns out to be bad, because Sqrt(c^2 * r^2) is expensive.
    BigRat radius_sq = max_arc_dist_sq + disc->radius_sq +
      BigRat::SqrtBounds(max_arc_dist_sq * disc->radius_sq,
                         inv_epsilon).second * 2;
    return Discival(std::move(bounding_center), std::move(radius_sq));

  } else {
    CHECK(method == Method::EXPANDED_ALT);
    // Better to take the square root of just the max_arc_dist_sq.
    BigRat radius_sq =
      max_arc_dist_sq + disc->radius_sq +
      BigRat::SqrtBounds(max_arc_dist_sq, inv_epsilon).second * in_r * 2;
    return Discival(std::move(bounding_center), std::move(radius_sq));
  }
}

Discival TranslateDisc(Discival *disc,
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
  // corner plus the original radius is an upper bound.

  const BigRat &original_radius = disc->Radius(inv_epsilon);

  // All the corners are the same distance from the center:
  BigRat half_w = tx.Width() / 2;
  BigRat half_h = ty.Width() / 2;

  // We can just sum the square roots of the squared radii and then
  // square that, or (c + r)^2 = c^2 + r^2 + 2cr.
  // The latter seems to produce much larger denominators.
  #if 1
  BigRat corner_dist =
    BigRat::SqrtBounds(half_w * half_w + half_h * half_h, inv_epsilon).second;

  // And the original radius.
  BigRat bound_radius = corner_dist + original_radius;

  BigRat radius_sq = bound_radius * bound_radius;

  return Discival(std::move(bound_center),
                  std::move(radius_sq),
                  std::move(bound_radius));
  #else

  BigRat corner_dist_sq = half_w * half_w + half_h * half_h;
  BigRat radius_sq = corner_dist_sq + disc->radius_sq +
    BigRat::SqrtBounds(corner_dist_sq, inv_epsilon).second *
    original_radius * 2;
  return Discival(std::move(bound_center),
                  std::move(radius_sq));

  #endif
}

// Helper for the two functions below.
// We want to test if for all points p in the disc, the line-side test
//   L(p) = edge.x * p.y - edge.y * p.x + cross_va_vb
// is strictly negative.
static bool IsDiscOutsideEdgeInternal(
    // The value of L at the disc's center.
    const Bigival &l_at_center,
    const BigRat &disc_radius_sq,
    const Vec2ival &outer_edge) {

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
  Bigival margin_sq = edge_len_sq * disc_radius_sq;

  // Now, check whether the inequality can hold.
  return min_l_at_center_sq > margin_sq.UB();
}

bool IsDiscOutsideEdge(const Discival &disc,
                       const Vec2ival &outer_edge,
                       const Bigival &outer_cross_va_vb) {
  // It's easy to just make a singular interval for the disc
  // center, but this is used in inner loops. So specialize
  // to a BigRat center.
  return IsDiscOutsideEdgeInternal(
      outer_edge.x * disc.center.y -
      outer_edge.y * disc.center.x +
      outer_cross_va_vb,
      disc.radius_sq,
      outer_edge);
}

bool IsDiscOutsideEdge(const Vec2ival &disc_center,
                       const BigRat &disc_radius_sq,
                       const Vec2ival &outer_edge,
                       const Bigival &outer_cross_va_vb) {
  return IsDiscOutsideEdgeInternal(
      outer_edge.x * disc_center.y -
      outer_edge.y * disc_center.x +
      outer_cross_va_vb,
      disc_radius_sq,
      outer_edge);
}


Discival GetInitialDisc(const BigVec3 &v,
                        const ViewBoundsTrig &trig,
                        const BigRat &smear_radius_sq,
                        const BigInt &inv_epsilon) {
  // The center will be the center of the az/an patch, which we already
  // computed in trig.
  //
  // The radius of the disc is the sum of two components (by triangle
  // inequality):
  //  1. The radius of the AABB for the center (center uncertainty).
  //  2. The smear radius from the patch, scaled by the vertex's magnitude.

  // AABB for the projection of the vertex from the central view direction.
  const Vec2ival center_aabb(
      trig.mid_sin_az * -v.x + trig.mid_cos_az * v.y,
      -trig.mid_cos_an * (trig.mid_cos_az * v.x + trig.mid_sin_az * v.y) +
      trig.mid_sin_an * v.z);

  // Choose a center that is close to the center of the patch.
  BigVec2 chosen_center(center_aabb.x.Midpoint(),
                        center_aabb.y.Midpoint());

  // Now the radius is approximately the smear_radius, but we need to
  // account for the error from the trigonometric approximations (we have
  // not computed the exact center).

  // Radius component from patch smear. We scale the smear radius by the
  // vector's distance from the origin.
  //
  // PERF: This squaring is not expensive, but we could precompute it
  // since it only depends on the original vector.
  const BigRat v_length_sq = dot(v, v);
  // PERF: Might be better to do two smaller square roots, then multiply?
  // We also can precompute the unsquared smear radius.
  const BigRat smear_radius_scaled_sq = v_length_sq * smear_radius_sq;

  enum class Method {
    EUCLIDEAN,
    MANHATTAN,
    OPTIMISTIC,
  };

  static constexpr Method method = Method::OPTIMISTIC;

  Discival disc;

  if constexpr (method == Method::EUCLIDEAN) {
    // Straightforward Euclidean calculation with triangle inequality.
    // This is way too slow because of the square roots.

    // Radius component from center uncertainty. Should be tiny.
    const BigRat half_w = center_aabb.x.Width() / 2;
    const BigRat half_h = center_aabb.y.Width() / 2;

    const BigRat center_uncertainty_r_sq = half_w * half_w + half_h * half_h;
    // status.Print("center_uncertainty_r^2 digits: {}",
    // center_uncertainty_r_sq.Denominator().ToString().size());
    const BigRat center_uncertainty_r =
      BigRat::SqrtBounds(center_uncertainty_r_sq, inv_epsilon).second;

    const BigRat smear_scaled_r =
      BigRat::SqrtBounds(smear_radius_scaled_sq, inv_epsilon).second;

    BigRat radius = center_uncertainty_r + smear_scaled_r;
    BigRat radius_sq = radius * radius;

    disc = Discival(std::move(chosen_center),
                    std::move(radius_sq),
                    std::move(radius));

  } else if constexpr (method == Method::MANHATTAN) {

    // Best would be Sqrt(half_w^2 + half_h^2), but these intervals are tiny
    // so the sqrt is extremely expensive (denominator has like 280 digits).
    // Manhattan distance to the corner is also an upper bound, and numerically
    // much simpler.
    BigRat center_uncertainty_r =
      (center_aabb.x.Width() + center_aabb.y.Width()) / 2;

    const BigRat smear_scaled_r =
      BigRat::SqrtBounds(smear_radius_scaled_sq, inv_epsilon).second;

    // Total radius is sum of the two radii.
    // TODO: Compare (c + r)^2 decomposition.
    BigRat radius = center_uncertainty_r + smear_scaled_r;
    BigRat radius_sq = radius * radius;

    disc = Discival(std::move(chosen_center),
                    std::move(radius_sq),
                    std::move(radius));
  } else {
    CHECK(method == Method::OPTIMISTIC);

    // Here we have a small error term coming from the trig.
    // We use the Manhattan distance to a corner.
    BigRat center_uncertainty_r =
      (center_aabb.x.Width() + center_aabb.y.Width()) / 2;

    BigRat radius_sq =
      ExpandSquaredRadius(smear_radius_scaled_sq,
                          center_uncertainty_r,
                          inv_epsilon);

    disc = Discival(std::move(chosen_center),
                    std::move(radius_sq));
  }


  if (SELF_CHECK) {
    // Each of the four corners of the az/an intervals is a valid pair of
    // parameters, so produce the location of the projected vertex for those
    // parameters. The result is a small AABB (due to sin/cos inaccuracy).
    // This AABB must overlap the disc we computed, since the disc is
    // supposed to contain all points.
    for (int i = 0; i < 4; i++) {
      const BigRat &az =
        (i & 0b01) ? trig.azimuth.LB() : trig.azimuth.UB();
      const BigRat &an =
        (i & 0b10) ? trig.angle.LB() : trig.angle.UB();

      Bigival sin_az = Bigival::Sin(az, inv_epsilon);
      Bigival cos_az = Bigival::Cos(az, inv_epsilon);
      Bigival sin_an = Bigival::Sin(an, inv_epsilon);
      Bigival cos_an = Bigival::Cos(an, inv_epsilon);

      // AABB for the projection of the vertex from this corner view.
      const Vec2ival corner_aabb(
          cos_az * v.y - sin_az * v.x,
          -cos_an * (cos_az * v.x + sin_az * v.y) + sin_an * v.z);

      CHECK(MightOverlap(disc, corner_aabb)) <<
        "Corner " << i << " does not overlap the disc!\n"
        "Input:\n"
        "  v: " << VecString(v) << "\n"
        "  azimuth: " << trig.azimuth.ToString() << "\n"
        "  angle: " << trig.angle.ToString() << "\n"
        "  smear_radius_sq ≅ " << smear_radius_sq.ToDouble() << "\n"
        "Got:\n"
        "  disc: " << disc.ToString() << "\n"
        "  corner aabb: " << corner_aabb.ToString();
    }
  }

  return disc;
}

// Experimental; incomplete.
//
// Consider an abstract point on the unit sphere. When we rotate that
// sphere to view it from the view position (which is some unit vector
// in the angle/azimuth patch), the point can move within some radius.
// Compute an upper bound on this radius (squared). This radius is
// the same for any vertex (on the unit sphere) and is invariant under
// rotation and orthographic projection, which is what we do in the
// main loop.
//
// This is based on the patch bounding ball for the view position. This
// ball contains every vector in the angle/azimuth patch, but its center
// is not quite on the unit sphere, so we need to patch that up.
BigRat SmearRadiusSq(const Ballival &patch_ball,
                     const BigInt &inv_epsilon) {
  // We can almost just use patch_ball.radius_sq, but its center is not
  // actually on the unit sphere because of trig inaccuracy. Consider the
  // point u that is the normalized patch_ball.center. Using the triangle
  // inequality, we know that the radius of a ball centered at u would
  // be no more than the distance from the center to u, and the radius
  // of the patch ball.

  // The center will be very close to u. We know that they are parallel
  // and u has length 1, so the distance is easy to compute as the
  // difference in their lengths.

  // s is the squared length of the center vector. The squared length
  // of u is 1.
  BigRat s = dot(patch_ball.center, patch_ball.center);

  // The actual error e = |sqrt(s)^2 - 1^1|
  // (a^2 - b^2) = (a - b)(a + b)
  // |sqrt(s)^2 - 1^1| = |(sqrt(s) - 1)(sqrt(s) + 1)|
  // |s - 1| = (sqrt(s) - 1)(sqrt(s) + 1)      (s is positive)
  // |(s - 1)/(sqrt(s) + 1)| = |sqrt(s) - 1|
  // |(s - 1)|/(sqrt(s) + 1) = |sqrt(s) - 1|  (sqrt(s) is non-negative)

  // So we have e = |sqrt(s) - 1| = |(s - 1)|/(sqrt(s) + 1).

  // The smallest the denominator could be is if sqrt(s) = 0. Then
  // we are just dividing by 1.
  //
  // So we have e <= |(s - 1)| as a reasonable bound.
  // Since we know sqrt(s) is close to 1, the true value for e is
  // more like |(s - 1)|/(1 + 1), which means we are off by a factor
  // of 2. This is a small price to pay for avoiding the square
  // root, however!
  BigRat e = BigRat::Abs(s - BigRat(1));

  return ExpandSquaredRadius(patch_ball.radius_sq, e, inv_epsilon);
}


Bigival SquaredDiameterFromChords(
    const std::vector<Chord3D> &chords,
    const ViewBoundsTrig &trig) {
  CHECK(!chords.empty());

  Bigival max_diam_sq(0);

  for (const Chord3D &chord : chords) {
    Bigival dot_sq =
      DotProductWithView(trig, chord.vec).Squared();

    // Note that the dot product is maximized when it is parallel
    // to the view direction. But the projected chord is longest when
    // it is perpendicular. This is why we subtract from the original
    // squared length (using Pythagorean theorem).
    Bigival max_proj_len_sq = chord.length_sq - dot_sq;

    max_diam_sq = Bigival::Max(std::move(max_diam_sq), max_proj_len_sq);
  }

  return max_diam_sq;
}

