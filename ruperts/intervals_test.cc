
#include "intervals.h"

#include <cmath>
#include <cstdio>
#include <format>
#include <numbers>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "ansi.h"
#include "arcfour.h"
#include "auto-histo.h"
#include "base/logging.h"
#include "big-interval.h"
#include "big-polyhedra.h"
#include "bignum/big.h"
#include "periodically.h"
#include "polyhedra.h"
#include "randutil.h"
#include "stats.h"
#include "status-bar.h"
#include "timer.h"
#include "yocto_matht.h"

using vec2 = yocto::vec<double, 2>;

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

// Helper to sample a random BigRat from an interval.
static BigRat RandomBigRat(ArcFour *rc, const Bigival &ival) {
  if (ival.Singular()) return ival.LB();
  // Maybe should be more careful about endpoints.
  BigRat t = BigRat::FromDouble(RandDouble(rc)) * ival.Width();
  return ival.LB() + t;
}

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

    CHECK(result.Width() <= naive_result.Width())
      << "Optimized width " << result.Width().ToString()
      << " should be no worse than native width " <<
      naive_result.Width().ToString();


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


template<class F>
static void TestGetBoundingAABB(std::string_view name,
                                const F &BoundingAABB) {
  BigInt inv_epsilon(100000);
  BigInt inv_epsilon_high("1000000000000000000000");
  Bigival pi = Bigival::Pi(inv_epsilon);

  // Computes the transformation for concrete points in the
  // intervals, using high precision rationals. The test AABB
  // must overlap these tight sampled AABBs.
  auto CheckAllSamples = [&](
      const Vec2ival &v_in, const Bigival &angle,
      const Bigival &tx, const Bigival &ty) {

    RotTrig rot_trig(angle, inv_epsilon);
    Vec2ival result = BoundingAABB(v_in, rot_trig, inv_epsilon, tx, ty);

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
    Vec2ival result = BoundingAABB(v_in, rot_trig, inv_epsilon, tx, ty);
    // For a singular angle of 0, sin and cos are exact.
    CHECK(rot_trig.cos_a.Singular() && rot_trig.cos_a.LB() == 1);
    CHECK(rot_trig.sin_a.Singular() && rot_trig.sin_a.LB() == 0);
    // So result should be exactly v_in.
    CHECK(result.x.LB() == v_in.x.LB() && result.x.UB() == v_in.x.UB());
    CHECK(result.y.LB() == v_in.y.LB() && result.y.UB() == v_in.y.UB());
  }

  // ~90 degree rotation.
  {
    Bigival angle(pi.Midpoint() / 2);
    Vec2ival v_in(Bigival(1, 2, true, true), Bigival(3, 4, true, true));
    Bigival tx(0), ty(0);
    CheckAllSamples(v_in, angle, tx, ty);

    // Expected is x' = -y, y' = x
    // x' in [-4, -3], y' in [1, 2]
    RotTrig rot_trig(angle, inv_epsilon);
    Vec2ival result = BoundingAABB(v_in, rot_trig, inv_epsilon, tx, ty);
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
    Bigival angle(BigRat(0), pi.Midpoint() / 2, true, true);
    Vec2ival v_in(Bigival(1, 2, true, true), Bigival(1, 2, true, true));
    Bigival tx(0), ty(0);
    CheckAllSamples(v_in, angle, tx, ty);
  }

  // Interval rotation and translation.
  {
    Bigival angle(BigRat(0), pi.Midpoint() / 2, true, true);
    Vec2ival v_in(Bigival(1, 2, true, true), Bigival(1, 2, true, true));
    Bigival tx(-1, 1, true, true), ty(-1, 1, true, true);
    CheckAllSamples(v_in, angle, tx, ty);
  }

  // AABB contains origin.
  {
    Bigival angle(BigRat(0), pi.Midpoint() / 2, true, true);
    Vec2ival v_in(Bigival(-1, 1, true, true), Bigival(-1, 1, true, true));
    Bigival tx(-1, 1, true, true), ty(0, 1, true, true);
    CheckAllSamples(v_in, angle, tx, ty);
  }

  // Large rotation.
  {
    Bigival almost_two_pi = pi * BigRat(31, 16);
    Bigival angle(BigRat(0), almost_two_pi.Midpoint(), true, true);
    Vec2ival v_in(Bigival(-1, 1, true, true), Bigival(0, 2, true, true));
    Bigival tx(-1, 1, true, true), ty(-1, 0, true, true);
    CheckAllSamples(v_in, angle, tx, ty);
  }

  // Open intervals.
  {
    Vec2ival v_in(Bigival(1, 2, false, true), Bigival(3, 4, true, false));
    Bigival angle(BigRat(0), pi.Midpoint() / 2, false, true);
    Bigival tx(5, 6, true, false), ty(7, 8, true, true);
    CheckAllSamples(v_in, angle, tx, ty);
  }

  // Rotation straddling the y-axis.
  {
    BigRat pi_over_4 = pi.Midpoint() / 4;
    Bigival angle(-pi_over_4, pi_over_4, true, true);
    Vec2ival v_in(Bigival(1, 2, true, true), Bigival(1, 2, true, true));
    Bigival tx(0), ty(0);
    CheckAllSamples(v_in, angle, tx, ty);
  }

  // AABB with corner on an axis.
  {
    Bigival pi = Bigival::Pi(inv_epsilon);
    Bigival angle(BigRat(0), pi.Midpoint() / 2, true, true);
    Vec2ival v_in(Bigival(0, 1, true, true), Bigival(1, 2, true, true));
    Bigival tx(0), ty(0);
    CheckAllSamples(v_in, angle, tx, ty);
  }

  printf("BoundingAABB tests (%s) OK\n", std::string(name).c_str());
}




