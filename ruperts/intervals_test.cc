
#include "intervals.h"

#include <cmath>
#include <cstdio>
#include <format>
#include <initializer_list>
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
#include "base/print.h"
#include "big-interval.h"
#include "big-polyhedra.h"
#include "bignum/big.h"
#include "patches.h"
#include "periodically.h"
#include "polyhedra.h"
#include "randutil.h"
#include "stats.h"
#include "status-bar.h"
#include "timer.h"
#include "yocto_matht.h"

using vec2 = yocto::vec<double, 2>;

// In this code we often sample a value and verify that it is
// within the interval (or bounding box or ball) that a function
// computes. The sample is often not exact (because of e.g.
// trigonometric functions that aren't rational) and so our samples
// are tight intervals. All we know is that the true value is
// somewhere in this interval, so we usually check over *overlap*
// between the tight sample interval and the function under test's
// result.

#define CHECK_CONTAINS_VEC2(aabb_exp, v_exp) do {               \
  auto aabb = (aabb_exp);                                       \
  auto v = (v_exp);                                             \
  CHECK(aabb.Contains(v)) << "Expected the AABB " #aabb_exp     \
    " to contain the value " #v_exp << ":\n"                    \
    "aabb: " << aabb.ToString() << "\nvalue: " <<               \
    VecString(v);                                               \
 } while (0)

// 1D overlap.
#define CHECK_OVERLAP(a_exp, b_exp) do {                          \
  Bigival a = (a_exp);                                            \
  Bigival b = (b_exp);                                            \
  CHECK(Bigival::MaybeIntersection(a, b).has_value())             \
    << "Expected the intervals to overlap:\n" #a_exp ": "         \
    << a.ToString() << "\n" #b_exp ": " << b.ToString();          \
 } while (0)

// 2D overlap.
#define CHECK_OVERLAP_2D(a_exp, b_exp) do {                       \
  Vec2ival a = (a_exp);                                           \
  Vec2ival b = (b_exp);                                           \
  const bool x_overlap =                                          \
    Bigival::MaybeIntersection(a.x, b.x).has_value();             \
  const bool y_overlap =                                          \
    Bigival::MaybeIntersection(a.y, b.y).has_value();             \
  CHECK(x_overlap && y_overlap)                                   \
    << "Expected the AABBs to overlap:\n" #a_exp ": "             \
    << a.ToString() << "\n" #b_exp ": " << b.ToString()           \
    << "\nx overlap: " << (x_overlap ? "true" : "false")          \
    << "\ny overlap: " << (y_overlap ? "true" : "false");         \
  } while (0)

// 3D overlap.
#define CHECK_OVERLAP_3D(a_exp, b_exp) do {                       \
  Vec3ival a = (a_exp);                                           \
  Vec3ival b = (b_exp);                                           \
  CHECK(Bigival::MaybeIntersection(a.x, b.x).has_value() &&       \
        Bigival::MaybeIntersection(a.y, b.y).has_value() &&       \
        Bigival::MaybeIntersection(a.z, b.z).has_value())         \
    << "Expected the AABBs to overlap:\n" #a_exp ": "             \
    << a.ToString() << "\n" #b_exp ": " << b.ToString();          \
 } while (0)

