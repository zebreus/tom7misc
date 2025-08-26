
#include "big.h"
#include "big-overloads.h"

#include <format>
#include <initializer_list>
#include <utility>
#include <vector>

#include "ansi.h"
#include "arcfour.h"
#include "base/do-not-optimize.h"
#include "base/logging.h"
#include "periodically.h"
#include "randutil.h"
#include "stats.h"
#include "status-bar.h"
#include "timer.h"


namespace {
struct BigVec2i {
  BigVec2i(BigInt x, BigInt y) :
    x(std::move(x)), y(std::move(y)) {}
  BigVec2i() {}
  BigInt x = BigInt(0), y = BigInt(0);
};
}  // namespace

inline BigVec2i operator +(const BigVec2i &a, const BigVec2i &b) {
  return BigVec2i(a.x + b.x, a.y + b.y);
}


// Add two BigVec2, but the x coordinate is unused.
// I currently don't know a way to get it to discard the unused addition!
static int BigVec2Unused(ArcFour *rc) {
  BigVec2i v1{
    BigInt("12345678901234567890713894719087234711"),
    BigInt("78239741987239847198273491827349827341"),
  };

  BigVec2i v2{
    BigInt("917823847198273498179485719387459183751"),
    BigInt("1279038479182739487192837981739075401278341"),
  };

  BigVec2i vc = v1 + v2;
  int s = vc.y.IsEven() ? 3 : 0;
  DoNotOptimize(s);
  return s;
}

static std::pair<BigRat, BigRat> CosCtrl(
    const BigRat &x, const BigInt &inv_epsilon) {
  CHECK(BigRat::Sign(inv_epsilon) == 1);
  // This is the one case where we can have an exact rational result.
  if (BigRat::Sign(x) == 0) return std::make_pair(BigRat(1), BigRat(1));

  const BigRat epsilon{BigInt(1), inv_epsilon};

  BigRat sum(0);

  const BigRat x_squared = x * x;

  // Taylor series is x^0/0! -  x^2/2! + x^4/4! - ...
  //                  term_0    term_1   term_2
  BigRat current_term(1);

  bool decreasing = false;

  // This is 2k * (2k - 1), which we compute incrementally
  // (strength reduction).
  //
  // Denominator should be 2k * (2k - 1) = 4k^2 - 2k.
  // So the difference between consecutive terms is
  //     4(k+1)^2 - 2(k+1)  -  (4k^2 - 2k)
  //  =  4k^2 + 6k + 2      -  4k^2 + 2k
  //  =  8k + 2
  //
  // So each time the factor increases by 8k + 2. But we
  // can strength-reduce THAT, to see that the increment
  // increases by 8 each time.
  BigInt factor_denom(2);
  // Post-increment will save us one addition.
  BigInt increment(10);
  for (;;) {
    if (decreasing) {
      const BigRat error_bound = BigRat::Abs(current_term);
      if (error_bound <= epsilon) {
        if (BigRat::Sign(current_term) > 0) {
          BigRat next = std::move(current_term) + sum;
          return std::make_pair(std::move(sum), std::move(next));
        } else {
          BigRat next = std::move(current_term) + sum;
          return std::make_pair(std::move(next), std::move(sum));
        }
      }
    }

    sum += current_term;

    // Testing (x^2 / next_factor) < 1. Since x^2 is non-negative,
    // x^2 < next_factor...
    BigRat next_factor = x_squared / factor_denom;
    if (!decreasing && next_factor < BigRat(1)) {
      decreasing = true;
    }

    factor_denom += increment;
    increment += 8;

    current_term = BigRat::Negate(std::move(current_term)) * next_factor;
  }
}

// TODO: When I graduate big-interval into cc-lib, just use
// that.
static std::pair<BigRat, BigRat> CosTest(
    const BigRat &x, const BigInt &inv_epsilon) {
  CHECK(BigRat::Sign(inv_epsilon) == 1);
  // This is the one case where we can have an exact rational result.
  if (BigRat::Sign(x) == 0) return std::make_pair(BigRat(1), BigRat(1));

  const BigRat epsilon{BigInt(1), inv_epsilon};

  BigRat sum(0);

  const BigRat x_squared = x * x;

  // Taylor series is x^0/0! -  x^2/2! + x^4/4! - ...
  //                  term_0    term_1   term_2
  BigRat current_term(1);

  bool decreasing = false;

  // This is 2k * (2k - 1), which we compute incrementally
  // (strength reduction).
  //
  // Denominator should be 2k * (2k - 1) = 4k^2 - 2k.
  // So the difference between consecutive terms is
  //     4(k+1)^2 - 2(k+1)  -  (4k^2 - 2k)
  //  =  4k^2 + 6k + 2      -  4k^2 + 2k
  //  =  8k + 2
  //
  // So each time the factor increases by 8k + 2. But we
  // can strength-reduce THAT, to see that the increment
  // increases by 8 each time.
  BigInt factor_denom(2);
  // Post-increment will save us one addition.
  BigInt increment(10);
  for (;;) {
    if (decreasing) {
      // const BigRat error_bound = BigRat::Abs(current_term);
      // if (error_bound <= epsilon) ...
      if (BigRat::Sign(current_term) > 0) {
        if (current_term <= epsilon) {
          BigRat next = std::move(current_term) + sum;
          return std::make_pair(std::move(sum), std::move(next));
        }
      } else {
        if (epsilon <= current_term) {
          BigRat next = std::move(current_term) + sum;
          return std::make_pair(std::move(next), std::move(sum));
        }
      }
    }

    sum += current_term;

    BigRat next_factor = x_squared / factor_denom;
    if (!decreasing && next_factor < BigInt(1)) {
      decreasing = true;
    }

    factor_denom += increment;
    increment += 8;

    current_term = BigRat::Negate(std::move(current_term)) * next_factor;
  }
}

