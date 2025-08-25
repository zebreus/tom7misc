
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

  std::string ToString() const;
};

// Vec2 where each component is an interval.
// This is an axis-aligned bounding box (AABB).
struct Vec2ival {
  Bigival x, y;
  Vec2ival(Bigival xx, Bigival yy) :
    x(std::move(xx)), y(std::move(yy)) {}
  Vec2ival(BigVec2 v) : x(std::move(v.x)), y(std::move(v.y)) {}
  Vec2ival() : x(0), y(0) {}

  // The exact area of the AABB.
  BigRat Area() const {
    return x.Width() * y.Width();
  }

  std::string ToString() const;

  bool Contains(const BigVec2 &v) const {
    return x.Contains(v.x) && y.Contains(v.y);
  }

  static Vec2ival Union(const Vec2ival &a,
                        const Vec2ival &b) {
    return Vec2ival(Bigival::Union(a.x, b.x),
                    Bigival::Union(a.y, b.y));
  }
};

// Small fixed-size matrices stored in column major format.
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

inline Vec2ival operator +(const Vec2ival &a,
                           const Vec2ival &b) {
  return Vec2ival(a.x + b.x, a.y + b.y);
}

inline Vec2ival operator -(const Vec2ival &a,
                           const BigVec2 &b) {
  return Vec2ival(a.x - b.x, a.y - b.y);
}

struct SinCos {
  Bigival sine;
  Bigival cosine;
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
    az.sine = azimuth.NiceSin(inv_epsilon);
    az.cosine = azimuth.NiceCos(inv_epsilon);

    an.sine = angle.NiceSin(inv_epsilon);
    an.cosine = angle.NiceCos(inv_epsilon);

    // We use these for the calculation of the patch's bounding ball.
    mid_azimuth = azimuth.Midpoint();
    mid_angle = angle.Midpoint();

    // Consider NiceSin/NiceCos here. But we just use this once.
    mid_sin_az = Bigival::Sin(mid_azimuth, inv_epsilon);
    mid_cos_az = Bigival::Cos(mid_azimuth, inv_epsilon);
    mid_sin_an = Bigival::Sin(mid_angle, inv_epsilon);
    mid_cos_an = Bigival::Cos(mid_angle, inv_epsilon);
  }

  Bigival azimuth;
  Bigival angle;

  BigInt inv_epsilon;

  SinCos az;
  SinCos an;

  // Middle of the interval.
  BigRat mid_azimuth;
  BigRat mid_angle;

  // XXX SinCos
  Bigival mid_sin_az;
  Bigival mid_cos_az;
  Bigival mid_sin_an;
  Bigival mid_cos_an;
};

// Precomputed trigonometry for the 2D rotation of the inner hull.
// Includes the sin and cosine of the endpoints and midpoint.
struct RotTrig {
  // If true, then we use the "Nice" versions of sine and cosine.
  static constexpr bool USE_NICE = true;

  Bigival PrecomputeCosI(const Bigival &a, const BigInt &inv_epsilon) {
    if constexpr (USE_NICE) {
      return a.NiceCos(inv_epsilon);
    } else {
      return a.Cos(inv_epsilon);
    }
  }

  Bigival PrecomputeSinI(const Bigival &a, const BigInt &inv_epsilon) {
    if constexpr (USE_NICE) {
      return a.NiceSin(inv_epsilon);
    } else {
      return a.Sin(inv_epsilon);
    }
  }


  Bigival PrecomputeCos(const BigRat &a, const BigInt &inv_epsilon) {
    if constexpr (USE_NICE) {
      return Bigival::NiceCos(a, inv_epsilon);
    } else {
      return Bigival::Cos(a, inv_epsilon);
    }
  }

  Bigival PrecomputeSin(const BigRat &a, const BigInt &inv_epsilon) {
    if constexpr (USE_NICE) {
      return Bigival::NiceSin(a, inv_epsilon);
    } else {
      return Bigival::Sin(a, inv_epsilon);
    }
  }

  RotTrig(Bigival angle_in,
          BigInt inv_epsilon_in) :
    angle(std::move(angle_in)),
    inv_epsilon(std::move(inv_epsilon_in)) {

    // Get high quality intervals since these are used many
    // times.
    cos_a = PrecomputeCosI(angle, inv_epsilon);
    sin_a = PrecomputeSinI(angle, inv_epsilon);

    // We use the middle of the angle for our disc
    // centers, so precompute that too.
    mid_angle = angle.Midpoint();
    mid.cosine = PrecomputeCos(mid_angle, inv_epsilon);
    mid.sine = PrecomputeSin(mid_angle, inv_epsilon);

    lower.cosine = PrecomputeCos(angle.LB(), inv_epsilon);
    lower.sine = PrecomputeSin(angle.LB(), inv_epsilon);

    upper.cosine = PrecomputeCos(angle.UB(), inv_epsilon);
    upper.sine = PrecomputeSin(angle.UB(), inv_epsilon);
  }

  Bigival angle;
  Bigival cos_a, sin_a;

  BigRat mid_angle;
  SinCos mid;

  // Point estimates for the sin/cos of the lower and upper bounds,
  // respectively.
  SinCos lower, upper;

  BigInt inv_epsilon;
};

// Compute the view vector from azimuth/angle. If you are going
// to take a dot product, use DotProductWithView.
Vec3ival ViewFromSpherical(const ViewBoundsTrig &trig);

