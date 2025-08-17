
#include "big-polyhedra.h"
#include "intervals.h"

#include <cstdio>

#include "base/logging.h"
#include "ansi.h"
#include "big-interval.h"
#include "bignum/big.h"

#define CHECK_CONTAINS_VEC2(aabb_exp, v_exp) do {               \
  auto aabb = (aabb_exp);                                       \
  auto v = (v_exp);                                             \
  CHECK(aabb.Contains(v)) << "Expected the AABB " #aabb_exp     \
    " to contain the value " #v_exp << ":\n"                    \
    "aabb: " << aabb.ToString() << "\nvalue: " <<               \
    VecString(v);                                               \
 } while (0)

#define CHECK_OVERLAPS_AABB(a_exp, b_exp) do {                    \
  auto a = (a_exp);                                               \
  auto b = (b_exp);                                               \
  CHECK(Bigival::MaybeIntersection(a.x, b.x).has_value() &&       \
        Bigival::MaybeIntersection(a.y, b.y).has_value())         \
    << "Expected the AABBs to overlap:\n" #a_exp ": "             \
    << a.ToString() << "\n" #b_exp ": " << b.ToString();          \
 } while (0)

// Sample some points in the interval.
template<class F>
static void Sample(const Bigival &v, const F &f) {
  // Completely specified; just one point to test.
  if (v.LB() == v.UB()) {
    CHECK(v.IncludesLB() && v.IncludesUB());
    f(v.LB());
    return;
  }

  BigRat epsilon = v.Width() / 1024;
  CHECK(BigRat::Sign(epsilon) == 1);

  if (v.IncludesLB()) {
    f(v.LB());
  } else {
    f(v.LB() + epsilon);
  }

  if (v.IncludesUB()) {
    f(v.UB());
  } else {
    f(v.UB() - epsilon);
  }

  BigRat zero(0);
  if (v.Contains(zero) && v.LB() != zero && v.UB() != zero) {
    f(zero);
  } else {
    // midpoint
    f((v.LB() + v.UB()) / BigRat(2));
  }
}

// Sample some points in the 2D interval.
template<class F>
static void Sample(const Vec2ival &v, const F &f) {
  Sample(v.x, [&](const BigRat &x_sample){
    Sample(v.y, [&](const BigRat &y_sample){
      f(BigVec2(x_sample, y_sample));
    });
  });
}

static void TestDotProductWithView() {
  BigInt inv_epsilon(1000000);
  BigInt inv_epsilon_high("1000000000000000000000");

  auto Check = [&](const Bigival &azimuth, const Bigival &angle,
                   const BigVec3 &v) {
    ViewBoundsTrig trig(azimuth, angle, inv_epsilon);

    Bigival result = DotProductWithView(trig, v);

    Vec3ival viewpos_ival = ViewFromSpherical(trig);
    Bigival naive_result = Dot(viewpos_ival, v);

    // The optimized result should be a sub-interval of the naive one.
    // (Or else we could be intersecting them for better bounds.)
    CHECK(result.LB() >= naive_result.LB())
      << "Optimized LB " << result.LB().ToString()
      << " should be >= naive LB " << naive_result.LB().ToString();
    CHECK(result.UB() <= naive_result.UB())
      << "Optimized UB " << result.UB().ToString()
      << " should be <= naive UB " << naive_result.UB().ToString();

    // For non-singular angle intervals, it should be strictly tighter because
    // of the dependency on sin(angle) in the naive version.
    if (!angle.Singular()) {
      CHECK(result.Width() < naive_result.Width())
        << "Optimized width " << result.Width().ToString()
        << " should be < naive width " << naive_result.Width().ToString();
    }

    Sample(azimuth, [&](const BigRat &az_sample) {
      Sample(angle, [&](const BigRat &an_sample) {
          Bigival sin_az_ival = Bigival::Sin(az_sample, inv_epsilon_high);
          Bigival cos_az_ival = Bigival::Cos(az_sample, inv_epsilon_high);
          Bigival sin_an_ival = Bigival::Sin(an_sample, inv_epsilon_high);
          Bigival cos_an_ival = Bigival::Cos(an_sample, inv_epsilon_high);

          // This is a tight interval around the exact dot product
          // for the sample point.
          Bigival dot_sample_ival =
            sin_an_ival * (cos_az_ival * v.x + sin_az_ival * v.y) +
            cos_an_ival * v.z;

          // The computed interval must contain the sample's interval.
          CHECK(result.Contains(dot_sample_ival.Midpoint()))
            << "Optimized result " << result.ToString()
            << " does not contain sample point "
            << dot_sample_ival.Midpoint().ToDouble();
          CHECK(naive_result.Contains(dot_sample_ival.Midpoint()))
            << "Naive result " << naive_result.ToString()
            << " does not contain sample point "
            << dot_sample_ival.Midpoint().ToDouble();
        });
    });
  };

  // A typical small interval.
  {
    Bigival azimuth(BigRat(1, 10), BigRat(2, 10), true, true);
    Bigival angle(BigRat(3, 10), BigRat(4, 10), true, true);
    BigVec3 v(BigRat(-1), BigRat(2), BigRat(3));
    Check(azimuth, angle, v);
  }

  // Intervals crossing zero.
  {
    Bigival azimuth(BigRat(-1, 10), BigRat(1, 10), true, true);
    Bigival angle(BigRat(-2, 10), BigRat(2, 10), true, true);
    BigVec3 v(BigRat(-5), BigRat(8), BigRat(-1));
    Check(azimuth, angle, v);
  }

  // Singular angle.
  {
    Bigival azimuth(BigRat(1, 10), BigRat(2, 10), true, true);
    Bigival angle(BigRat(3, 10));
    BigVec3 v(BigRat(1), BigRat(2), BigRat(3));

    ViewBoundsTrig trig(azimuth, angle, inv_epsilon);
    Bigival result = DotProductWithView(trig, v);
    Bigival naive_result = Dot(ViewFromSpherical(trig), v);
    // Results should be very close for singular inputs, because
    // the dependency problem should not really arise.
    CHECK(BigRat::Abs(result.Width() - naive_result.Width()) <
          BigRat(BigInt(10), inv_epsilon));
  }

  // Singular azimuth. Still has dependency on angle.
  {
    Bigival azimuth(BigRat(1, 10));
    Bigival angle(BigRat(3, 10), BigRat(4, 10), true, true);
    BigVec3 v(BigRat(1), BigRat(2), BigRat(3));
    Check(azimuth, angle, v);
  }

  printf("DotProductWithView OK\n");
}


