
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
    Bigival pi = Bigival::Pi(inv_epsilon) * BigRat(15, 16);
    Bigival angle(BigRat(0), pi.Midpoint() / 2, true, true);
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


int main(int argc, char **argv) {
  ANSI::Init();
  printf("\n");

  TestGetBoundingAABB();

  printf("OK\n");
  return 0;
}
