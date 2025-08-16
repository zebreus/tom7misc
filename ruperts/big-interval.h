
// Interval arithmetic with rationals.

// Conceptually, a Bigival represents a specific number but for which
// we only know bounds. The bounds are specified as exact,
// arbitrary-precision rationals. Bounds can be inclusive or
// non-inclusive at each end. Any big rational can be represented
// exactly as a Bigival where the bounds are equal and inclusive. All
// bigivals are non-empty, since they conceptually represent an actual
// quantity. Another informative property is that a value like
// x.Squared() is always positive, even if x's interval includes
// negative numbers; we know that it is the same number from the
// interval each time.
//
// We support various algebraic operations on these intervals. The
// operations may lose accuracy only in the sense that intervals
// become wider; the true value is always contained in the resulting
// interval.

// Note that some operations can be performed with better accuracy
// with higher-level operations. An example would be x.Squared()
// instead of x*x; we know that x.Squared() is always positive.

// Functions that may need to estimate irrationals take a target
// precision. This is described as 1/inv_epsilon. Note that the
// resulting intervals are not guaranteed to be as small as
// 1/inv_epsilon in width, unless specified. (This library should
// strive to make this guarantee, but it is not currently satisfied,
// with bounds like 2/inv_epsilon being common.) In all cases, the
// interval width will decrease as inv_epsilon increases; this is
// usually all you need.

// XXX to cc-lib/bignum
#ifndef _RUPERTS_BIG_INTERVAL_H
#define _RUPERTS_BIG_INTERVAL_H

#include <optional>
#include <string>
#include <utility>
#include <cstdint>

#include "base/logging.h"
#include "bignum/big-numbers.h"
#include "bignum/big-overloads.h"
#include "bignum/big.h"

struct Bigival {
  static constexpr bool SELF_CHECK = true;

  Bigival() : Bigival(0) {}
  explicit Bigival(const BigRat &pt) : Bigival(pt, pt, true, true) {}
  explicit Bigival(const BigInt &pt) : Bigival(BigRat(pt)) {}
  explicit Bigival(int64_t i) : Bigival(BigRat(i)) {}

  Bigival(BigRat lb_in, BigRat ub_in, bool include_lb, bool include_ub) :
    Bigival(Point(std::move(lb_in), include_lb),
            Point(std::move(ub_in), include_ub)) {
    CHECK(lb.r <= ub.r) << lb.r.ToString() << " " << ub.r.ToString();
    Normalize();
  }
  Bigival(BigInt lb, BigInt ub, bool include_lb, bool include_ub) :
    Bigival(BigRat(std::move(lb)), BigRat(std::move(ub)),
            include_lb, include_ub) {}
  Bigival(int64_t lb, int64_t ub, bool include_lb, bool include_ub) :
    Bigival(BigRat(lb), BigRat(ub), include_lb, include_ub) {}

  Bigival Plus(const Bigival &b) const {
    return Bigival(lb + b.lb, ub + b.ub);
  }

  Bigival Plus(const BigRat &b) const {
    return Bigival(LB() + b, UB() + b, IncludesLB(), IncludesUB());
  }

  Bigival Minus(const Bigival &b) const {
    return Bigival(lb - b.ub, ub - b.lb);
  }

  Bigival Minus(const BigRat &b) const {
    return Bigival(LB() - b, UB() - b, IncludesLB(), IncludesUB());
  }

  Bigival Times(const Bigival &b) const {
    Point x = lb * b.lb;
    Point y = lb * b.ub;
    Point z = ub * b.lb;
    Point w = ub * b.ub;

    return Bigival(MinOr(MinOr(x, y), MinOr(z, w)),
                   MaxOr(MaxOr(x, y), MaxOr(z, w)));
  }

  // When we know that one argument is exact, we can save some steps.
  Bigival Times(const BigRat &b) const {
    switch (BigRat::Sign(b)) {
    default:
    case 0:
      return Bigival(0);
    case -1:
      // Endpoint order is swapped when negated.
      return Bigival(UB() * b, LB() * b, IncludesUB(), IncludesLB());
    case 1:
      return Bigival(LB() * b, UB() * b, IncludesLB(), IncludesUB());
    }
  }

  Bigival Div(const Bigival &b) const {
    return Times(b.Reciprocal());
  }

  Bigival Div(const BigRat &b) const {
    CHECK(!BigRat::IsZero(b)) << "Division by zero is not defined.";
    return Times(BigRat::Inverse(b));
  }

  // Width of the interval, ignoring the open/closedness of endpoints.
  BigRat Width() const {
    return UB() - LB();
  }