static void TestGetBoundingAABB() {
  BigInt inv_epsilon(100000);
  BigInt inv_epsilon_high("1000000000000000000000");

  // Computes the transformation for concrete points in the
  // intervals, using high precision rationals. The test AABB
  // must overlap these tight sampled AABBs.
  auto CheckAllSamples = [&](
      const Vec2ival &v_in, const Bigival &angle,
      const Bigival &tx, const Bigival &ty) {

    RotTrig rot_trig(angle, inv_epsilon);
    Vec2ival result = GetBoundingAABB(v_in, rot_trig, inv_epsilon, tx, ty);

    Sample(v_in, [&](const BigVec2 &p_in) {
        Sample(angle, [&](const BigRat &a) {
            Bigival s = Bigival::Sin(a, inv_epsilon_high);
            Bigival c = Bigival::Cos(a, inv_epsilon_high);

            Sample(tx, [&](const BigRat &trans_x) {
                Sample(ty, [&](const BigRat &trans_y) {

                    // Tight AABB for the sampled point.
                    Vec2ival pt(
                        p_in.x * c - p_in.y * s + trans_x,
                        p_in.x * s + p_in.y * c + trans_y);

                    CHECK_OVERLAPS_AABB(result, pt);
                  });
              });
          });
      });
    };

  // Singular identity transformation.
  {
    Vec2ival v_in(Bigival(1, 2, false, false), Bigival(3, 4, false, false));
    Bigival angle(0);
    Bigival tx(0), ty(0);
    CheckAllSamples(v_in, angle, tx, ty);

    RotTrig rot_trig(angle, inv_epsilon);
    Vec2ival result = GetBoundingAABB(v_in, rot_trig, inv_epsilon, tx, ty);
    // For a singular angle of 0, sin and cos are exact.
    CHECK(rot_trig.cos_a.Singular() && rot_trig.cos_a.LB() == 1);
    CHECK(rot_trig.sin_a.Singular() && rot_trig.sin_a.LB() == 0);
    // So result should be exactly v_in.
    CHECK(result.x.LB() == v_in.x.LB() && result.x.UB() == v_in.x.UB());
    CHECK(result.y.LB() == v_in.y.LB() && result.y.UB() == v_in.y.UB());
  }

  // ~90 degree rotation.
  {
    Bigival pi = Bigival::Pi(inv_epsilon);
    Bigival angle(pi.Midpoint() / 2);
    Vec2ival v_in(Bigival(1, 2, true, true), Bigival(3, 4, true, true));
    Bigival tx(0), ty(0);
    CheckAllSamples(v_in, angle, tx, ty);

    // Expected is x' = -y, y' = x
    // x' in [-4, -3], y' in [1, 2]
    RotTrig rot_trig(angle, inv_epsilon);
    Vec2ival result = GetBoundingAABB(v_in, rot_trig, inv_epsilon, tx, ty);
    // For an ideal 90-degree rotation, the result would be [-4, -3] x [1, 2],
    // but we can't actually get a rational pi/2.
    // Instead check that the center is contained in the result.
    CHECK_CONTAINS_VEC2(result, BigVec2(BigRat(-7, 2), BigRat(3, 2)));
    // The result interval will have some width due to pi not being exact,
    // and sin/cos being approximations.
    CHECK(BigRat::Abs(result.x.Width()) - BigRat(1) < BigRat(1, 1024)) <<
      result.x.Width().ToString();
    CHECK(BigRat::Abs(result.y.Width()) - BigRat(1) < BigRat(1, 1024)) <<
      result.y.Width().ToString();
  }

  // Interval rotation [0, 90 deg].
  {
    Bigival pi = Bigival::Pi(inv_epsilon);
    Bigival angle(BigRat(0), pi.Midpoint() / 2, true, true);
    Vec2ival v_in(Bigival(1, 2, true, true), Bigival(1, 2, true, true));
    Bigival tx(0), ty(0);
    CheckAllSamples(v_in, angle, tx, ty);
  }

  // Interval rotation and translation.
  {
    Bigival pi = Bigival::Pi(inv_epsilon);
    Bigival angle(BigRat(0), pi.Midpoint() / 2, true, true);
    Vec2ival v_in(Bigival(1, 2, true, true), Bigival(1, 2, true, true));
    Bigival tx(-1, 1, true, true), ty(-1, 1, true, true);
    CheckAllSamples(v_in, angle, tx, ty);
  }

  // AABB contains origin.
  {
    Bigival pi = Bigival::Pi(inv_epsilon);
    Bigival angle(BigRat(0), pi.Midpoint() / 2, true, true);
    Vec2ival v_in(Bigival(-1, 1, true, true), Bigival(-1, 1, true, true));
    Bigival tx(-1, 1, true, true), ty(0, 1, true, true);
    CheckAllSamples(v_in, angle, tx, ty);
  }

  // Large rotation.
  {
    Bigival almost_two_pi = Bigival::Pi(inv_epsilon) * BigRat(31, 16);
    Bigival angle(BigRat(0), almost_two_pi.Midpoint(), true, true);
    Vec2ival v_in(Bigival(-1, 1, true, true), Bigival(0, 2, true, true));
    Bigival tx(-1, 1, true, true), ty(-1, 0, true, true);
    CheckAllSamples(v_in, angle, tx, ty);
  }

  // Open intervals
  {
    Bigival pi = Bigival::Pi(inv_epsilon);
    Vec2ival v_in(Bigival(1, 2, false, true), Bigival(3, 4, true, false));
    Bigival angle(BigRat(0), pi.Midpoint() / 2, false, true);
    Bigival tx(5, 6, true, false), ty(7, 8, true, true);
    CheckAllSamples(v_in, angle, tx, ty);
  }

  printf("GetBoundingAABB OK\n");
}

