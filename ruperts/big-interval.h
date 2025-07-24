
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

#include <cstdio>
#include <format>
#include <optional>
#include <string>
#include <utility>

#include "ansi.h"
#include "base/logging.h"
#include "bignum/big-numbers.h"
#include "bignum/big-overloads.h"
#include "bignum/big.h"

struct Bigival {
  Bigival(const BigRat &pt) : Bigival(pt, pt, true, true) {}
  Bigival(const BigInt &pt) : Bigival(BigRat(pt)) {}
  Bigival(int64_t i) : Bigival(BigRat(i)) {}

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
                     Max(alb, aub));
    }

    // Then the sign of the two must be the same.
    const int sign1 = BigRat::Sign(lb.r);
    const int sign2 = BigRat::Sign(ub.r);
    CHECK(sign1 == sign2) << "Bug: Should not be possible!";

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

  // True if the interval contains any exact integer.
  bool ContainsInteger() const {
    // Floor is a little faster than ceil.
    BigInt i = BigRat::Floor(UB());
    if (IncludesUB() && i == UB()) return true;
    --i;
    return IncludesLB() ? i >= LB() : i > LB();
  }

  static Bigival Pi(const BigRat &epsilon) {
    BigRat half_epsilon = epsilon / 2;
    BigRat approx_pi = BigNumbers::Pi(half_epsilon);
    return Bigival(approx_pi - half_epsilon, approx_pi + half_epsilon,
                   false, false);
  }

  // TODO: Maybe we should standardize on inv_epsilon. It's nice
  // to know it has this special form.
  static Bigival Sqrt(const BigRat &a, const BigInt &inv_epsilon) {
    auto [lb, ub] = BigRat::SqrtBounds(a, inv_epsilon);
    return Bigival(std::move(lb), std::move(ub), true, true);
  }

  // Compute sin(x) as a Bigival with a width of no more than epsilon.
  // This is only appropriate for x with small magnitude.
  static Bigival Sin(const BigRat &x, const BigRat &epsilon) {
    CHECK(BigRat::Sign(epsilon) == 1);
    // This is the one case where we can have an exact rational result.
    // We return open intervals below, so this test is also required
    // for correctness.
    if (BigRat::Sign(x) == 0) return Bigival(0);

    BigRat sum(0);

    const BigRat x_squared = x * x;

    // Taylor series is x^1/1! - x^3/3! + x^5/5! - ...
    //                  term_0   term_1   term_2
    BigRat current_term = x;

    bool decreasing = false;
    for (BigInt k(1); true; ++k) {
      // For an alternating series, the next term is also a bound
      // on the absolute error. We also know that the current term is
      // an upper (or lower) bound on the true value. But in order
      // to use this fact, we must be in the phase of the sequence
      // where the terms are decreasing.
      if (decreasing) {
        const BigRat error_bound = BigRat::Abs(current_term);
        if (error_bound <= epsilon) {
          if (BigRat::Sign(current_term) > 0) {
            return Bigival(sum, sum + current_term, false, false);
          } else {
            return Bigival(sum + current_term, sum, false, false);
          }
        }
      }

      sum += current_term;

      // term_k = x^(2k + 1) / (2k + 1)!
      // So to get term_k+1, we want an additional factor of x^2 for the
      // numerator, and a factor of 1 / ((2k + 1) * 2k) for the
      // denominator.

      BigInt two_k = k << 1;

      BigRat next_factor = x_squared / (two_k * (two_k + 1));
      if (!decreasing && next_factor < BigRat(1)) {
        decreasing = true;
      }

      current_term =  BigRat::Negate(std::move(current_term)) * next_factor;
    }
  }

  // This is only appropriate for x with small magnitude.
  static Bigival Cos(const BigRat &x, const BigRat &epsilon) {
    CHECK(BigRat::Sign(epsilon) == 1);
    // This is the one case where we can have an exact rational result.
    // We return open intervals below, so this test is also required
    // for correctness.
    if (BigRat::Sign(x) == 0) return Bigival(1);

    BigRat sum(0);

    const BigRat x_squared = x * x;

    // Taylor series is x^0/0! -  x^2/2! + x^4/4! - ...
    //                  term_0    term_1   term_2
    BigRat current_term(1);

    bool decreasing = false;
    for (BigInt k(1); true; ++k) {
      if (decreasing) {
        const BigRat error_bound = BigRat::Abs(current_term);
        if (error_bound <= epsilon) {
          if (BigRat::Sign(current_term) > 0) {
            return Bigival(sum, sum + current_term, false, false);
          } else {
            return Bigival(sum + current_term, sum, false, false);
          }
        }
      }

      sum += current_term;

      BigInt two_k = k << 1;

      BigRat next_factor = x_squared / (two_k * (two_k - 1));
      if (!decreasing && next_factor < BigRat(1)) {
        decreasing = true;
      }

      current_term = BigRat::Negate(std::move(current_term)) * next_factor;
    }
  }

  Bigival Sin(const BigRat &epsilon) const {
    BigRat width = Width();
    if (width == 0) {
      return Sin(LB(), epsilon);
    }

    // Endpoints bound the result unless it contains a peak or
    // trough.
    const BigRat half_epsilon = epsilon / 2;
    Bigival a = Sin(lb.r, half_epsilon);
    Bigival b = Sin(ub.r, half_epsilon);
    Point lower = Min(a.lb, b.lb);
    Point upper = Max(a.ub, b.ub);

    // The tricky part is that we might have a peak or trough
    // of the sine function inside that interval, so the endpoints
    // are not the max/min value. Peaks happen at π * (2k + 0.5)
    // and troughs happen at π * (2k - 0.5).

    // Use an approximation of pi that is accurate compared to the
    // width of the current interval.
    Bigival pi = Pi(width / 1024);

    // If the interval is big enough, then it contains both a peak and
    // trough for sure. Probably can just rely on the thing below.
    // PERF could use lower-res pi for this
    if (width >= pi.UB() * 2) {
      return Bigival(-1, 1, true, true);
    }

    // Determine k such that π * (2k + 0.5) might fall in the interval.
    // p = π * (2k + 0.5)
    // p/π - 0.5 = 2k
    // (p/π - 0.5)/2 = k
    Bigival kpeak = Div(pi).Minus(BigRat(1, 2)).Div(2);
    // And similar for troughs.
    Bigival ktrough = Div(pi).Plus(BigRat(1, 2)).Div(2);
    if (kpeak.ContainsInteger()) {
      printf(AYELLOW("Interval %s had integer") ".\n", kpeak.ToString().c_str());
      upper = Point(BigRat(1), true);
    }
    if (ktrough.ContainsInteger()) {
      lower = Point(BigRat(-1), true);
    }

    return Bigival(lower, upper);
  }

  Bigival Cos(const BigRat &epsilon) const {
    BigRat width = Width();
    if (width == 0) {
      return Cos(LB(), epsilon);
    }

    // Same approach as Sin(), except that the peaks/troughs
    // appear at different points.
    const BigRat half_epsilon = epsilon / 2;
    Bigival a = Cos(lb.r, half_epsilon);
    Bigival b = Cos(ub.r, half_epsilon);
    Point lower = Min(a.lb, b.lb);
    Point upper = Max(a.ub, b.ub);

    Bigival pi = Pi(width / 1024);
    if (width >= pi.UB() * 2) {
      return Bigival(-1, 1, true, true);
    }

    // Peaks of cos(x) = 1 occur at x = 2kπ.
    Bigival kpeak = Div(pi.Times(2));
    // Troughs of cos(x) = -1 occur at x = (2k+1)π.
    // Solving for k: k = (x/π - 1) / 2.
    Bigival ktrough = Div(pi).Minus(BigRat(1)).Div(2);
    if (kpeak.ContainsInteger()) {
      upper = Point(BigRat(1), true);
    }
    if (ktrough.ContainsInteger()) {
      lower = Point(BigRat(-1), true);
    }

    return Bigival(lower, upper);
  }

  enum class MaybeBool {
    True,
    False,
    Unknown,
  };

  // Returns nullopt if there is no possible intersection.
  static std::optional<Bigival> MaybeIntersection(const Bigival &a,
                                                  const Bigival &b) {
    // No overlap at all
    if (a.ub.r < b.lb.r) {
      return std::nullopt;
    } else if (b.ub.r < a.lb.r) {
      return std::nullopt;
    } else {
      // PERF: Maybe the above logic falls out of this? Check carefully.
      // Note that we only include an endpoint if both arguments include it.
      Point lb = MaxAnd(a.lb, b.lb);
      Point ub = MinAnd(a.ub, b.ub);

      // The intersection is the max of the lower bounds to the min
      // of the upper bounds. But we want to round towards the
      // inside of the interval, since we are intersecting.

      /*
      printf("lb: %s\n"
             "ub: %s\n",
             lb.ToString().c_str(),
             ub.ToString().c_str());
      */
      if (lb.r == ub.r && (!lb.included || !ub.included)) {
        // Empty intersection.
        return std::nullopt;
      }
      return {Bigival(lb, ub)};
    }
  }

  // True if the interval contains just one number; the number
  // is then equal to the LB (and UB).
  bool Singular() const {
    return lb.r == ub.r;
  }

  bool MayBeNegative() const {
    return BigRat::Sign(LB()) == -1;
  }

  bool MayBePositive() const {
    return BigRat::Sign(UB()) == 1;
  }

  // These predicates are talking about the two underlying
  // unknown values, not the intervals.
  MaybeBool Eq(const Bigival &r) const {
    std::optional<Bigival> isect = MaybeIntersection(*this, r);
    if (!isect.has_value()) return MaybeBool::False;
    // The only way we know they are equal is if both sies
    // are singletons. Since we also checked that the intersection
    // is non-empty, this means they are the same.
    if (Singular() && r.Singular()) return MaybeBool::True;
    // Otherwise, they intersect, but we can't conclude the values
    // are equal.
    return MaybeBool::Unknown;
  }

  MaybeBool Less(const Bigival &r) const {
    // This interval entirely less than the other
    if (ub.r < r.lb.r) return MaybeBool::True;
    if (ub.r == r.lb.r && (!IncludesUB() || !r.IncludesLB()))
      return MaybeBool::True;

    // Other interval entirely less than this
    if (r.ub.r < lb.r) return MaybeBool::False;
    if (r.ub.r == lb.r && (!r.IncludesUB() || !IncludesLB()))
      return MaybeBool::False;

    // If they are definitely equal, then it is not less.
    if (Singular() && r.Singular() && ub.r == r.ub.r)
      return MaybeBool::False;

    return MaybeBool::Unknown;
  }

  MaybeBool LessEq(const Bigival &r) const {
    // Entirely less, or touching (eq).
    if (ub.r <= r.lb.r) return MaybeBool::True;

    if (r.ub.r < lb.r) return MaybeBool::False;
    if (r.ub.r == lb.r && (!r.IncludesUB() || !IncludesLB()))
      return MaybeBool::False;

    return MaybeBool::Unknown;
  }

  std::string ToString() const {
    return std::format("{}{}, {}{}",
                       IncludesLB() ? "[" : "(",
                       LB().ToString(),
                       UB().ToString(),
                       IncludesUB() ? "]" : ")");
  }

 private:
  struct Point {
    Point(BigRat rr, bool inc) : r(std::move(rr)), included(inc) {}
    BigRat r;
    bool included = false;
    std::string ToString() const {
      return std::format("{}{}", included ? "⏺" : "∘", r.ToString());
    }
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

  static Point MinAnd(const Point &a, const Point &b) {
    if (a.r == b.r) {
      if (a.included && b.included) return a;
      else return Point(a.r, false);
    } else {
      return a.r < b.r ? a : b;
    }
  }

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