  // 1/b
  Bigival Reciprocal() const {
    CHECK(!ContainsZero()) << "Division by zero is not defined.";

    CHECK(BigRat::Sign(lb.r) != 0 &&
          BigRat::Sign(ub.r) != 0) << "This makes the interval "
      "arbitrarily large, so it is not handled. It could be supported "
      "by adding an infinity sentinel if it is useful.";

    return Bigival(Reciprocal(ub), Reciprocal(lb));
  }

  Bigival Negate() const {
    return Bigival(-ub.r, -lb.r, IncludesUB(), IncludesLB());
  }

  Bigival Abs() const {
    Point alb = Abs(lb);
    Point aub = Abs(ub);

    // I think abs requires case analysis. It is not monotone.
    if (ContainsZero()) {
      return Bigival(Point(BigRat(0), true),
                     MaxOr(alb, aub));
    }

    if (SELF_CHECK) {
      // First note the possibility of an open interval at zero.
      const int sign1 = BigRat::Sign(lb.r);
      const int sign2 = BigRat::Sign(ub.r);
      if (sign1 == 0) {
        CHECK(!IncludesLB());
      }
      if (sign2 == 0) {
        CHECK(!IncludesUB());
      }
      if (sign1 != 0 && sign2 != 0) {
        CHECK(sign1 == sign2) << "Bug: Should not be possible! "
          "We had this:\n" << ToString() << "\nWith alb: " <<
          alb.ToString() << "\nAnd  aub: " << aub.ToString();
      }
    }

    // PERF: Can avoid comparing twice, copying...
    return Bigival(MinOr(alb, aub), MaxOr(alb, aub));
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

  bool ContainsOrApproachesZero() const {
    const int ls = BigRat::Sign(LB());
    const int us = BigRat::Sign(UB());

    // Open endpoints mean it approaches zero.
    if (ls == 0 || us == 0) return true;

    // Otherwise, check if the interval spans zero.
    return ls != us;
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

  // True if the interval contains any exact integer.
  bool ContainsInteger() const {
    // Floor is a little faster than ceil.
    BigInt i = BigRat::Floor(UB());
    if (i == UB()) {
      if (IncludesUB()) return true;
      --i;
    }
    return IncludesLB() ? i >= LB() : i > LB();
  }

  static Bigival Pi(const BigInt &inv_epsilon) {
    BigRat half_epsilon = BigRat(BigInt(1), inv_epsilon * 2);
    BigRat approx_pi = BigNumbers::Pi(half_epsilon);
    return Bigival(approx_pi - half_epsilon, approx_pi + half_epsilon,
                   false, false);
  }

  // Computes the square root of the interval; the interval width
  // will be about 2/inv_epsilon.
  static Bigival Sqrt(const Bigival &xx, const BigInt &inv_epsilon);

  // Faster square root approximation. sqrt(n/d) = sqrt(n)/sqrt(d),
  // so we separately compute integer square roots for n and d.
  // The resulting rationals thus have complexity related to the
  // input complexity (and in fact, significantly smaller).
  //
  // This is good when the interval represents a tiny error term with
  // a big denominator and you don't want the expression to blow up.
  static Bigival CoarseIntSqrt(const BigRat &xx);

  // Like the previous, but with an error target. This works by
  // multiplying the numerator and denominator by a power of two,
  // so it's like operating on a fixed-point representation with
  // that many bits. Bounds will be correct, but does not guarantee
  // that the width is less than 1/inv_epsilon.
  static Bigival CoarseIntSqrt(const BigRat &xx, const BigInt &inv_epsilon);

  // Compute sin(x) as a Bigival with a width of no more than 1/inv_epsilon.
  //
  // Careful: This is only appropriate for x with small magnitude (will
  // be very slow otherwise; remove multiples of 2π before calling).
  static Bigival Sin(const BigRat &x, const BigInt &inv_epsilon);
  static Bigival Cos(const BigRat &x, const BigInt &inv_epsilon);

  // Both endpoints must have small magnitude.
  Bigival Sin(const BigInt &inv_epsilon) const;
  Bigival Cos(const BigInt &inv_epsilon) const;

  // More expensive than the above, but produces higher quality
  // intervals. Good when you will use the result multiple times.
  static Bigival NiceSin(const BigRat &r, const BigInt &inv_epsilon);
  static Bigival NiceCos(const BigRat &r, const BigInt &inv_epsilon);

  Bigival NiceSin(const BigInt &inv_epsilon) const;
  Bigival NiceCos(const BigInt &inv_epsilon) const;

  // Returns nullopt if there is no possible intersection.
  static std::optional<Bigival> MaybeIntersection(
      const Bigival &a, const Bigival &b);

  // Return the smallest interval that contains both intervals.
  static Bigival Union(const Bigival &a, const Bigival &b) {
    return Bigival(MinOr(a.lb, b.lb), MaxOr(a.ub, b.ub));
  }

  // The max() function on the underlying values.
  static Bigival Max(const Bigival &a, const Bigival &b) {
    return Bigival(MaxAnd(a.lb, b.lb), MaxOr(a.ub, b.ub));
  }

  // The min() function on the underlying values.
  static Bigival Min(const Bigival &a, const Bigival &b) {
    return Bigival(MinOr(a.lb, b.lb), MinAnd(a.ub, b.ub));
  }

  // True if the interval contains just one number; the number
  // is then equal to the LB (and UB).
  bool Singular() const {
    return lb.r == ub.r;
  }

  BigRat Midpoint() const {
    if (lb.r == ub.r) return lb.r;
    return (lb.r + ub.r) / 2;
  }

  bool MightBeNegative() const {
    return BigRat::Sign(LB()) == -1;
  }

  bool MightBePositive() const {
    return BigRat::Sign(UB()) == 1;
  }

  // These predicates are talking about the two underlying
  // unknown values, not the intervals.
  //
  // Eq(x, y) is only True if both intervals are singular and
  // represent the same exact value. For overlapping, non-singular
  // intervals, Eq returns Unknown. To test whether two intervals
  // *might* be equal (i.e., they overlap), check Eq(x, y) != False.
  enum class MaybeBool {
    True,
    False,
    Unknown,
  };

  MaybeBool Eq(const Bigival &r) const;
  MaybeBool Less(const Bigival &r) const;
  MaybeBool LessEq(const Bigival &r) const;

  std::string ToString() const;

 private:
  struct Point {
    Point(BigRat rr, bool inc) : r(std::move(rr)), included(inc) {}
    BigRat r;
    bool included = false;
    std::string ToString() const;
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

  // Careful: There are two notions of Min, depending on how
  // it will be used. They differ on whether the result is
  // included if the two points have the same value. Here
  // the point is inclusive if eithe argument is inclusive.
  static Point MinOr(const Point &a, const Point &b) {
    if (a.r == b.r) {
      // this is a.included || b.included, but with the
      // hopes that the compiler will not copy.
      return a.included ? a : b;
    } else {
      return a.r < b.r ? a : b;
    }
  }

  static Point MaxOr(const Point &a, const Point &b) {
    if (a.r == b.r) {
      return a.included ? a : b;
    } else {
      return a.r > b.r ? a : b;
    }
  }

  // Endpoints must both be included for the result to be included.
  static Point MinAnd(const Point &a, const Point &b) {
    if (a.r == b.r) {
      if (a.included && b.included) return a;
      else return Point(a.r, false);
    } else {
      return a.r < b.r ? a : b;
    }
  }

  // Endpoints must both be included for the result to be included.
  static Point MaxAnd(const Point &a, const Point &b) {
    if (a.r == b.r) {
      if (a.included && b.included) return a;
      else return Point(a.r, false);
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

inline Bigival operator+(const Bigival &a, const BigRat &b) {
  return a.Plus(b);
}

inline Bigival operator+(const BigRat &a, const Bigival &b) {
  return b.Plus(a);
}

inline Bigival operator-(const Bigival &a, const Bigival &b) {
  return a.Minus(b);
}

inline Bigival operator-(const Bigival &a, const BigRat &b) {
  return a.Minus(b);
}

inline Bigival operator-(const Bigival &a) {
  return a.Negate();
}

inline Bigival operator*(const Bigival &a, const Bigival &b) {
  return a.Times(b);
}

inline Bigival operator*(const Bigival &a, const BigRat &b) {
  return a.Times(b);
}

inline Bigival operator*(const BigRat &a, const Bigival &b) {
  return b.Times(a);
}

inline Bigival operator/(const Bigival &a, const Bigival &b) {
  return a.Div(b);
}

inline Bigival operator/(const Bigival &a, const BigRat &b) {
  return a.Div(b);
}

inline Bigival::MaybeBool operator ==(const Bigival &a, const Bigival &b) {
  return a.Eq(b);
}

inline Bigival::MaybeBool operator <(const Bigival &a, const Bigival &b) {
  return a.Less(b);
}

inline Bigival::MaybeBool operator <=(const Bigival &a, const Bigival &b) {
  return a.LessEq(b);
}

inline Bigival::MaybeBool operator >(const Bigival &a, const Bigival &b) {
  return b.Less(a);
}

inline Bigival::MaybeBool operator >=(const Bigival &a, const Bigival &b) {
  return b.LessEq(a);
}

#endif
