
#include "big-interval.h"

#include <cmath>
#include <numbers>
#include <string>
#include <cstdio>

#include "bignum/big.h"
#include "bignum/big-overloads.h"
#include "ansi.h"

static constexpr bool VERBOSE = false;

#define CHECK_CONTAINS(ival_exp, v_exp) do {             \
  auto ival = (ival_exp);                                \
  auto v = (v_exp);                                      \
  CHECK(ival.Contains(v)) << "Expected the interval "    \
    #ival_exp " to contain the value " #v_exp << ":\n"   \
    "ival: " << ival.ToString() << "\nvalue: " <<        \
    v.ToString();                                        \
 } while (0)

#define CHECK_HAS(opt_exp) [&]() {                      \
  auto opt = (opt_exp);                                 \
  CHECK(opt.has_value()) << "Expected the optional "    \
    "expression " #opt_exp " to result in a value, "    \
    "but it was nullopt.\n";                            \
  return opt.value();                                   \
  }()

// Sample some points in the interval. Call f on them.
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

static void Simple() {
  {
    Bigival b(BigRat(-2), BigRat(3), true, true);
    CHECK(b.ContainsZero());
    CHECK(b.ContainsOrApproachesZero());

    CHECK_CONTAINS(b, BigRat(0));
    CHECK_CONTAINS(b, BigRat(-2));
    CHECK_CONTAINS(b, BigRat(1));
    CHECK_CONTAINS(b, BigRat(1, 2));
    CHECK(b.Midpoint() == BigRat(1, 2));

    Sample(b, [&](const BigRat &s) {
        CHECK_CONTAINS(b, s);
      });
  }

  {
    Bigival b(BigRat(3), BigRat(4), true, true);
    CHECK(!b.ContainsZero());
    CHECK(!b.ContainsOrApproachesZero());
  }

  {
    Bigival b(BigRat(-4), BigRat(0), true, false);
    CHECK(!b.ContainsZero());
    CHECK(b.ContainsOrApproachesZero());
  }

  {
    Bigival b(BigRat(0), BigRat(4), false, true);
    CHECK(!b.ContainsZero());
    CHECK(b.ContainsOrApproachesZero());
  }

  {
    Bigival b(BigRat(-4), BigRat(0), false, true);
    CHECK(b.ContainsZero());
    CHECK(b.ContainsOrApproachesZero());
  }

  {
    Bigival a(BigRat(-1), BigRat(1), false, false);
    a.Add(BigRat(1));
    CHECK(a.UB() == BigRat(1));
    CHECK(a.IncludesUB());
    CHECK(!a.IncludesLB());
    a.Add(BigRat(-1));
    CHECK(a.LB() == BigRat(-1));
    CHECK(a.IncludesLB());
  }

  {
    Bigival a(BigRat(2), BigRat(2), true, true);
    a.Add(BigRat(2));
    CHECK(a.LB() == BigRat(2));
    CHECK(a.UB() == BigRat(2));
    a.Add(BigRat(3));
    CHECK(a.LB() == BigRat(2));
    CHECK(a.UB() == BigRat(3));
    CHECK(a.IncludesUB());
  }

  for (Bigival a : {
      Bigival(BigRat(-1), BigRat(2), true, true),
      Bigival(BigRat(1), BigRat(6, 5), false, true),
      Bigival(BigRat(3), BigRat(16, 5), false, false),
      Bigival(BigRat(-1), BigRat(1), true, false),
      Bigival(BigRat(-3)),
      Bigival(BigRat(0)),
    }) {

    CHECK_CONTAINS(a, a.Midpoint());

    CHECK_EQ(a.Midpoint(), (a.LB() + a.UB()) / 2);

    for (Bigival b : {
        Bigival(BigRat(3), BigRat(8), false, false),
        Bigival(BigRat(-2, 3), BigRat(0), false, false),
        Bigival(BigRat(-8, 3), BigRat(-1), false, true),
        Bigival(BigRat(1, 8), BigRat(1, 7), true, false),
        Bigival(BigRat(-1), BigRat(3), true, true),
        Bigival(BigRat(1)),
        Bigival(BigRat(-1)),
        Bigival(BigRat(0)),
      }) {

      if (VERBOSE) {
        printf("%s @ %s\n",
               a.ToString().c_str(), b.ToString().c_str());
      }

      {
        Bigival sum = a + b;
        Sample(a, [&](const BigRat &sa) {
            Sample(b, [&](const BigRat &sb) {
                CHECK_CONTAINS(sum, sa + sb);
              });
          });
      }

      {
        Bigival difference = a - b;
        Sample(a, [&](const BigRat &sa) {
            Sample(b, [&](const BigRat &sb) {
                CHECK_CONTAINS(difference, sa - sb);
              });
          });
      }

      {
        Bigival product = a * b;
        Sample(a, [&](const BigRat &sa) {
            Sample(b, [&](const BigRat &sb) {
                CHECK_CONTAINS(product, sa * sb);
              });
          });
      }

      // Only when defined and supported.
      if (!b.ContainsOrApproachesZero() &&
          b.LB() != 0 &&
          b.UB() != 0) {
        Bigival quotient = a / b;
        Sample(a, [&](const BigRat &sa) {
            Sample(b, [&](const BigRat &sb) {
                CHECK_CONTAINS(quotient, sa / sb);
              });
          });
      }

      // With rational arg when we have the overload.
      {
        Bigival sum1 = a + b.UB();
        Sample(a, [&](const BigRat &sa) {
            CHECK_CONTAINS(sum1, sa + b.UB());
          });
        Bigival sum2 = b.UB() + a;
        Sample(a, [&](const BigRat &sa) {
            CHECK_CONTAINS(sum2, sa + b.UB());
          });
      }

      {
        Bigival diff1 = a - b.UB();
        Sample(a, [&](const BigRat &sa) {
            CHECK_CONTAINS(diff1, sa - b.UB());
          });
      }

      {
        Bigival product1 = a * b.UB();
        Sample(a, [&](const BigRat &sa) {
            CHECK_CONTAINS(product1, sa * b.UB());
          });
        Bigival product2 = b.UB() * a;
        Sample(a, [&](const BigRat &sa) {
            CHECK_CONTAINS(product1, sa * b.UB());
          });
      }

      if (!BigRat::IsZero(b.UB())) {
        Bigival quot1 = a / b.UB();
        Sample(a, [&](const BigRat &sa) {
            CHECK_CONTAINS(quot1, sa / b.UB());
          });
      }

      {
        Bigival mn = Bigival::Min(a, b);
        Sample(a, [&](const BigRat &sa) {
            Sample(b, [&](const BigRat &sb) {
                CHECK_CONTAINS(mn, BigRat::Min(sa, sb));
              });
          });
      }

      {
        Bigival mx = Bigival::Max(a, b);
        Sample(a, [&](const BigRat &sa) {
            Sample(b, [&](const BigRat &sb) {
                CHECK_CONTAINS(mx, BigRat::Max(sa, sb));
              });
          });
      }

    }
  }

}