// Benchmark the bounding AABB approach. This does compute the
// speed, but primarily we're interested in how well it bounds the
// actual swept shape. The efficiency is the ratio of the area of
// the convex hull of the sample (lower bound on the actual swept shape)
// to the AABB's area.
template<class F1, class F2>
static void BenchAABB(const F1 &f1,
                      const F2 &f2) {
  ArcFour rc("bench");
  const BigInt inv_epsilon(100000);
  const BigInt inv_epsilon_high("1000000000000000000000");

  // Samples points from the swept shape.
  auto SampleSweptShape = [&](
      const Vec2ival &v_in, const Bigival &angle,
      const Bigival &tx, const Bigival &ty,
      int n_samples) -> std::vector<vec2> {

    std::vector<vec2> points;
    points.reserve(n_samples);
    for (int i = 0; i < n_samples; i++) {
      BigRat p_in_x = RandomBigRat(&rc, v_in.x);
      BigRat p_in_y = RandomBigRat(&rc, v_in.y);
      BigRat a = RandomBigRat(&rc, angle);
      BigRat trans_x = RandomBigRat(&rc, tx);
      BigRat trans_y = RandomBigRat(&rc, ty);

      // Use high precision sin/cos for accurate sample points
      BigRat s = Bigival::Sin(a, inv_epsilon_high).Midpoint();
      BigRat c = Bigival::Cos(a, inv_epsilon_high).Midpoint();

      BigRat res_x = p_in_x * c - p_in_y * s + trans_x;
      BigRat res_y = p_in_x * s + p_in_y * c + trans_y;

      points.push_back({res_x.ToDouble(), res_y.ToDouble()});
    }
    return points;
  };


  auto RunOne = [&](const Vec2ival &v_in,
                    const Bigival &angle,
                    const Vec2ival &trans) ->
    std::tuple<Vec2ival, double, double,
               Vec2ival, double, double> {

    double time1 = 0.0, time2 = 0.0;
    Vec2ival aabb1, aabb2;

    RotTrig rot_trig(angle, inv_epsilon);

    // Perform them in a random order so that we don't
    // benefit from cache effects, etc.
    int order = rc.Byte() & 1;
    for (int i = 0; i < 2; i++) {
      Timer timer;
      if (i == order) {
        aabb1 = f1(v_in, rot_trig, inv_epsilon,
                   trans.x, trans.y);
        time1 = timer.Seconds();
      } else {
        aabb2 = f2(v_in, rot_trig, inv_epsilon,
                   trans.x, trans.y);
        time2 = timer.Seconds();
      }
    }

    constexpr int n_samples = 1024;
    std::vector<vec2> points =
      SampleSweptShape(v_in, angle, trans.x, trans.y, n_samples);

    for (const vec2 &v : points) {
      // Failure here would be surprising, but it is technically
      // possible (double samples are not necessarily contained
      // in the mathematical intervals).
      CHECK(aabb2.Contains(BigVec2(BigRat::FromDouble(v.x),
                                   BigRat::FromDouble(v.y)))) << "(1) "
        "Note that this could fail simply due to the floating point "
        "approximations being wrong.";

      CHECK(aabb2.Contains(BigVec2(BigRat::FromDouble(v.x),
                                   BigRat::FromDouble(v.y)))) << "(2) "
        "Note that this could fail simply due to the floating point "
        "approximations being wrong.";
    }


    std::vector<int> hull_indices = QuickHull(points);
    double hull_area = AreaOfHull(points, hull_indices);

    double aabb1_area = aabb1.Area().ToDouble();
    double aabb2_area = aabb2.Area().ToDouble();

    return std::make_tuple(
        aabb1,
        aabb1_area > 1.0e-12 ? hull_area / aabb1_area : 1.0,
        time1,
        aabb2,
        aabb2_area > 1.0e-12 ? hull_area / aabb2_area : 1.0,
        time2);
    };

  // Benchmark on deterministically random intervals.
  static constexpr int NUM_BENCH = 16384;
  StatusBar status(1);
  Periodically status_per(1.0);

  std::vector<double> eff_ratio;
  std::vector<double> time_diff;

  for (int i = 0; i < NUM_BENCH; i++) {

    // Make a random AABB near the unit circle.

    Vec2ival pt_in = [&](){
        double prad = RandDouble(&rc) * std::numbers::pi;
        double xc = std::cos(prad);
        double yc = std::sin(prad);

        double dx1 = RandDouble(&rc) * 0.1 - 0.05;
        double dy1 = RandDouble(&rc) * 0.1 - 0.05;
        double dx2 = RandDouble(&rc) * 0.1 - 0.05;
        double dy2 = RandDouble(&rc) * 0.1 - 0.05;

        Bigival x = Bigival::Union(Bigival(BigRat::FromDouble(xc + dx1)),
                                   Bigival(BigRat::FromDouble(xc + dx2)));
        Bigival y = Bigival::Union(Bigival(BigRat::FromDouble(yc + dy1)),
                                   Bigival(BigRat::FromDouble(yc + dy2)));
        return Vec2ival(x, y);
      }();

    // Mostly interested in small angles.
    Bigival rot = [&](){
        BigRat c = BigRat::FromDouble(RandDouble(&rc) * std::numbers::pi);

        BigRat w = BigRat::FromDouble(RandDouble(&rc) * 0.005);

        return Bigival(c - w, c + w, true, true);
      }();

    // Mostly interested in translation near the origin.
    Vec2ival trans = [&](){

        double x = RandDouble(&rc) * 0.05 - 0.025;
        double y = RandDouble(&rc) * 0.05 - 0.025;

        double wx = RandDouble(&rc) * 0.05;
        double wy = RandDouble(&rc) * 0.05;

        return Vec2ival(Bigival(BigRat::FromDouble(x - wx),
                                BigRat::FromDouble(x + wx),
                                false, false),
                        Bigival(BigRat::FromDouble(y - wy),
                                BigRat::FromDouble(y + wy),
                                false, false));
      }();

    const auto &[aabb1, eff1, sec1, aabb2, eff2, sec2] =
      RunOne(pt_in, rot, trans);
    eff_ratio.push_back(eff2 / eff1);
    time_diff.push_back(sec2 - sec1);

    status_per.RunIf([&]() {

        status.Print("Eff: " ARED("{:.4f}%") " vs " AGREEN("{:.4f}%")
                     " ={:.3f}"
                     ". Time {} vs {}\n",
                     eff1 * 100.0, eff2 * 100.0,
                     eff2 / eff1,
                     ANSI::Time(sec1), ANSI::Time(sec2));
        status.Progress(i, NUM_BENCH, "bench");
      });

    auto ShortIval = [](const Bigival &iv) {
        return std::format("[{:.4f},{:.4f}]",
                           iv.LB().ToDouble(),
                           iv.UB().ToDouble());
      };

    if (eff2 < eff1) {
      status.Print("?? pt x: {} y: {}, angle: {}, tx: {} ty: {}\n"
                   "AABB1: x: {} y: {} area: {:.4f}\n"
                   "AABB2: x: {} y: {} area: {:.4f}\n"
                   ,
                   ShortIval(pt_in.x), ShortIval(pt_in.y),
                   ShortIval(rot),
                   ShortIval(trans.x), ShortIval(trans.y),

                   ShortIval(aabb1.x), ShortIval(aabb1.y),
                   aabb1.Area().ToDouble(),
                   ShortIval(aabb2.x), ShortIval(aabb2.y),
                   aabb2.Area().ToDouble()
                   );
    }
  }

  AutoHisto eff_histo;
  for (double e : eff_ratio) eff_histo.Observe(e);
  printf("%s\n", eff_histo.SimpleANSI(20).c_str());

  printf("\nTime:\n");
  AutoHisto time_histo;
  for (double e : time_diff) time_histo.Observe(e);
  printf("%s\n", time_histo.SimpleANSI(20).c_str());
  printf("\n");

  Stats::Gaussian eff_stats = Stats::EstimateGaussian(eff_ratio);
  Stats::Gaussian time_stats = Stats::EstimateGaussian(time_diff);

  printf("%s\n",
         std::format("new / old efficiency: {:.4f} ± {:.4f}",
                     eff_stats.mean, eff_stats.PlusMinus95()).c_str());

  std::string sign;
  double t = time_stats.mean;
  if (t < 0.0) {
    sign = ARED("-");
    t = -t;
  } else {
    sign = AGREEN("+");
  }

  printf("%s\n",
         std::format("new - old time: {}{} ± {}",
                     sign,
                     ANSI::Time(t),
                     ANSI::Time(time_stats.PlusMinus95())).c_str());
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
  TestGetBoundingAABB("GetBoundingAABB", &GetBoundingAABB);
  TestGetBoundingAABB("GetBoundingAABB2", &GetBoundingAABB2);
  TestExpandSquaredRadius();

  BenchAABB(&GetBoundingAABB, &GetBoundingAABB2);

  printf("OK\n");
  return 0;
}
