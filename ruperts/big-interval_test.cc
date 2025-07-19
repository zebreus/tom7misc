
#include "big-interval.h"

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
void Sample(const Bigival &v, const F &f) {
  // Completely specified; just one point to test.
  if (v.LB() == v.UB()) {
    CHECK(v.IncludesLB() && v.IncludesUB());
    f(v.LB());
    return;
  }

  if (v.IncludesLB()) {
    f(v.LB());
  }

  if (v.IncludesUB()) {
    f(v.UB());
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

    CHECK_CONTAINS(b, BigRat(0));
    CHECK_CONTAINS(b, BigRat(-2));
    CHECK_CONTAINS(b, BigRat(1));
    CHECK_CONTAINS(b, BigRat(1, 2));

    Sample(b, [&](const BigRat &s) {
        CHECK_CONTAINS(b, s);
      });
  }

  {
    Bigival b(BigRat(3), BigRat(4), true, true);
    CHECK(!b.ContainsZero());
  }

  {
    Bigival b(BigRat(-4), BigRat(0), true, false);
    CHECK(!b.ContainsZero());
  }

  {
    Bigival b(BigRat(-4), BigRat(0), false, true);
    CHECK(b.ContainsZero());
  }

  for (Bigival a : {
      Bigival(BigRat(-1), BigRat(2), true, true),
      Bigival(BigRat(1), BigRat(6, 5), false, true),
      Bigival(BigRat(3), BigRat(16, 5), false, false),
      Bigival(BigRat(-1), BigRat(1), true, false),
      Bigival(BigRat(-3)),
      Bigival(BigRat(0)),
    }) {
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

      Bigival sum = a + b;
      Sample(a, [&](const BigRat &sa) {
          Sample(b, [&](const BigRat &sb) {
              CHECK_CONTAINS(sum, sa + sb);
            });
        });

      Bigival difference = a - b;
      Sample(a, [&](const BigRat &sa) {
          Sample(b, [&](const BigRat &sb) {
              CHECK_CONTAINS(difference, sa - sb);
            });
        });

      Bigival product = a * b;
      Sample(a, [&](const BigRat &sa) {
          Sample(b, [&](const BigRat &sb) {
              CHECK_CONTAINS(product, sa * sb);
            });
        });

      // Only when defined and supported.
      if (!b.ContainsZero() &&
          b.LB() != 0 &&
          b.UB() != 0) {
        Bigival quotient = a / b;
        Sample(a, [&](const BigRat &sa) {
            Sample(b, [&](const BigRat &sb) {
                CHECK_CONTAINS(quotient, sa / sb);
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
         Bigival(BigRat(2), BigRat(3), false, false)) == Bigival::MaybeBool::True) <<
        "2 is not included in either interval, so every value is less.";

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

int main(int argc, char **argv) {
  ANSI::Init();

  Simple();
  Special();
  IntervalOps();
  Comparisons();

  printf("OK\n");
  return 0;
}