static void Special() {
  {
    Bigival b(BigRat(-2), BigRat(3), true, true);

    Bigival bb = b.Squared();

    CHECK(bb.LB() == BigRat(0));
    CHECK(bb.UB() == BigRat(9));
    CHECK(bb.IncludesLB());
    CHECK(bb.IncludesUB());

    Sample(b, [&bb](const BigRat &s) {
        // printf("Sample: %s\n", s.ToString().c_str());
        CHECK_CONTAINS(bb, s * s);
      });
  }

  // Abs:
  {
    // Contains zero, negative side has larger magnitude.
    Bigival a(-5, 2, true, true);
    Bigival aa = a.Abs();
    CHECK(aa.LB() == 0);
    CHECK(aa.UB() == 5);
    Sample(a, [&aa](const BigRat &s) {
        CHECK_CONTAINS(aa, BigRat::Abs(s));
      });
  }
  {
    // All negative.
    Bigival b(-5, -2, false, true);
    Bigival bb = b.Abs();
    CHECK(bb.LB() == 2);
    CHECK(bb.UB() == 5);
    CHECK(bb.IncludesLB());
    CHECK(!bb.IncludesUB());
    Sample(b, [&bb](const BigRat &s) {
        CHECK_CONTAINS(bb, BigRat::Abs(s));
      });
  }

  {
    Bigival b = Bigival(BigRat{0}, BigRat{"52/73"}, false, false);
    Bigival bb = b.Abs();
    Sample(b, [&bb](const BigRat &s) {
        CHECK_CONTAINS(bb, BigRat::Abs(s));
      });
  }

  // Tests for Reciprocal()
  {
    // All positive.
    Bigival c(2, 4, true, false);
    Bigival cc = c.Reciprocal();
    CHECK(cc.LB() == BigRat(1, 4));
    CHECK(cc.UB() == BigRat(1, 2));
    CHECK(!cc.IncludesLB());
    CHECK(cc.IncludesUB());
    Sample(c, [&cc](const BigRat &s) {
        CHECK_CONTAINS(cc, BigRat::Inverse(s));
      });
  }
  {
    // All negative.
    Bigival d(-4, -2, true, true);
    Bigival dd = d.Reciprocal();
    CHECK(dd.LB() == BigRat(-1, 2));
    CHECK(dd.UB() == BigRat(-1, 4));
    Sample(d, [&dd](const BigRat &s) {
        CHECK_CONTAINS(dd, BigRat::Inverse(s));
      });
  }
}