static void TestExpandSquaredRadius() {
  const BigInt inv_epsilon(1000000);

  auto GetExpected = [&](const BigRat &r_sq, const BigRat &e) {
    BigRat r = BigRat::SqrtBounds(r_sq, inv_epsilon).second;
    BigRat total_r = r + e;
    return total_r * total_r;
  };

  // Small radius and error, where the linear approximation is a safe
  // upper bound. 4*r^2 <= (1-e)^2. Here 4*(1/100) <= (1-1/1000)^2.
  {
    BigRat r_sq(1 / 100);
    BigRat e(1 / 1000);
    BigRat result = ExpandSquaredRadius(r_sq, e, inv_epsilon);
    BigRat expected = GetExpected(r_sq, e);
    CHECK(result >= expected);
    // Also check it used the fast path.
    CHECK(result == r_sq + e);
  }

  // Large radius, where the fast path condition fails.
  // 4*1 > (1-1/1000)^2.
  {
    BigRat r_sq(1);
    BigRat e(1, 1000);
    BigRat result = ExpandSquaredRadius(r_sq, e, inv_epsilon);
    BigRat expected = GetExpected(r_sq, e);
    // Slow path should be very close to the oracle.
    CHECK(result >= expected);
    CHECK(BigRat::Abs(result - expected) < BigRat(1, 100000));
  }

  // Zero radius.
  {
    BigRat r_sq(0);
    BigRat e(1, 10);
    BigRat result = ExpandSquaredRadius(r_sq, e, inv_epsilon);
    BigRat expected = GetExpected(r_sq, e);
    CHECK(result >= expected);
    CHECK(result == r_sq + e);
  }

  // Zero error term.
  {
    BigRat r_sq("1/25");
    BigRat e(0);
    BigRat result = ExpandSquaredRadius(r_sq, e, inv_epsilon);
    BigRat expected = GetExpected(r_sq, e);
    // Should be exact.
    CHECK(result == expected);
    CHECK(result == r_sq);
  }

  printf("ExpandSquaredRadius OK\n");
}

int main(int argc, char **argv) {
  ANSI::Init();
  printf("\n");

  TestDotProductWithView();
  TestGetBoundingAABB();
  TestExpandSquaredRadius();

  printf("OK\n");
  return 0;
}
