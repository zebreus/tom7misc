
#include "big-interval.h"

#include <cstdio>

#include "bignum/big.h"
#include "bignum/big-overloads.h"

#include "ansi.h"

#define CHECK_CONTAINS(ival_exp, v_exp) do {             \
  auto ival = (ival_exp);                                \
  auto v = (v_exp);                                      \
  CHECK(ival.Contains(v)) << "Expected the interval "    \
    #ival_exp " to contain the value " #v_exp << ":\n"   \
    "ival: " << ival.ToString() << "\nvalue: " <<        \
    v.ToString();                                        \
 } while (0)

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

  {
    Bigival a(BigRat(-1), BigRat(2), true, true);
    Bigival b(BigRat(3), BigRat(8), false, false);

    Bigival c = a + b;
    Sample(a, [&](const BigRat &sa) {
        Sample(b, [&](const BigRat &sb) {
            CHECK_CONTAINS(c, sa + sb);
          });
      });
  }

  {
    Bigival a(BigRat(-1), BigRat(2), true, true);
    Bigival b(BigRat(3), BigRat(8), false, false);

    Bigival c = a * b;
    Sample(a, [&](const BigRat &sa) {
        Sample(b, [&](const BigRat &sb) {
            CHECK_CONTAINS(c, sa * sb);
          });
      });
  }

  {
    Bigival a(BigRat(-1), BigRat(2), true, true);
    Bigival b(BigRat(3), BigRat(8), false, false);

    Bigival c = a / b;
    Sample(a, [&](const BigRat &sa) {
        Sample(b, [&](const BigRat &sb) {
            CHECK_CONTAINS(c, sa / sb);
          });
      });
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
        printf("Sample: %s\n", s.ToString().c_str());
        CHECK_CONTAINS(bb, s * s);
      });
  }

}


int main(int argc, char **argv) {
  ANSI::Init();

  Simple();
  Special();

  printf("OK\n");
  return 0;
}