static void IntervalOps() {
  {
    auto i = Bigival::MaybeIntersection(Bigival(1, 2, false, false),
                                        Bigival(3, 4, false, false));
    CHECK(!i.has_value());
  }

  {
    Bigival i = CHECK_HAS(
        Bigival::MaybeIntersection(Bigival(1, 2, false, true),
                                   Bigival(2, 3, true, false)));
    CHECK(i.Singular());
    CHECK(i.UB() == i.LB());
    CHECK(i.LB() == 2);
  }


  {
    auto i = Bigival::MaybeIntersection(Bigival(1, 2, false, false),
                                        Bigival(2));
    CHECK(!i.has_value()) << "The endpoint 2 is not included, so there "
      "is no overlap. But got: " << i.value().ToString();
  }

  {
    // One interval contains the other.
    Bigival i = CHECK_HAS(
        Bigival::MaybeIntersection(Bigival(0, 10, true, true),
                                   Bigival(2, 5, true, false)));
    CHECK(!i.Singular());
    CHECK(i.LB() == 2);
    CHECK(i.UB() == 5);
    CHECK(i.IncludesLB());
    CHECK(!i.IncludesUB());
  }


  {
    Bigival a(1, 4, false, false);
    Bigival b(3, 10, true, true);
    Bigival u = Bigival::Union(a, b);
    CHECK(u.LB() == 1);
    CHECK(u.UB() == 10);
    CHECK(!u.IncludesLB());
    CHECK(u.IncludesUB());
  }

  {
    Bigival a(1, 4, false, true);
    Bigival b(1, 4, true, false);
    Bigival u = Bigival::Union(a, b);
    CHECK(u.LB() == 1);
    CHECK(u.UB() == 4);
    CHECK(u.IncludesLB());
    CHECK(u.IncludesUB());
  }

  {
    Bigival a(-1, 1, false, true);
    Bigival b(5, 6, true, false);
    Bigival u = Bigival::Union(a, b);
    CHECK(u.LB() == -1);
    CHECK(u.UB() == 6);
    CHECK(!u.IncludesLB());
    CHECK(!u.IncludesUB());
  }

}

static void Comparisons() {
  CHECK((Bigival(BigRat(2)) == Bigival(BigRat(2))) ==
        Bigival::MaybeBool::True);

  CHECK((Bigival(BigRat(2)) == Bigival(BigRat(3))) ==
        Bigival::MaybeBool::False);
  CHECK((Bigival(BigRat(2), BigRat(3), false, true) == Bigival(BigRat(3))) ==
        Bigival::MaybeBool::Unknown);

  CHECK((Bigival(BigRat(2), BigRat(3), false, false) == Bigival(BigRat(3))) ==
        Bigival::MaybeBool::False);


  CHECK((Bigival(BigRat(2), BigRat(4), true, true) == Bigival(BigRat(3))) ==
        Bigival::MaybeBool::Unknown);

  CHECK((Bigival(BigRat(2), BigRat(4), true, true) ==
         Bigival(BigRat(-1), BigRat(0), true, true)) ==
        Bigival::MaybeBool::False);

  CHECK((Bigival(BigRat(1), BigRat(2), true, false) <
         Bigival(BigRat(2), BigRat(3), false, false)) ==
        Bigival::MaybeBool::True)
      << "2 is not included in either interval, so every value is less.";

  // <
  CHECK((Bigival(1, 2, true, true) < Bigival(3, 4, true, true)) ==
        Bigival::MaybeBool::True);
  CHECK((Bigival(1, 2, true, false) < Bigival(2, 3, true, true)) ==
        Bigival::MaybeBool::True);
  CHECK((Bigival(2) < Bigival(2)) ==
        Bigival::MaybeBool::False);
  CHECK((Bigival(1, 3, true, true) < Bigival(2, 4, true, true)) ==
        Bigival::MaybeBool::Unknown);
  CHECK((Bigival(3, 4, true, true) < Bigival(1, 2, true, true)) ==
        Bigival::MaybeBool::False);
  CHECK((Bigival(2, 3, true, true) < Bigival(1, 2, true, false)) ==
        Bigival::MaybeBool::False);

  // <=
  CHECK((Bigival(1, 2, true, true) <= Bigival(2, 3, true, true)) ==
        Bigival::MaybeBool::True);
  CHECK((Bigival(1, 2, true, false) <= Bigival(2, 3, true, true)) ==
        Bigival::MaybeBool::True);
  CHECK((Bigival(2) <= Bigival(2)) ==
        Bigival::MaybeBool::True);
  CHECK((Bigival(1, 3, true, true) <= Bigival(2, 4, true, true)) ==
        Bigival::MaybeBool::Unknown);
  CHECK((Bigival(3, 4, true, true) <= Bigival(1, 2, true, true)) ==
        Bigival::MaybeBool::False);
  CHECK((Bigival(2, 3, false, true) <= Bigival(1, 2, true, false)) ==
        Bigival::MaybeBool::False);
}

