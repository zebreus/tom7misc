
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