// Like Dot(ViewFromSpherical(azimuth, angle), v) but gives
// tighter bounds. Precondition: The angle intervals must both
// be less than 3 radians.
Bigival DotProductWithView(const ViewBoundsTrig &trig,
                           const BigVec3 &v);

// Compute a bounding ball for the patch on the unit sphere
// given by the azimuth and angle (this is the view position).
// The patch must be smaller than a hemisphere or you will
// get a degenerate (but correct) result.
Ballival SphericalPatchBall(const ViewBoundsTrig &trig,
                            const BigInt &inv_epsilon);

// Rotate the point v_in by rot and translate it by tx,ty.
// Since v_in is an AABB and rot is an interval, the resulting
// shape here is a rectangle swept along a circular arc. We
// will call this the "swept shape." This returns an AABB for
// the swept shape.
Vec2ival GetBoundingAABB(const Vec2ival &v_in,
                         const RotTrig &rot_trig,
                         const BigInt &inv_epsilon,
                         const Bigival &tx, const Bigival &ty);

// More accurate (and more complicated) version of the above.
// This one reduces dependency problems and has a few percentage
// points better AABB efficiency. On the other hand, it takes
// about 3x as long.
Vec2ival GetBoundingAABB2(const Vec2ival &v_in,
                          const RotTrig &rot_trig,
                          const BigInt &inv_epsilon,
                          const Bigival &tx, const Bigival &ty);

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

// Like TransformPointTo2D(
//    FrameFromViewPos(ViewFromSpherical(azimuth, angle)), v)
// but producing a tighter AABB.
inline Vec2ival TransformVec(const ViewBoundsTrig &trig,
                             const BigVec3 &v) {
  // x = dot(v, view_frame_x_axis)
  // view_frame_x_axis = (-sin(az), cos(az), 0)
  Bigival px = trig.az.sine * -v.x + trig.az.cosine * v.y;

  // y = dot(v, view_frame_y_axis)
  // view_frame_y_axis = (-cos(an)cos(az), -cos(an)sin(az), sin(an))
  Bigival py = -trig.an.cosine * (trig.az.cosine * v.x + trig.az.sine * v.y) +
    trig.an.sine * v.z;

  return Vec2ival(std::move(px), std::move(py));
}

// view pos must be unit length, and moreover, the origin can't be
// included (or approached) in the interval. Careful: The view
// positions are naturally unit length, but their interval
// approximations (AABBs) can easily include the origin if the angles
// subtended are more than a hemisphere, for example.
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

// Rotates a disc by an angle interval, producing a new, larger disc that
// bounds the entire swept shape. There are many choices of bounding disc;
// this code uses one that is biased away from the origin, and this
// usually produces a disc that is much bigger than it needs to be.
// But for the purpose of proving the parameterized point is on the outside of
// the edge, we want to minimize the error on the *inside* and don't really
// care about the outside. (If the disc gets *too* big then it might
// intersect the edge somewhere else, so we don't go crazy here.)
// See rotate-disc-inner-bias.png.
//
// To simplify the math, the angle interval's width must be reasonable (less
// than 3) or the resulting disc will be very conservative.
Discival RotateDiscInnerBias(
    // Mutable because we might calculate and cache radius.
    Discival *disc,
    const RotTrig &rot_trig,
    // A factor > 1 pushes the center away from the origin
    // to create a tighter inner bound. 1.0 is unbiased.
    const BigRat &bias,
    const BigInt &inv_epsilon);

// Translates a disc by an interval (tx, ty), producing a new, larger disc
// that bounds the entire resulting shape (a roundrect).
Discival TranslateDisc(Discival *disc,
                       const Bigival &tx,
                       const Bigival &ty,
                       const BigInt &inv_epsilon);

// Check if a disc is guaranteed to be strictly on the "outside" of an
// edge. "Outside" means the side of the line that doesn't contain the
// origin. Edge must be ordered (Cartesian) CCW.
bool IsDiscOutsideEdge(const Discival &disc,
                       const Vec2ival &outer_edge,
                       const Bigival &outer_cross_va_vb);

// Same, but with the disc center expresed as an AABB.
bool IsDiscOutsideEdge(const Vec2ival &disc_center,
                       const BigRat &disc_radius_sq,
                       const Vec2ival &outer_edge,
                       const Bigival &outer_cross_va_vb);


// Experimental; incomplete.
//
// Computes a bounding disc for an original exact 3D point (v), when
// viewed from anywhere in view position (trig; given by the
// azimuth/angle intervals) and projected to 2D along the z-axis.
//
// This uses the precomputed squared smear radius, which does not
// depend on the vertex. It's the squared radius of a ball centered
// on the unit sphere that encloses the entire azimuth/angle patch.
Discival GetInitialDisc(const BigVec3 &v,
                        const ViewBoundsTrig &trig,
                        const BigRat &smear_radius_sq,
                        const BigInt &inv_epsilon);

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
                     const BigInt &inv_epsilon);

// The vector between distinct hull points (in 3D). Order is
// arbitrary here, but each pair only appears once.
struct Chord3D {
  // Indices into hull.
  int start = 0, end = 0;
  // va - vb
  BigVec3 vec;
  BigRat length_sq;
};

// Compute bounds on the (squared) diameter of the shadow using
// the precomputed 3D chords. This is the maximum distance between
// any two vertices. This should give tighter bounds than simply
// computing distance from projected AABBs, since the points move in
// tandem.
Bigival SquaredDiameterFromChords(const std::vector<Chord3D> &chords,
                                  const ViewBoundsTrig &trig);

#endif