static void Transcendental() {

  BigInt inv_epsilon{1000000};
  BigRat epsilon{BigInt(1), inv_epsilon};

  for (double x : { -2.999999997, -1.0, 0.0, 3.141592652, 3.141592654,
      1.1e-16, 1.0, 1.1, 1.2, 2.9, 3.0, 400.0 }) {
    BigRat rx = BigRat::FromDouble(x);
    Bigival sinx = Bigival::Sin(rx, inv_epsilon);

    // std::sin also rounds, so the true value must be somewhere
    // in (nextafter(s, -1), nextafter(s, +1)).
    double s = std::sin(x);
    Bigival actual = Bigival(BigRat::FromDouble(std::nextafter(s, -2)),
                             BigRat::FromDouble(std::nextafter(s, +2)),
                             false, false);

    if (VERBOSE) {
      printf("sin(%f) = %s\n"
             "Want: %s\n", x, sinx.ToString().c_str(),
             actual.ToString().c_str());
    }

    CHECK(Bigival::MaybeIntersection(sinx, actual).has_value()) << x;
    // Moreover, the interval must be small enough.
    CHECK(sinx.Width() <= epsilon);

    // Also NiceSin.
    Bigival nicesinx = Bigival::NiceSin(rx, inv_epsilon);
    CHECK(Bigival::MaybeIntersection(nicesinx, actual).has_value()) << x;
    // TODO: Sometimes we are not as tight as expected.
    CHECK(nicesinx.Width() <= epsilon * 2) <<
      "On: " << x << "\n"
      "Width: " << nicesinx.Width().ToString() << " which is about " <<
      nicesinx.Width().ToDouble();
  }

  for (double x : { -2.999999997, -1.0, 0.0, 3.1415926452, 3.141592654,
      1.1e-16, 1.0, 1.1, 1.2, 2.9, 3.0, 400.0 }) {
    BigRat rx = BigRat::FromDouble(x);
    Bigival cosx = Bigival::Cos(rx, inv_epsilon);
    double c = std::cos(x);
    Bigival actual = Bigival(BigRat::FromDouble(std::nextafter(c, -2)),
                             BigRat::FromDouble(std::nextafter(c, +2)),
                             false, false);

    if (VERBOSE) {
      printf("cos(%.17g) = %s = %.17g\n"
             "Want: %s\n",
             x, cosx.ToString().c_str(), c,
             actual.ToString().c_str());
    }

    CHECK(Bigival::MaybeIntersection(cosx, actual).has_value()) << x;
    CHECK(cosx.Width() <= epsilon);

    // Also NiceCos.
    Bigival nicecosx = Bigival::NiceCos(rx, inv_epsilon);
    CHECK(Bigival::MaybeIntersection(nicecosx, actual).has_value()) << x;
    // TODO: Sometimes we are not as tight as expected.
    CHECK(nicecosx.Width() <= epsilon * 2) <<
      "On: " << x << "\n"
      "Width: " << nicecosx.Width().ToString() << " which is about " <<
      nicecosx.Width().ToDouble();
  }

  // Sin of intervals.
  {
    Bigival a = Bigival(BigRat::FromDouble(0.3),
                        BigRat::FromDouble(0.31), true, true);
    Bigival sina = a.Sin(BigInt(1024 * 1024));
    CHECK_CONTAINS(sina, BigRat::FromDouble(std::sin(0.301)));
    CHECK_CONTAINS(sina, BigRat::FromDouble(std::sin(0.305)));
    CHECK_CONTAINS(sina, BigRat::FromDouble(std::sin(0.30999)));

    CHECK(!sina.Contains(BigRat::FromDouble(std::sin(0.311))));
    CHECK(!sina.Contains(BigRat::FromDouble(std::sin(0.299))));
    CHECK(!sina.Contains(BigRat(1)));
    CHECK(!sina.Contains(BigRat(-1)));
    CHECK(!sina.Contains(BigRat(0)));
  }

  {
    // Very small interval around 26.5π, which is a peak.
    Bigival a = Bigival(BigRat::FromDouble(26.5 * std::numbers::pi - 1.0e-12),
                        BigRat::FromDouble(26.5 * std::numbers::pi + 1.0e-12),
                        false, false);
    Bigival sina = a.Sin(BigInt(1024 * 1024));
    CHECK_CONTAINS(sina, BigRat(1));
    CHECK(!sina.Contains(BigRat(-1)));
    CHECK(!sina.Contains(BigRat(0)));
  }

  {
    // Very small interval after (and not including) 26.5π, which is a peak.
    Bigival a = Bigival(BigRat::FromDouble(26.5 * std::numbers::pi + 1.0e-6),
                        BigRat::FromDouble(26.5 * std::numbers::pi + 2.0e-6),
                        false, false);
    Bigival sina = a.Sin(BigInt::Pow(BigInt(10), 24));
    CHECK(!sina.Contains(BigRat(1)));
    // Should be a small interval, since the input interval is small and we
    // requested a small epsilon.
    CHECK(sina.Width() <= BigRat(1, 1024));
  }

  {
    // Very small interval before (and not including) 26.5π, which is a peak.
    Bigival a = Bigival(BigRat::FromDouble(26.5 * std::numbers::pi - 2.0e-6),
                        BigRat::FromDouble(26.5 * std::numbers::pi - 1.0e-6),
                        false, false);
    Bigival sina = a.Sin(BigInt::Pow(BigInt(10), 24));
    CHECK(!sina.Contains(BigRat(1)));
    CHECK(sina.Width() <= BigRat(1, 1024));
  }

  {
    Bigival sina = Bigival(BigRat(-10), BigRat(10),
                           false, false).Sin(BigInt(10));
    CHECK(sina.LB() == -1);
    CHECK(sina.UB() == 1);
    CHECK(sina.IncludesLB() && sina.IncludesUB());
  }


  // Cos of intervals.
  {
    Bigival a = Bigival(BigRat::FromDouble(0.3), BigRat::FromDouble(0.31),
                        true, true);
    Bigival cosa = a.Cos(BigInt(1024 * 1024));
    CHECK_CONTAINS(cosa, BigRat::FromDouble(std::cos(0.301)));
    CHECK_CONTAINS(cosa, BigRat::FromDouble(std::cos(0.305)));
    CHECK_CONTAINS(cosa, BigRat::FromDouble(std::cos(0.30999)));

    CHECK(!cosa.Contains(BigRat::FromDouble(std::cos(0.311))));
    CHECK(!cosa.Contains(BigRat::FromDouble(std::cos(0.299))));
    CHECK(!cosa.Contains(BigRat(1)));
    CHECK(!cosa.Contains(BigRat(-1)));
    CHECK(!cosa.Contains(BigRat(0)));
  }

  {
    // Very small interval around 26π, which is a peak.
    Bigival a = Bigival(BigRat::FromDouble(26 * std::numbers::pi - 1.0e-12),
                        BigRat::FromDouble(26 * std::numbers::pi + 1.0e-12),
                        false, false);
    Bigival cosa = a.Cos(BigInt(1024 * 1024));
    CHECK_CONTAINS(cosa, BigRat(1));
    CHECK(!cosa.Contains(BigRat(-1)));
    CHECK(!cosa.Contains(BigRat(0)));
  }

  {
    // Very small interval after (and not including) 26π, which is a peak.
    Bigival a = Bigival(BigRat::FromDouble(26 * std::numbers::pi + 1.0e-6),
                        BigRat::FromDouble(26 * std::numbers::pi + 2.0e-6),
                        false, false);
    Bigival cosa = a.Cos(BigInt::Pow(BigInt(10), 24));
    CHECK(!cosa.Contains(BigRat(1)));
    // Should be a small interval, since the input interval is small and we
    // requested a small epsilon.
    CHECK(cosa.Width() <= BigRat(1, 1024));
  }

  {
    // Very small interval before (and not including) 26π, which is a peak.
    Bigival a = Bigival(BigRat::FromDouble(26 * std::numbers::pi - 2.0e-6),
                        BigRat::FromDouble(26 * std::numbers::pi - 1.0e-6),
                        false, false);
    Bigival cosa = a.Cos(BigInt::Pow(BigInt(10), 24));
    CHECK(!cosa.Contains(BigRat(1)));
    CHECK(cosa.Width() <= BigRat(1, 1024));
  }

  {
    Bigival cosa = Bigival(BigRat(-10), BigRat(10),
                           false, false).Cos(BigInt(10));
    CHECK(cosa.LB() == -1);
    CHECK(cosa.UB() == 1);
    CHECK(cosa.IncludesLB() && cosa.IncludesUB());
  }

}

