
#include "intervals.h"

#include <format>
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
