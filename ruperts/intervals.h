
// General purpose interval arithmetic stuff for the ruperts
// project. Maybe some of this could go in big-interval or
// bignum library.

// Maybe could be called ival-polyhedra.h? It kind of
// parallels the structure of those.

#ifndef _RUPERTS_INTERVALS_H
#define _RUPERTS_INTERVALS_H

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/logging.h"
#include "big-interval.h"
#include "big-polyhedra.h"
#include "bignum/big.h"

// Vec3 where each component is an interval.
struct Vec3ival {
  Bigival x, y, z;
  Vec3ival(Bigival xx, Bigival yy, Bigival zz) :
    x(std::move(xx)), y(std::move(yy)), z(std::move(zz)) {}
  Vec3ival() : x(0), y(0), z(0) {}

  std::string ToString() const;
};

inline Bigival Dot(const Vec3ival &a, const BigVec3 &b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

// PERF: I think we only need Rot3 for this task; the origin is always
// zero. Can save ourselves some pointless addition and copying.
struct Frame3ival {
  Vec3ival x = {Bigival(1), Bigival(0), Bigival(0)};
  Vec3ival y = {Bigival(0), Bigival(1), Bigival(0)};
  Vec3ival z = {Bigival(0), Bigival(0), Bigival(1)};
  Vec3ival o = {Bigival(0), Bigival(0), Bigival(0)};

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

  std::string ToString() const;

  bool Contains(const BigVec2 &v) const {
    return x.Contains(v.x) && y.Contains(v.y);
  }
};

// Small Fixed-size matrices stored in column major format.
struct Mat3ival {
  // left column
  Vec3ival x = {Bigival(1), Bigival(0), Bigival(0)};
  // middle column
  Vec3ival y = {Bigival(0), Bigival(1), Bigival(0)};
  // right column
  Vec3ival z = {Bigival(0), Bigival(0), Bigival(1)};

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


// Bounding disc.
struct Discival {
  // An exact center.
  BigVec2 center;
  // Upper bound on the squared radius.
  BigRat radius_sq;
  // Upper bound on the radius. We sometimes have this anyway,
  // in which case we can save the trouble of iteratively
  // approximating the square root. Note that this does not
  // need to be the exact square root of radius_sq, but both
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

  Discival() : center(BigRat(0), BigRat(0)), radius_sq(1) {}

  Discival(const Vec2ival &v) : center(v.x.Midpoint(),
                                       v.y.Midpoint()) {
    // All corners are the same distance from the exact
    // center.
    BigRat dx = v.x.Width() / 2;
    BigRat dy = v.y.Width() / 2;
    radius_sq = dx * dx + dy * dy;
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

  std::string ToString() const;
};

inline Bigival Cross(const Vec2ival &a, const Vec2ival &b) {
  return a.x * b.y - a.y * b.x;
}

inline Vec3ival Cross(const Vec3ival &a, const Vec3ival &b) {
  return Vec3ival(
    a.y * b.z - a.z * b.y,
    a.z * b.x - a.x * b.z,
    a.x * b.y - a.y * b.x);
}

inline Bigival Length(const Vec3ival &v, const BigInt &inv_epsilon) {
  return Bigival::Sqrt(v.x.Squared() + v.y.Squared() + v.z.Squared(),
                       inv_epsilon);
}

inline Bigival Length(const Vec2ival &v, const BigInt &inv_epsilon) {
  return Bigival::Sqrt(v.x.Squared() + v.y.Squared(),
                       inv_epsilon);
}

inline Vec3ival Normalize(const Vec3ival &v, const BigInt &inv_epsilon) {
  Bigival len = Length(v, inv_epsilon);
  CHECK(!len.ContainsZero()) << "Can't normalize if the length might be "
    "zero. v was: " << v.ToString();
  return Vec3ival(
      v.x / len,
      v.y / len,
      v.z / len);
}

inline Vec3ival operator +(const Vec3ival &a,
                           const Vec3ival &b) {
  return Vec3ival(a.x + b.x, a.y + b.y, a.z + b.z);
}

inline Vec3ival operator *(const Vec3ival &a,
                           const Vec3ival &b) {
  return Vec3ival(a.x * b.x, a.y * b.y, a.z * b.z);
}

inline Vec3ival operator *(const Vec3ival &a,
                           const BigVec3 &b) {
  return Vec3ival(a.x * b.x, a.y * b.y, a.z * b.z);
}

inline Vec3ival operator *(const Vec3ival &a,
                           const BigRat &s) {
  return Vec3ival(a.x * s, a.y * s, a.z * s);
}

inline Vec2ival operator -(const Vec2ival &a,
                           const BigVec2 &b) {
  return Vec2ival(a.x - b.x, a.y - b.y);
}

// Returns true if the disc and axis-aligned bounding box may overlap.
// This is guaranteed to be true if they do overlap. It can have false
// positives if the intervals are wide.
bool MightOverlap(const Discival &disc, const Vec2ival &aabb);

// This can certainly work on Vec3ival, but our input data at this point
// is a single point and this is in inner loops.
inline Vec2ival TransformPointTo2D(const Frame3ival &frame,
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
Frame3ival FrameFromViewPos(const Vec3ival &view,
                            const BigInt &inv_epsilon);

// We commonly have a squared radius for a disc, and want to expand it
// by some error term (i.e. another radius) to account for something
// like trigonometric approximation. The error term is very small
// compared to the radius.
// We can compute (sqrt(radius^2) + error_term)^2 to get a new
// squared radius, but this involves an expensive square root.
//
// Here we use a different bound (radius_sq + error_term), which is
// good when the error term is very small. This is only correct when
// the radius is in a certain range, which the function tests (and
// uses the Euclidean approach if not). Roughly, the radius should be
// less than 1/2. This is typical for our problems.
BigRat ExpandSquaredRadius(const BigRat &radius_sq,
                           const BigRat &error_term,
                           const BigInt &inv_epsilon);

// Compute an upper bound for the squared diameter of a polygon.
// If the polygon is not convex, this is really computing the diameter
// of its convex hull (it's just the max distance between vertices).
BigRat MaxSquaredDiameter(const std::vector<Vec2ival> &vs);

#endif