static void ContainsInteger() {
  CHECK(Bigival(0).ContainsInteger());
  CHECK(Bigival(2).ContainsInteger());
  CHECK(Bigival(-27).ContainsInteger());
  CHECK(!Bigival(BigRat(-1, 2)).ContainsInteger());
  CHECK(!Bigival(BigRat(1, 3)).ContainsInteger());

  CHECK(Bigival(BigRat(-1), BigRat(1), false, false).ContainsInteger());
  CHECK(Bigival(BigRat(-1), BigRat(1), true, false).ContainsInteger());
  CHECK(Bigival(BigRat(-1), BigRat(1), false, true).ContainsInteger());
  CHECK(Bigival(BigRat(-1), BigRat(1), true, true).ContainsInteger());

  CHECK(Bigival(BigRat(3, 4), BigRat(5, 4), false, false).ContainsInteger());
}

static void TimesRat() {
  {
    Bigival a(2, 5, true, false);
    Bigival p = a * BigRat(3);
    CHECK(p.LB() == 6);
    CHECK(p.UB() == 15);
    CHECK(p.IncludesLB());
    CHECK(!p.IncludesUB());
  }

  {
    Bigival a(2, 5, true, false);
    Bigival p = a * BigRat(0);
    CHECK(p.Singular());
    CHECK(p.LB() == 0);
  }

  {
    Bigival a(2, 5, true, false);
    Bigival p = a * BigRat(-3);
    CHECK(p.LB() == -15);
    CHECK(p.UB() == -6);
    CHECK(!p.IncludesLB());
    CHECK(p.IncludesUB());
  }

  {
    Bigival a(-5, -2, false, true);
    Bigival p = a * BigRat(-3);
    CHECK(p.LB() == 6);
    CHECK(p.UB() == 15);
    CHECK(p.IncludesLB());
    CHECK(!p.IncludesUB());
  }

  {
    Bigival a(-2, 5, true, true);
    Bigival p = a * BigRat(-3);
    CHECK(p.LB() == -15);
    CHECK(p.UB() == 6);
    CHECK(p.IncludesLB());
    CHECK(p.IncludesUB());
  }
  printf("TimesRat ok.\n");
}

