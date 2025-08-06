
#include "big.h"
#include "big-overloads.h"

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

// TODO: When I graduate big-interval into cc-lib, just use
// that.
static std::pair<BigRat, BigRat> Cos(
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
  for (BigInt k(1); true; ++k) {
    if (decreasing) {
      const BigRat error_bound = BigRat::Abs(current_term);
      if (error_bound <= epsilon) {
        if (BigRat::Sign(current_term) > 0) {
          return std::make_pair(sum, sum + current_term);
        } else {
          return std::make_pair(sum + current_term, sum);
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


static int DoSomeMath(ArcFour *rc) {
  BigRat a(RandTo(rc, 100) - 50, 100 + RandTo(rc, 200));

  const auto &[cosa_lb, cosa_ub] = Cos(a, BigInt(131072));

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

struct BenchDef {
  int (*fn)(ArcFour *);
  int num_samples = 1000;
  const char *name;
};

static std::initializer_list<BenchDef> BENCHES = {
  {.fn = &DoSomeMath, .num_samples = 1000, .name = "some_math"},
};

static void RunBench() {
  StatusBar status(1);

  ArcFour rc("bench");
  Periodically status_per(1);
  for (const BenchDef &def : BENCHES) {
    Timer all_time;
    status.Print("Running {}...\n", def.name);
    std::vector<double> samples;
    samples.reserve(def.num_samples);
    for (int iter = 0; iter < def.num_samples; iter++) {
      Timer timer;
      int res = DoSomeMath(&rc);
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