// Helper to sample a random BigRat from an interval.
[[maybe_unused]]
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
        Bigival sin_az_ival = Bigival::Sin(az_sample, inv_epsilon_high);
        Bigival cos_az_ival = Bigival::Cos(az_sample, inv_epsilon_high);

        Sample(angle, [&](const BigRat &an_sample) {
          Bigival sin_an_ival = Bigival::Sin(an_sample, inv_epsilon_high);
          Bigival cos_an_ival = Bigival::Cos(an_sample, inv_epsilon_high);

          // This is a tight interval around the exact dot product
          // for the sample point.
          Bigival dot_sample_ival =
            sin_an_ival * (cos_az_ival * v.x + sin_az_ival * v.y) +
            cos_an_ival * v.z;

          // The true value must be in both intervals, so they must
          // overlap (probably the tiny sample interval is entirely
          // inside the result).
          CHECK_OVERLAP(result, dot_sample_ival);
          CHECK_OVERLAP(naive_result, dot_sample_ival);
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

  // A patch that crosses zero.
  {
    Bigival azimuth(BigRat(-1, 10), BigRat(1, 10), true, true);
    Bigival angle(BigRat(-2, 10), BigRat(2, 10), true, true);
    BigVec3 v(BigRat(5), BigRat(-8), BigRat(1));
    Check(azimuth, angle, v);
  }

  // A patch that crosses the prime meridian near the equator.
  {
    Bigival azimuth(BigRat(-1, 10), BigRat(1, 10), true, true);
    // Shifted angle away from the pole (z-axis).
    Bigival angle(BigRat(14, 10), BigRat(16, 10), true, true);
    BigVec3 v(BigRat(5), BigRat(-8), BigRat(1));
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
  // must overlap these tight sampled AABBs. (We do not check
  // for *containment* because the tight sampled AABBs have
  // their own uncertainty from bounding the trig functions.)
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

                    CHECK_OVERLAP_2D(result, pt);
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

  // Non-square AABB to test max radius logic.
  {
    Bigival angle(BigRat(0), pi.Midpoint() / 2, true, true);
    // Tall, narrow box. The corner (2, 10) has the largest radius.
    Vec2ival v_in(Bigival(1, 2, true, true), Bigival(1, 10, true, true));
    Bigival tx(0), ty(0);
    CheckAllSamples(v_in, angle, tx, ty);
  }

  // Rotation entirely within the second quadrant.
  {
    Bigival angle(pi.Midpoint() / 2, pi.Midpoint(), true, true);
    Vec2ival v_in(Bigival(1, 2, true, true), Bigival(1, 2, true, true));
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
  // TODO: Would improve accuracy to sample corners here.
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
  static constexpr int NUM_BENCH = 1024;
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

static void TestIsDiscOutsideEdge() {
  #define CHECK_DISC(d_in, va_in, vb_in, expected) do {         \
      const auto d_ = (d_in);                                   \
      const auto va_ = (va_in);                                 \
      const auto vb_ = (vb_in);                                 \
      Vec2ival edge(vb_.x - va_.x, vb_.y - va_.y);              \
      Bigival cross = Cross(va_, vb_);                          \
      CHECK(IsDiscOutsideEdge(d_, edge, cross) == expected)     \
        << "disc: " << d_.ToString() << "\n"                    \
        << "va: " << va_.ToString() << "\n"                     \
        << "vb: " << vb_.ToString() << "\n"                     \
        << "edge: " << edge.ToString() << "\n"                  \
        << "cross: " << cross.ToString() << "\n";               \
    } while (0)

  Discival disc(BigVec2(BigRat(5), BigRat(4)), BigRat(1));

  auto V2 = [](int x, int y) {
      return Vec2ival(BigVec2(BigRat(x), BigRat(y)));
    };

  // Vertical edge; disc is way outside.
  CHECK_DISC(disc, V2(3, 0), V2(3, 1), true);

  // Disc slightly overlaps a vertical edge.
  CHECK_DISC(disc,
             Vec2ival(Bigival(BigRat(9, 2)),
                      Bigival(BigRat(0))),
             Vec2ival(Bigival(BigRat(9, 2)),
                      Bigival(BigRat(1))), false);

  // Disc is exactly tangent to the edge, so it is not strictly
  // outside.
  CHECK_DISC(disc, V2(4, 0), V2(4, 1), false);

  // Disc center on inside.
  CHECK_DISC(disc, V2(6, 0), V2(6, 1), false);

  // Disc is way outside a diagonal edge.
  CHECK_DISC(disc, V2(4, 0), V2(0, 4), true);

  // Wide intervals.
  {
    // Remember the vector should point up for Cartesian CCW winding
    // and the disc to the right.
    Vec2ival va = Vec2ival(Bigival(BigRat(32, 10), BigRat(4), false, false),
                           Bigival(BigRat(13, 10), BigRat(13, 10), true, true));
    Vec2ival vb = Vec2ival(Bigival(BigRat(3), BigRat(42, 10), false, false),
                           Bigival(BigRat(6), BigRat(7), false, true));

    // Original disc is overlapping the uncertain region.
    CHECK_DISC(disc, va, vb, false);

    Discival far_disc(BigVec2(BigRat(85, 10), BigRat(4)), BigRat(3, 2));
    CHECK_DISC(far_disc, va, vb, true);
  }

  // Intervals are wide, but the disc is far enough away to pass.
  printf("IsDiscOutsideEdge tests OK\n");
}

static void TestTranslateDisc() {
  BigInt inv_epsilon(100000);

  // We'll divide by this, so we want an upper bound.
  BigRat sqrt2_ub = BigRat::SqrtBounds(BigRat(2), inv_epsilon).second;

  auto CheckAllSamples = [&](Discival disc_in,
                             const Bigival &tx, const Bigival &ty) {

      Discival result = TranslateDisc(&disc_in, tx, ty, inv_epsilon);

      // Sample points from the input disc to test against.
      const BigRat &radius = disc_in.Radius(inv_epsilon);
      BigVec2 dx = BigVec2(radius, BigRat(0));
      BigVec2 dy = BigVec2(BigRat(0), radius);

      std::vector<BigVec2> points_in_disc = {
        disc_in.center,
        disc_in.center + dx,
        disc_in.center - dx,
        disc_in.center + dy,
        disc_in.center - dy,
      };

      // And 4 diagonal points near the boundary.
      BigRat r_over_sqrt2 = radius / sqrt2_ub;
      points_in_disc.push_back(disc_in.center +
                               BigVec2(r_over_sqrt2, r_over_sqrt2));
      points_in_disc.push_back(disc_in.center +
                               BigVec2(r_over_sqrt2, -r_over_sqrt2));
      points_in_disc.push_back(disc_in.center +
                               BigVec2(-r_over_sqrt2, r_over_sqrt2));
      points_in_disc.push_back(disc_in.center +
                               BigVec2(-r_over_sqrt2, -r_over_sqrt2));

      Sample(tx, [&](const BigRat &trans_x){
          Sample(ty, [&](const BigRat &trans_y){
              const BigVec2 translation(trans_x, trans_y);
              for (const BigVec2 &p_in : points_in_disc) {
                BigVec2 p_out = p_in + translation;
                // Check p_out is in result disc.
                BigVec2 delta = p_out - result.center;
                BigRat dist_sq = dot(delta, delta);
                CHECK(dist_sq <= result.radius_sq)
                  << "Sample point " << VecString(p_out)
                  << " is outside the resulting disc " << result.ToString()
                  << "\n  dist_sq: " << dist_sq.ToString()
                  << "\n  radius_sq: " << result.radius_sq.ToString()
                  << "\n  p_in: " << VecString(p_in)
                  << "\n  translation: " << VecString(translation)
                  << "\n  disc_in: " << disc_in.ToString();
              }
            });
        });
    };

  // Zero translation.
  {
    Discival disc_in(BigVec2(BigRat(1), BigRat(2)), BigRat(9));
    Bigival tx(0), ty(0);
    CheckAllSamples(disc_in, tx, ty);
    Discival result = TranslateDisc(&disc_in, tx, ty, inv_epsilon);
    // Center should be identical.
    CHECK(result.center == disc_in.center);
    // Radius should be very close.
    BigRat r_in = disc_in.Radius(inv_epsilon);
    CHECK(BigRat::Abs(result.Radius(inv_epsilon) - r_in) <
          BigRat(1, 10000));
  }

  // Point translation.
  {
    Discival disc_in(BigVec2(BigRat(1, 2), BigRat(-3, 4)), BigRat(1, 16));
    Bigival tx(10), ty(-20);
    CheckAllSamples(disc_in, tx, ty);
    Discival result = TranslateDisc(&disc_in, tx, ty, inv_epsilon);
    // Center should be close to the singular point.

    BigRat dx = result.center.x - (tx.LB() + disc_in.center.x);
    BigRat dy = result.center.y - (ty.LB() + disc_in.center.y);
    CHECK(dx * dx + dy * dy < BigRat(1, 10000));
  }

  // Rectangular translation.
  {
    Discival disc_in(BigVec2(BigRat(0), BigRat(-1, 3)), BigRat(1, 4));
    Bigival tx(10, 12, true, true), ty(-2, -1, true, true);
    CheckAllSamples(disc_in, tx, ty);
  }

  // Translation includes origin.
  {
    Discival disc_in(BigVec2(BigRat(5), BigRat(5)), BigRat(4));
    Bigival tx(-1, 1, true, true), ty(-1, 1, true, true);
    CheckAllSamples(disc_in, tx, ty);
  }

  // Open intervals.
  {
    Discival disc_in(BigVec2(BigRat(0), BigRat(0)), BigRat(2, 3));
    Bigival tx(0, 1, false, true), ty(2, 3, true, false);
    CheckAllSamples(disc_in, tx, ty);
  }

  printf("TranslateDisc OK\n");
}

static void TestSphericalPatchBall() {
  BigInt inv_epsilon(1000000);
  BigInt inv_epsilon_high("1000000000000000000000");

  auto Check = [&](const Bigival &azimuth, const Bigival &angle) {
    ViewBoundsTrig trig(azimuth, angle, inv_epsilon);
    Ballival result = SphericalPatchBall(trig, inv_epsilon);

    // This is not a required property, but we always return a
    // center that is either exactly the origin or close to the
    // surface of the sphere.
    BigRat center_len_sq = dot(result.center, result.center);
    if (center_len_sq != BigRat(0)) {
      CHECK(BigRat::Abs(center_len_sq - BigRat(1)) < BigRat(1, 100000));
    }

    // Check that points sampled from the patch are inside the ball.
    // This includes endpoints and special points.
    Sample(azimuth, [&](const BigRat &az_sample) {
        Bigival sin_az = Bigival::Sin(az_sample, inv_epsilon_high);
        Bigival cos_az = Bigival::Cos(az_sample, inv_epsilon_high);

        Sample(angle, [&](const BigRat &an_sample) {
            Bigival sin_an = Bigival::Sin(an_sample, inv_epsilon_high);
            Bigival cos_an = Bigival::Cos(an_sample, inv_epsilon_high);

            Vec3ival sample(sin_an * cos_az, sin_an * sin_az, cos_an);

            Bigival dx = sample.x - result.center.x;
            Bigival dy = sample.y - result.center.y;
            Bigival dz = sample.z - result.center.z;

            Bigival dist_sq =
              dx.Squared() +
              dy.Squared() +
              dz.Squared();

            // The intervals must overlap, which means that the lower
            // bound must be in the ball's radius.
            CHECK(dist_sq.LB() <= result.radius_sq)
              << "Corner of patch is outside the computed bounding ball.\n"
              << " az: " << az_sample.ToString() << "\n"
              << " an: " << an_sample.ToString() << "\n"
              << " Ball: c=" << VecString(result.center)
              << " r_sq=" << result.radius_sq.ToString() << "\n"
              << " Sample AABB: " << sample.ToString() << "\n"
              << " dist_sq: " << dist_sq.ToString();
          });
      });
    };

  // A typical small patch.
  {
    Bigival azimuth(BigRat(1, 10), BigRat(2, 10), true, true);
    Bigival angle(BigRat(3, 10), BigRat(4, 10), true, true);
    Check(azimuth, angle);
  }

  // A patch that crosses zero.
  {
    Bigival azimuth(BigRat(-1, 20), BigRat(1, 20), true, true);
    Bigival angle(BigRat(-1, 20), BigRat(1, 20), true, true);
    Check(azimuth, angle);
  }

  // A single point.
  {
    Bigival azimuth(BigRat(1, 7));
    Bigival angle(BigRat(2, 7));
    ViewBoundsTrig trig(azimuth, angle, inv_epsilon);
    Ballival result = SphericalPatchBall(trig, inv_epsilon);
    // Radius should be tiny (but maybe not zero due to trig approximations).
    CHECK(result.radius_sq < BigRat(BigInt(1), inv_epsilon));
  }

  // Degenerate case with a wide angle interval.
  {
    Bigival azimuth(BigRat(1, 10), BigRat(2, 10), true, true);
    Bigival angle(BigRat(0), BigRat(2), true, true);
    Check(azimuth, angle);
  }

  // 5. A patch near the north pole.
  {
    Bigival azimuth(BigRat(1, 5), BigRat(2, 5), true, true);
    Bigival angle(BigRat(1, 100), BigRat(2, 100), true, true);
    Check(azimuth, angle);
  }

  printf("SphericalPatchBall OK\n");
}

static Vec3ival FromVec3(const vec3 &v) {
  return Vec3ival{
    Bigival(BigRat::FromDouble(v.x)),
    Bigival(BigRat::FromDouble(v.y)),
    Bigival(BigRat::FromDouble(v.z)),
  };
}

static void TestFrameFromViewPos() {
  static constexpr bool VERBOSE = false;
  BigInt inv_epsilon(1000000);

  auto CheckSpecificView = [&](const vec3 &v) {
      vec3 view = normalize(v);

      frame3 frame_patches_dbl = FrameFromViewPos(view);
      Frame3ival frame_patches = Frame3ival{
        .x = FromVec3(frame_patches_dbl.x),
        .y = FromVec3(frame_patches_dbl.y),
        .z = FromVec3(frame_patches_dbl.z),
      };

      Vec3ival view_ival = FromVec3(view);
      Frame3ival frame_intervals =
        FrameFromViewPos(view_ival, inv_epsilon);

      CHECK_OVERLAP_3D(frame_intervals.x, frame_patches.x);
      CHECK_OVERLAP_3D(frame_intervals.y, frame_patches.y);
      CHECK_OVERLAP_3D(frame_intervals.z, frame_patches.z);
    };

  CheckSpecificView(vec3{1.0, 2.0, 3.0});
  CheckSpecificView(vec3{-1.0, 2.0, 3.0});
  CheckSpecificView(vec3{1.0, -2.0, 3.0});
  CheckSpecificView(vec3{1.0, -2.0, -3.0});
  CheckSpecificView(vec3{1.0, 0.0, 0.0});

  // A view position must be on the unit sphere (and not the z
  // axis), so the input must intersect the unit sphere (and not
  // the z axis).
  auto CheckInterval = [&](const Vec3ival &view_ival_any) {
      // Must be a unit vector.
      // Vec3ival view_ival = Normalize(view_ival_any, inv_epsilon);
      const Vec3ival &view_ival = view_ival_any;
      auto InView = [&view_ival](const vec3 &v) {
          return view_ival.x.Contains(BigRat::FromDouble(v.x)) &&
            view_ival.y.Contains(BigRat::FromDouble(v.y)) &&
            view_ival.z.Contains(BigRat::FromDouble(v.z));
        };
      CHECK(!InView(vec3(0, 0, 0))) << "Interval cannot contain origin.";

      vec3 midpoint{
        view_ival.x.Midpoint().ToDouble(),
        view_ival.y.Midpoint().ToDouble(),
        view_ival.z.Midpoint().ToDouble(),
      };
      CHECK(InView(midpoint));

      Frame3ival frame = FrameFromViewPos(view_ival, inv_epsilon);

      if (VERBOSE)
        Print("Interval frame:\n{}\n", frame.ToString());

      // Sample concrete views.
      Sample(view_ival.x, [&](const BigRat &vx) {
          Sample(view_ival.y, [&](const BigRat &vy) {
              Sample(view_ival.z, [&](const BigRat &vz) {

                  if (vx == 0 && vy == 0 && vz == 0)
                    return;

                  // Subtle: Now we need a sample that is a unit vector
                  // and in the input AABB. We picked a point
                  // (vx, vy, vz) in the AABB, but we cannot simply
                  // normalize it because it might end up *outside*
                  // the AABB!

                  // Move towards the center until we are in
                  // the AABB. This assumes that the normalized center
                  // is in the AABB!
                  vec3 view_sample{
                    vx.ToDouble(), vy.ToDouble(), vz.ToDouble(),
                  };
                  for (int attempts = 0; true; attempts++) {
                    CHECK(attempts < 100) << "This test assumes that "
                      "the AABB's center, when normalized, is still "
                      "in the AABB. You might have a 'spherical zone' "
                      "of the view sphere that is missing its cap.";
                    view_sample = normalize(view_sample);
                    if (InView(view_sample)) break;

                    view_sample = view_sample + midpoint;
                  }

                  if (VERBOSE) {
                    Print("Sampled view: {:.17g} {:.17g} {:.17g}\n",
                          view_sample.x, view_sample.y, view_sample.z);
                  }
                  // Get the sampled frame using patches.h conventions.
                  frame3 frame_patches = FrameFromViewPos(view_sample);

                  if (VERBOSE) {
                    Print("Sampled frame:\n{}\n", FrameString(frame_patches));
                  }

                  // Now see that for some points, we get consistent
                  // 2D projections of them.
                  // Just need a few 3D points.
                  for (const vec3 &v : std::initializer_list<vec3>{
                      vec3{1.0, 2.0, 3.0},
                      vec3{0.0, -1.0, 0.0},
                      vec3{1.0, 0.0, 0.0},
                      vec3{0.0, 0.0, -0.5},
                      vec3{-3.5, 1.25, -2},
                    }) {

                    if (VERBOSE) {
                      Print("For v = {}...\n", VecString(v));
                    }

                    const BigVec3 bv{
                      BigRat::FromDouble(v.x),
                      BigRat::FromDouble(v.y),
                      BigRat::FromDouble(v.z),
                    };

                    Vec2ival p1 = TransformPointTo2D(frame, bv);

                    vec3 p2d = transform_point(frame_patches, v);

                    // The floating point results are not exact, so allow
                    // a small epsilon around them. We are looking for
                    // gross inconsistency here.
                    Vec2ival p2{
                      Bigival(BigRat::FromDouble(p2d.x - 1e-6),
                              BigRat::FromDouble(p2d.x + 1e-6), false, false),
                      Bigival(BigRat::FromDouble(p2d.y - 1e-6),
                              BigRat::FromDouble(p2d.y + 1e-6), false, false),
                    };

                    CHECK_OVERLAP_2D(p1, p2);
                  }
                });
            });
        });
    };


  CheckInterval(
      Vec3ival{
        Bigival(BigRat(99, 100), BigRat(101, 100), false, false),
        Bigival(BigRat(-1, 1024), BigRat(1, 1025), false, false),
        Bigival(BigRat(-1, 1023), BigRat(1, 1021), false, false),
      });

  printf("FrameFromViewPos OK.\n");
}

static void TestTransformVec() {
  static constexpr bool VERBOSE = false;
  BigInt inv_epsilon(1000000);
  BigInt inv_epsilon_high("1000000000000000000000");

  auto Check = [&](const Bigival &azimuth, const Bigival &angle,
                   const BigVec3 &v) {
      if (VERBOSE) {
        Print("-------------------\n"
              "Azimuth: {}\n"
              "Angle: {}\n"
              "v: {}\n",
              azimuth.ToString(),
              angle.ToString(),
              VecString(v));
      }

      ViewBoundsTrig trig(azimuth, angle, inv_epsilon);

      // Optimized version
      Vec2ival result = TransformVec(trig, v);

      // Naive version for comparison, but only if it's safe to run.
      // It's unsafe if the angle interval contains a multiple of pi,
      // which causes division by zero in FrameFromViewPos.
      Bigival pi = Bigival::Pi(inv_epsilon);
      CHECK(!(angle / pi).ContainsInteger());

      Vec3ival viewpos_ival = ViewFromSpherical(trig);
      Frame3ival frame_ival = FrameFromViewPos(viewpos_ival, inv_epsilon);
      Vec2ival naive_result = TransformPointTo2D(frame_ival, v);

      // The optimized result should be a sub-interval of the naive one,
      // or else we should be getting a better bound by intersecting
      // both approaches!
      CHECK(result.x.LB() >= naive_result.x.LB() &&
            result.x.UB() <= naive_result.x.UB())
        << "Optimized x " << result.x.ToString()
        << " is not a sub-interval of naive x "
        << naive_result.x.ToString();
      CHECK(result.y.LB() >= naive_result.y.LB() &&
            result.y.UB() <= naive_result.y.UB())
        << "Optimized y " << result.y.ToString()
        << " is not a sub-interval of naive y "
        << naive_result.y.ToString();

      // Verify correctness by sampling. The result AABB must overlap with
      // the high-precision AABB of any sample point.
      Bigival pi_high = Bigival::Pi(inv_epsilon_high);
      Sample(azimuth, [&](const BigRat &az_sample) {
          Sample(angle, [&](BigRat an_sample) {

              // Cannot compute this at the north/south poles.
              CHECK(an_sample != 0);

              if (VERBOSE) {
                Print("az_sample: {}\n"
                      "an_sample: {}\n",
                      az_sample.ToString(),
                      an_sample.ToString());
              }

              ViewBoundsTrig sample_trig(
                  Bigival(az_sample), Bigival(an_sample), inv_epsilon_high);
              Vec3ival viewpos_ival = ViewFromSpherical(sample_trig);

              if (VERBOSE) {
                Print("viewpos_ival: {}\n",
                      viewpos_ival.ToString());
              }

              Frame3ival frame_ival =
                FrameFromViewPos(viewpos_ival, inv_epsilon_high);
              Vec2ival sample_point = TransformPointTo2D(frame_ival, v);

              CHECK_OVERLAP_2D(result, sample_point);
            });
      });
  };

  // A typical small patch.
  {
    Bigival azimuth(BigRat(1, 10), BigRat(2, 10), true, true);
    Bigival angle(BigRat(3, 10), BigRat(4, 10), true, true);
    Check(azimuth, angle, BigVec3(BigRat(-1), BigRat(2), BigRat(3)));
    Check(azimuth, angle, BigVec3(BigRat(1), BigRat(2), BigRat(3)));
    Check(azimuth, angle, BigVec3(BigRat(2), BigRat(0), BigRat(0)));
    Check(azimuth, angle, BigVec3(BigRat(0), BigRat(-1, 2), BigRat(0)));
    Check(azimuth, angle, BigVec3(BigRat(0), BigRat(0), BigRat(1)));
    Check(azimuth, angle, BigVec3(BigRat(0), BigRat(0), BigRat(0)));
  }

  // A patch that crosses the prime meridian near the equator.
  {
    Bigival azimuth(BigRat(-1, 10), BigRat(1, 10), true, true);
    // Shifted angle away from the pole (z-axis).
    Bigival angle(BigRat(14, 10), BigRat(16, 10), true, true);
    Check(azimuth, angle, BigVec3(BigRat(5), BigRat(-8), BigRat(1)));
    Check(azimuth, angle, BigVec3(BigRat(1, 2), BigRat(0), BigRat(0)));
    Check(azimuth, angle, BigVec3(BigRat(3, 5), BigRat(1, 2), BigRat(-2)));
  }

  // A single point.
  {
    Bigival azimuth(BigRat(1, 7));
    Bigival angle(BigRat(2, 7));
    BigVec3 v(BigRat(1), BigRat(2), BigRat(3));
    ViewBoundsTrig trig(azimuth, angle, inv_epsilon);

    Vec2ival result = TransformVec(trig, v);
    // For a singular point, the AABB should be very small,
    // only reflecting the uncertainty in the trig functions.
    CHECK(result.x.Width() < BigRat(BigInt(1), inv_epsilon));
    CHECK(result.y.Width() < BigRat(BigInt(1), inv_epsilon));
  }

  // Open intervals.
  {
    Bigival azimuth(BigRat(1, 10), BigRat(2, 10), false, true);
    Bigival angle(BigRat(3, 10), BigRat(4, 10), true, false);
    Check(azimuth, angle, BigVec3(BigRat(1), BigRat(1), BigRat(1)));
    Check(azimuth, angle, BigVec3(BigRat(0), BigRat(1), BigRat(0)));
    Check(azimuth, angle, BigVec3(BigRat(0), BigRat(0), BigRat(-2)));
  }

  printf("TransformVec OK\n");
}

int main(int argc, char **argv) {
  ANSI::Init();
  printf("\n");

  TestDotProductWithView();
  TestGetBoundingAABB("GetBoundingAABB", &GetBoundingAABB);
  TestGetBoundingAABB("GetBoundingAABB2", &GetBoundingAABB2);
  TestExpandSquaredRadius();
  TestIsDiscOutsideEdge();
  TestTranslateDisc();
  TestSphericalPatchBall();
  TestFrameFromViewPos();
  TestTransformVec();

  // BenchAABB(&GetBoundingAABB, &GetBoundingAABB2);

  printf("OK\n");
  return 0;
}