static void MathFuncs() {
  BigInt inv_epsilon(1000000);
  BigRat epsilon(BigInt(1), inv_epsilon);

  {
    Bigival pi = Bigival::Pi(inv_epsilon);
    // std::numbers::pi is the double value closest to the actual
    // value of pi. This means that the doubles immediately and
    // before this one must bracket the true value.
    Bigival double_pi(BigRat::FromDouble(
                          std::nextafter(std::numbers::pi, -100.0)),
                      BigRat::FromDouble(
                          std::nextafter(std::numbers::pi, +100.0)),
                      false, false);

    auto isect = Bigival::MaybeIntersection(pi, double_pi);
    CHECK(isect.has_value());

    CHECK(pi.Width() <= epsilon);
    // Expect open endpoints, since pi is irrational. (But it is not
    // formally wrong for these to be true).
    CHECK(!pi.IncludesLB() && !pi.IncludesUB());
  }

  {
    Bigival nine(9);
    Bigival sqrt9 = Bigival::Sqrt(nine, inv_epsilon);
    CHECK_CONTAINS(sqrt9, BigRat(3));
    CHECK(sqrt9.Width() <= epsilon);
  }

  {
    // Non-singular interval, both ends closed.
    Bigival range(4, 9, true, true);
    Bigival sqrt_range = Bigival::Sqrt(range, inv_epsilon);

    CHECK_CONTAINS(sqrt_range, BigRat(2));
    CHECK_CONTAINS(sqrt_range, BigRat(3));
  }
}