// Cosine. Ensure numbers are big by asking for 250 digits.
static int OnlyCosCtrl(ArcFour *rc) {
  const int d = 100 + RandTo(rc, 200);
  BigRat a(RandTo(rc, 100) - 50, d);
  BigInt inv_epsilon = BigInt::Pow(BigInt(10), 250);
  const auto &[cosa_lb, cosa_ub] = CosCtrl(a, inv_epsilon);
  CHECK(cosa_lb <= cosa_ub);
  DoNotOptimize(cosa_lb);
  return d;
}

// Cosine. Ensure numbers are big by asking for 250 digits.
static int OnlyCosTest(ArcFour *rc) {
  const int d = 100 + RandTo(rc, 200);
  BigRat a(RandTo(rc, 100) - 50, d);
  BigInt inv_epsilon = BigInt::Pow(BigInt(10), 250);
  const auto &[cosa_lb, cosa_ub] = CosTest(a, inv_epsilon);
  CHECK(cosa_lb <= cosa_ub);
  DoNotOptimize(cosa_lb);
  return d;
}

static int OnlySqrtBounds(ArcFour *rc) {
  const int d = 100 + RandTo(rc, 200);
  // Must be non-negative.
  BigRat a(RandTo(rc, 100) + 1, d);
  BigInt inv_epsilon = BigInt::Pow(BigInt(10), 250);
  const auto &[lb, ub] = BigRat::SqrtBounds(a, inv_epsilon);
  CHECK(lb <= ub);
  DoNotOptimize(ub);
  return d;
}

static int DoSomeMath(ArcFour *rc) {
  BigRat a(RandTo(rc, 100) - 50, 100 + RandTo(rc, 200));

  const auto &[cosa_lb, cosa_ub] = CosCtrl(a, BigInt(131072));

  CHECK(cosa_lb <= cosa_ub);

  BigRat x = cosa_lb * BigRat(3, 55) + BigRat(-12345, 777);

  if (BigRat::Sign(x) == 1) x = std::move(x) + BigRat(1);

  std::vector<BigInt> small_primes;
  for (int i = 0; i < 131072; i++) {
    BigInt bi(i);
    if (BigInt::IsPrime(bi)) {
      small_primes.emplace_back(std::move(bi));
    }
  }

  BigInt primorial(1);
  for (const BigInt &p : small_primes) {
    primorial *= p;
  }

  BigRat z = x / primorial;

  return (int)z.ToString().size();
}

static int SmallMath(ArcFour *rc) {
  BigInt b;
  for (BigInt a(1); a < 31337; ++a) {
    b += a;
  }

  DoNotOptimize(b);
  return b.IsEven();
}

static int PlusEq(ArcFour *rc) {
  BigInt b("198273498172390487102938741098723419027834");
  for (BigInt a(1); a < 31337; ++a) {
    b += a;
  }

  DoNotOptimize(b);
  return b.IsEven();
}

static int BigIntAbs(ArcFour *rc) {
  BigInt b("-198273498172390487102938741098723419027834");
  for (BigInt a(1); a < 31337; ++a) {
    b = BigInt::Abs(std::move(b));
  }

  DoNotOptimize(b);
  return b.IsEven();
}


namespace {
struct BenchDef {
  int (*fn)(ArcFour *);
  int num_samples = 1000;
  const char *name;
};
}

static std::initializer_list<BenchDef> BENCHES = {
  {.fn = &OnlyCosCtrl, .num_samples = 50000, .name = "only_cos_ctrl"},
  {.fn = &OnlyCosTest, .num_samples = 50000, .name = "only_cos_test"},
  {.fn = &OnlySqrtBounds, .num_samples = 10000, .name = "only_sqrt_bounds"},
  {.fn = &DoSomeMath, .num_samples = 1000, .name = "some_math"},
  {.fn = &BigVec2Unused, .num_samples = 5000000, .name = "bigvec2_unused"},
  {.fn = &SmallMath, .num_samples = 10000, .name = "small_math"},
  {.fn = &PlusEq, .num_samples = 10000, .name = "plus_eq"},
  {.fn = &BigIntAbs, .num_samples = 10000, .name = "bigint_abs"},
};

static void RunBench() {
  StatusBar status(1);

  Periodically status_per(1);
  for (const BenchDef &def : BENCHES) {
    // Some stuff might depend on the specific numbers chosen,
    // so use a deterministic sequence. This also makes it
    // possible to compare a before/after version of a function.
    ArcFour rc("bench");

    Timer all_time;
    status.Print("Running {}...\n", def.name);
    std::vector<double> samples;
    samples.reserve(def.num_samples);
    for (int iter = 0; iter < def.num_samples; iter++) {
      Timer timer;
      int res = (*def.fn)(&rc);
      DoNotOptimize(res);
      samples.push_back(timer.Seconds());

      status_per.RunIf([&]() {
          status.Status("[{}] {}/{}",
                        def.name, iter, def.num_samples);
        });
    }

    Stats::Gaussian gauss = Stats::EstimateGaussian(samples);
    status.Print("[{}] Done in {}. Mean: {} ± {} ",
                 def.name,
                 ANSI::Time(all_time.Seconds()),
                 ANSI::Time(gauss.mean),
                 ANSI::Time(gauss.PlusMinus99()));
  }
}


int main(int argc, char **argv) {
  ANSI::Init();

  RunBench();

  return 0;
}
