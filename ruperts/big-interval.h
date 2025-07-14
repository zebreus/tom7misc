
// Interval arithmetic with rationals.

// Conceptually, a Bigival represents a specific number but for which
// we only know bounds. The bounds are specified as exact,
// arbitrary-precision rationals. Bounds can be inclusive or
// non-inclusive at each end. Any big rational can be represented
// exactly as a Bigival where the bounds are equal and inclusive. All
// bigivals are non-empty, since they conceptually represent an actual
// quantity. Another informative property is that a value like x * x
// is always positive, even if x's interval includes negative numbers;
// we know that it is the same number from the interval each time.
//
// We support various algebraic operations on these intervals. The
// operations may lose accuracy only in the sense that intervals
// become wider; the true value is always contained in the resulting
// interval.

// Note that some operations can be performed with better accuracy
// with higher-level operations. An example would be x.Squared()
// instead of x*x; we know that x.Squared() is always positive.

// XXX to cc-lib/bignum?
#ifndef _RUPERTS_BIG_INTERVAL_H
#define _RUPERTS_BIG_INTERVAL_H

#include "bignum/big.h"
#include "bignum/big-overloads.h"
#include "base/logging.h"

struct Bigival {
  Bigival(const BigRat &pt) : Bigival(pt, pt, true, true) {}
  Bigival(const BigInt &pt) : Bigival(BigRat(pt)) {}

  Bigival(BigRat lb_in, BigRat ub_in, bool include_lb, bool include_ub) :
    Bigival(Point(std::move(lb_in), include_lb),
            Point(std::move(ub_in), include_ub)) {
    CHECK(lb.r <= ub.r);
    Normalize();
  }

  Bigival Plus(const Bigival &b) const {
    return Bigival(lb + b.lb, ub + b.ub);
  }

  Bigival Minus(const Bigival &b) const {
    return Bigival(lb - b.ub, ub - b.lb);
  }

  Bigival Times(const Bigival &b) const {
    Point x = lb * b.lb;
    Point y = lb * b.ub;
    Point z = ub * b.lb;
    Point w = ub * b.ub;

    return Bigival(Min(Min(x, y), Min(z, w)),
                   Max(Max(x, y), Max(z, w)));
  }

  Bigival Div(const Bigival &b) const {
    return Times(b.Reciprocal());
  }

  // 1/b
  Bigival Reciprocal() const {
    CHECK(!ContainsZero()) << "Division by zero is not defined.";

    CHECK(BigRat::Sign(lb.r) != 0 &&
          BigRat::Sign(ub.r) != 0) << "This makes the interval "
      "arbitrarily large, so it is not handled. It could be supported "
      "by adding an infinity sentinel if it is useful.";

    return Bigival(Reciprocal(lb), Reciprocal(ub));
  }

  Bigival Negate() const {
    return Bigival(-ub.r, -lb.r, IncludesUB(), IncludesLB());
  }

  Bigival Abs() const {
    // I think abs requires case analysis. It is not monotone.
    if (ContainsZero()) {
      return Bigival(Point(BigRat(0), true),
                     Max(lb, ub));
    }

    // Then the sign of the two must be the same.
    const int sign1 = BigRat::Sign(lb.r);
    const int sign2 = BigRat::Sign(ub.r);
    CHECK(sign1 == sign2) << "Bug: Should not be possible!";

    Point alb = Abs(lb);
    Point aub = Abs(ub);

    // PERF: Can avoid comparing twice, copying...
    return Bigival(Min(alb, aub), Max(alb, aub));
  }

  Bigival Squared() const {
    Bigival o = Abs();
    // now we know lb and ub are both nonnegative.
    return Bigival(o.lb * o.lb, o.ub * o.ub);
  }

  const BigRat &LB() const { return lb.r; }
  const BigRat &UB() const { return ub.r; }

  // Same as Contains(0), but faster.
  bool ContainsZero() const {
    // Since we know lb <= ub, the interval can only contain
    // zero if it is of the form [-, 0], [-, +}, or [0, ?}.

    switch (BigRat::Sign(LB())) {
    case -1: {
      const int us = BigRat::Sign(UB());
      return us == 1 || (us == 0 && IncludesUB());
    }

    case 0:
      return lb.included;

    default:
    case 1:
      return false;
    }
  }

  bool Contains(const BigRat &r) const {
    if (IncludesLB()) {
      if (r < LB()) return false;
    } else {
      if (r <= LB()) return false;
    }

    if (IncludesUB()) {
      if (r > UB()) return false;
    } else {
      if (r >= UB()) return false;
    }

    return true;
  }

  bool IncludesLB() const { return lb.included; }
  bool IncludesUB() const { return ub.included; }

  std::string ToString() const {
    return std::format("{}{}, {}{}",
                       IncludesLB() ? "[" : "(",
                       LB().ToString(),
                       LB().ToString(),
                       IncludesUB() ? "]" : ")");
  }

 private:
  struct Point {
    Point(BigRat rr, bool inc) : r(std::move(rr)), included(inc) {}
    BigRat r;
    bool included = false;
  };

  // Notation note: [] are closed endpoints, () are open, and {}
  // are a wildcard over both.

  Bigival(Point lb_in, Point ub_in) : lb(std::move(lb_in)),
                                      ub(std::move(ub_in)) {
    Normalize();
  }

  static Point Abs(const Point &a) {
    switch (BigRat::Sign(a.r)) {
      // Non-negative
    default:
    case 1: return a;
    case 0: return a;
    case -1: return Point(-a.r, a.included);
    }
  }

  friend Point operator +(const Point &a, const Point &b) {
    return Point(a.r + b.r, a.included && b.included);
  }

  friend Point operator -(const Point &a, const Point &b) {
    return Point(a.r - b.r, a.included && b.included);
  }

  friend Point operator *(const Point &a, const Point &b) {
    return Point(a.r * b.r, a.included && b.included);
  }

  static Point Reciprocal(const Point &a) {
    return Point(BigRat::Inverse(a.r), a.included);
  }

  static Point Min(const Point &a, const Point &b) {
    if (a.r == b.r) {
      // this is a.included || b.included, but with the
      // hopes that the compiler will not copy.
      return a.included ? a : b;
    } else {
      return a.r < b.r ? a : b;
    }
  }

  static Point Max(const Point &a, const Point &b) {
    if (a.r == b.r) {
      return a.included ? a : b;
    } else {
      return a.r > b.r ? a : b;
    }
  }

  void Normalize() {
    // If they are equal, since we know the interval is non-empty,
    // we can conclude that the endpoints are included.

    // But: Avoid calling bigrat equality if we won't even do anything
    // with it. This is reached in the common case of point intervals.
    if (!lb.included || !ub.included) {
      if (lb.r == ub.r) {
        lb.included = ub.included = true;
      }
    }
  }

  Point lb, ub;
};

inline Bigival operator+(const Bigival &a, const Bigival &b) {
  return a.Plus(b);
}

inline Bigival operator-(const Bigival &a, const Bigival &b) {
  return a.Minus(b);
}

inline Bigival operator-(const Bigival &a) {
  return a.Negate();
}

inline Bigival operator*(const Bigival &a, const Bigival &b) {
  return a.Times(b);
}

inline Bigival operator/(const Bigival &a, const Bigival &b) {
  return a.Div(b);
}

#endif