// Note: The function doesn't guarantee specifically that we
// return (n/(d+1), (n+1)/d), but this test does expect that.
static void CoarseSqrt() {
  // A high-precision epsilon for getting an accurate value of sqrt(r).
  const BigInt inv_epsilon(1000000);

  // Tests both the plain integer version and the version with an epsilon
  // target.
  #define CHECK_COARSE_SQRT(r) do {                                     \
      Bigival coarse = Bigival::CoarseIntSqrt(r);                       \
      if (VERBOSE) printf("Coarse Sqrt %s: %s\n", r.ToString().c_str(), \
        coarse.ToString().c_str());                                     \
      Bigival coarse2 = Bigival::CoarseIntSqrt(r, inv_epsilon);         \
      Bigival fine = Bigival::Sqrt(Bigival(r), inv_epsilon);            \
      if (VERBOSE) printf("Fine Sqrt %s: %s\n", r.ToString().c_str(),   \
        fine.ToString().c_str());                                       \
      CHECK(coarse.Eq(fine) != Bigival::MaybeBool::False);              \
      CHECK(coarse2.Eq(fine) != Bigival::MaybeBool::False);             \
  } while (0)

  {
    Bigival s = Bigival::CoarseIntSqrt(BigRat(0));
    CHECK(s.Singular() && s.LB() == 0);
  }

  {
    Bigival s = Bigival::CoarseIntSqrt(BigRat(1));
    CHECK(s.Singular() && s.LB() == 1);
  }

  {
    Bigival s = Bigival::CoarseIntSqrt(BigRat(0), inv_epsilon);
    CHECK(s.Singular() && s.LB() == 0);
  }

  // Integer inputs.
  CHECK_COARSE_SQRT(BigRat(1));
  CHECK_COARSE_SQRT(BigRat(2));
  CHECK_COARSE_SQRT(BigRat(3));
  CHECK_COARSE_SQRT(BigRat(100));
  CHECK_COARSE_SQRT(BigRat("100000000000000000000"));

  {
    Bigival s = Bigival::CoarseIntSqrt(BigRat(4));
    CHECK_CONTAINS(s, BigRat(2));
    CHECK(s.LB() == 2 && s.UB() == 3);
  }

  // 1/d inputs.
  CHECK_COARSE_SQRT(BigRat(1, 2));
  CHECK_COARSE_SQRT(BigRat(1, 3));
  CHECK_COARSE_SQRT(BigRat(1, 100));
  CHECK_COARSE_SQRT(BigRat(BigInt(1), BigInt("100000000000000000000")));

  {
    Bigival s = Bigival::CoarseIntSqrt(BigRat(1, 4));
    CHECK_CONTAINS(s, BigRat(1, 2));
    CHECK(s.LB() == BigRat(1, 3) && s.UB() == BigRat(1, 2));
  }

  CHECK_COARSE_SQRT(BigRat(2, 3));
  CHECK_COARSE_SQRT(BigRat(8, 9));
  CHECK_COARSE_SQRT(BigRat(9, 8));
  CHECK_COARSE_SQRT(BigRat(355, 113));

  {
    Bigival s = Bigival::CoarseIntSqrt(BigRat(25, 9));
    CHECK_CONTAINS(s, BigRat(5, 3));
    CHECK(s.LB() == BigRat(5, 4) && s.UB() == BigRat(2));
    CHECK(!s.IncludesLB() && !s.IncludesUB());
  }
}

int main(int argc, char **argv) {
  ANSI::Init();
  printf("\n");

  Simple();
  Special();
  IntervalOps();
  Comparisons();
  ContainsInteger();
  TimesRat();

  Transcendental();
  MathFuncs();

  CoarseSqrt();

  printf("OK\n");
  return 0;
}
