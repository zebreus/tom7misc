
#include "big-interval.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <optional>
#include <string>
#include <utility>

#include "bignum/big.h"
#include "base/logging.h"

// Returns nullopt if there is no possible intersection.
std::optional<Bigival> Bigival::MaybeIntersection(const Bigival &a,
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


Bigival::MaybeBool Bigival::Eq(const Bigival &r) const {
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

Bigival::MaybeBool Bigival::Less(const Bigival &r) const {
  // This interval entirely less than the other.
  if (ub.r < r.lb.r) return MaybeBool::True;
  if (ub.r == r.lb.r && (!IncludesUB() || !r.IncludesLB()))
    return MaybeBool::True;

  // Other interval entirely less than this.
  if (r.ub.r < lb.r) return MaybeBool::False;
  if (r.ub.r == lb.r && (!r.IncludesUB() || !IncludesLB()))
    return MaybeBool::False;

  // If they are definitely equal, then it is not less.
  if (Singular() && r.Singular() && ub.r == r.ub.r)
    return MaybeBool::False;

  return MaybeBool::Unknown;
}

Bigival::MaybeBool Bigival::LessEq(const Bigival &r) const {
  // Entirely less, or touching (eq).
  if (ub.r <= r.lb.r) return MaybeBool::True;

  if (r.ub.r < lb.r) return MaybeBool::False;
  if (r.ub.r == lb.r && (!r.IncludesUB() || !IncludesLB()))
    return MaybeBool::False;

  return MaybeBool::Unknown;
}

std::string Bigival::ToString() const {
  auto Render = [](const BigRat &r) {
      std::string rs = r.ToString();
      std::string ds = std::format("{:.6g}", r.ToDouble());
      if (rs == ds) {
        return rs;
      } else {
        return std::format("{} ≅ {}", rs, ds);
      }
    };
  return std::format("{}{}, {}{}",
                     IncludesLB() ? "[" : "(",
                     Render(LB()),
                     Render(UB()),
                     IncludesUB() ? "]" : ")");
}

Bigival Bigival::Sqrt(const Bigival &xx, const BigInt &inv_epsilon) {
  if (xx.Singular()) {
    auto [lb, ub] = BigRat::SqrtBounds(xx.LB(), inv_epsilon);
    return Bigival(std::move(lb), std::move(ub), true, true);
  } else {
    auto [lba, uba] = BigRat::SqrtBounds(xx.LB(), inv_epsilon);
    auto [lbb, ubb] = BigRat::SqrtBounds(xx.UB(), inv_epsilon);

    if (SELF_CHECK) {
      // Because it is not singular, and both arguments are
      // non-negative, and sqrt is monotonic.
      CHECK(lba < lbb);
      CHECK(ubb > uba);
    }

    return Bigival(std::move(lba), std::move(ubb), true, true);
  }
}


Bigival Bigival::CoarseIntSqrt(const BigRat &xx) {
  int sign = BigRat::Sign(xx);
  CHECK(sign >= 0) << "Precondition";
  if (sign == 0) return Bigival(0);

  // PERF: Would be nice to avoid copying :/
  auto [nn, dd] = xx.Parts();

  // We could also check for perfect squares (SqrtRem), but
  // these are the most common ones we care about, since we
  // have lots of 1/d and integers.
  bool n_is_one = nn == 1;
  bool d_is_one = dd == 1;
  if (n_is_one && d_is_one) {
    return Bigival(1);
  } else if (n_is_one) {
    BigInt d = BigInt::Sqrt(dd);
    return Bigival(BigRat(BigInt(1), d + 1), BigRat(BigInt(1), d),
                   false, true);

  } else if (d_is_one) {
    BigInt n = BigInt::Sqrt(nn);
    return Bigival(BigRat(n), BigRat(n + 1),
                   true, false);
  }

  // Otherwise, the general case.

  // Floors of the square roots.
  // So then we have n/(d + 1) < sqrt(nn)/sqrt(dd) < (n+1)/d
  BigInt n = BigInt::Sqrt(nn);
  BigInt d = BigInt::Sqrt(dd);

  return Bigival(BigRat(n, d + 1), BigRat(n + 1, d), false, false);
}

Bigival Bigival::CoarseIntSqrt(const BigRat &xx, const BigInt &inv_epsilon) {
  int sign = BigRat::Sign(xx);
  CHECK(sign >= 0) << "Precondition";
  if (sign == 0) return Bigival(0);

  // PERF: Would be nice to avoid copying :/
  auto [nn, dd] = xx.Parts();

  int64_t shift =
    std::max((int64_t)std::ceil((BigInt::LogBase2(BigRat::Ceil(xx)) +
                                 BigInt::LogBase2(inv_epsilon)) * 2.0 -
                                BigInt::LogBase2(dd)),
             int64_t(0));
  // Not strictly necessary, but it is better if we are multiplying
  // by a square power of two.
  if (shift & 1) shift++;

  if (shift > 0) {
    nn <<= shift;
    dd <<= shift;
  }

  // Floors of the square roots.
  // So then we have n/(d + 1) < sqrt(nn)/sqrt(dd) < (n+1)/d
  BigInt n = BigInt::Sqrt(nn);
  BigInt d = BigInt::Sqrt(dd);

  return Bigival(BigRat(n, d + 1), BigRat(n + 1, d), false, false);
}

Bigival Bigival::Sin(const BigRat &x, const BigInt &inv_epsilon) {
  CHECK(BigInt::Sign(inv_epsilon) == 1);
  // This is the one case where we can have an exact rational result.
  // We return open intervals below, so this test is also required
  // for correctness.
  if (BigRat::Sign(x) == 0) return Bigival(0);

  BigRat sum(0);

  const BigRat x_squared = x * x;

  // Taylor series is x^1/1! - x^3/3! + x^5/5! - ...
  //                  term_0   term_1   term_2
  BigRat current_term = x;

  const BigRat epsilon{BigInt(1), inv_epsilon};

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


Bigival Bigival::Cos(const BigRat &x, const BigInt &inv_epsilon) {
  CHECK(BigRat::Sign(inv_epsilon) == 1);
  // This is the one case where we can have an exact rational result.
  // We return open intervals below, so this test is also required
  // for correctness.
  if (BigRat::Sign(x) == 0) return Bigival(1);

  const BigRat epsilon{BigInt(1), inv_epsilon};

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

Bigival Bigival::Sin(const BigInt &inv_epsilon) const {
  BigRat width = Width();
  if (width == 0) {
    return Sin(LB(), inv_epsilon);
  }

  // Endpoints bound the result unless it contains a peak or
  // trough.
  const BigInt double_inv_epsilon = inv_epsilon * 2;
  Bigival a = Sin(lb.r, double_inv_epsilon);
  Bigival b = Sin(ub.r, double_inv_epsilon);
  Point lower = MinOr(a.lb, b.lb);
  Point upper = MaxOr(a.ub, b.ub);

  // The tricky part is that we might have a peak or trough
  // of the sine function inside that interval, so the endpoints
  // are not the max/min value. Peaks happen at π * (2k + 0.5)
  // and troughs happen at π * (2k - 0.5).

  // Use an approximation of pi that is accurate compared to the
  // width of the current interval.
  Bigival pi = Pi(width.Denominator() * 1024);

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
  Bigival kpeak = Div(pi).Minus(BigRat(1, 2)).Div(Bigival(2));
  // And similar for troughs.
  Bigival ktrough = Div(pi).Plus(BigRat(1, 2)).Div(Bigival(2));
  if (kpeak.ContainsInteger()) {
    // printf(AYELLOW("Interval %s had integer") ".\n", kpeak.ToString().c_str());
    upper = Point(BigRat(1), true);
  }
  if (ktrough.ContainsInteger()) {
    lower = Point(BigRat(-1), true);
  }

  return Bigival(lower, upper);
}


Bigival Bigival::Cos(const BigInt &inv_epsilon) const {
  BigRat width = Width();
  if (width == 0) {
    return Cos(LB(), inv_epsilon);
  }

  // Same approach as Sin(), except that the peaks/troughs
  // appear at different points.
  const BigInt double_inv_epsilon = inv_epsilon * 2;
  Bigival a = Cos(lb.r, double_inv_epsilon);
  Bigival b = Cos(ub.r, double_inv_epsilon);
  Point lower = MinOr(a.lb, b.lb);
  Point upper = MaxOr(a.ub, b.ub);

  Bigival pi = Pi(width.Denominator() * 1024);
  if (width >= pi.UB() * 2) {
    return Bigival(-1, 1, true, true);
  }

  // Peaks of cos(x) = 1 occur at x = 2kπ.
  Bigival kpeak = Div(pi.Times(BigRat(2)));
  // Troughs of cos(x) = -1 occur at x = (2k+1)π.
  // Solving for k: k = (x/π - 1) / 2.
  Bigival ktrough = Div(pi).Minus(BigRat(1)).Div(Bigival(2));
  if (kpeak.ContainsInteger()) {
    upper = Point(BigRat(1), true);
  }
  if (ktrough.ContainsInteger()) {
    lower = Point(BigRat(-1), true);
  }

  return Bigival(lower, upper);
}


Bigival Bigival::NiceSin(const BigRat &r, const BigInt &inv_epsilon) {
  // PERF Can avoid recomputing this over and over.
  BigInt fine_epsilon = (inv_epsilon * inv_epsilon) << 2;

  Bigival fine_sin = Bigival::Sin(r, fine_epsilon);

  // We use the whole interval even if there's a simple fraction
  // in the middle. It's not easy to test which side the true value
  // falls into. So this can return an interval with width up to
  // 2/inv_epsilon. We pass in inv_epsilon/2 so that the target
  // error is still inv_epsilon.
  // Note potential round-off error; we don't actually have any
  // formal requirement on the intervals except that they are small
  // and get smaller as we subdivide. So this doesn't affect correctness.
  auto tpl = BigRat::SimplifyInterval(fine_sin.LB(), fine_sin.UB(),
                                      inv_epsilon >> 1);

  // Sin can never be outside [-1, 1] and SimplifyInterval doesn't
  // really guarantee that it wouldn't expand past these points
  // (though you would expect it to choose the intervals!), so enforce
  // that bound here.
  return Bigival(BigRat::Max(std::move(std::get<0>(tpl)), BigRat(-1)),
                 BigRat::Min(std::move(std::get<2>(tpl)), BigRat(1)),
                 true, true);
}

Bigival Bigival::NiceCos(const BigRat &r, const BigInt &inv_epsilon) {
  // PERF Can avoid recomputing this over and over.
  BigInt fine_epsilon = (inv_epsilon * inv_epsilon) << 2;

  Bigival fine_cos = Bigival::Cos(r, fine_epsilon);

  auto tpl = BigRat::SimplifyInterval(fine_cos.LB(), fine_cos.UB(),
                                      inv_epsilon >> 1);

  return Bigival(BigRat::Max(std::move(std::get<0>(tpl)), BigRat(-1)),
                 BigRat::Min(std::move(std::get<2>(tpl)), BigRat(1)),
                 true, true);
}

Bigival Bigival::NiceSin(const BigInt &inv_epsilon) const {
  // PERF Can avoid recomputing this over and over.
  BigInt fine_epsilon = (inv_epsilon * inv_epsilon) << 2;
  Bigival fine_sin = Sin(fine_epsilon);
  auto tpl = BigRat::SimplifyInterval(fine_sin.LB(), fine_sin.UB(),
                                      inv_epsilon >> 1);
  return Bigival(BigRat::Max(std::move(std::get<0>(tpl)), BigRat(-1)),
                 BigRat::Min(std::move(std::get<2>(tpl)), BigRat(1)),
                 true, true);
}

Bigival Bigival::NiceCos(const BigInt &inv_epsilon) const {
  // PERF Can avoid recomputing this over and over.
  BigInt fine_epsilon = (inv_epsilon * inv_epsilon) << 2;
  Bigival fine_cos = Cos(fine_epsilon);
  auto tpl = BigRat::SimplifyInterval(fine_cos.LB(), fine_cos.UB(),
                                      inv_epsilon >> 1);
  return Bigival(BigRat::Max(std::move(std::get<0>(tpl)), BigRat(-1)),
                 BigRat::Min(std::move(std::get<2>(tpl)), BigRat(1)),
                 true, true);
}

std::string Bigival::Point::ToString() const {
  std::string rs = r.ToString();
  std::string ds = std::format("{:.6g}", r.ToDouble());
  if (rs == ds) {
    return std::format("{}{}", included ? "⏺" : "∘", rs);
  } else {
    return std::format("{}{} ≅ {}", included ? "⏺" : "∘", rs, ds);
  }
}
