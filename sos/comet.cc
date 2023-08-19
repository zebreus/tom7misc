
// Another attempt (following orbit.cc) of solving two simultaneously.
// Here, since we know we want a to be the same in each equation,
// we simply maintain that to be true.

#include <string>
#include <vector>
#include <cstdint>
#include <unordered_set>

#include "ansi.h"
#include "timer.h"
#include "periodically.h"
#include "bignum/big.h"
#include "bignum/big-overloads.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "hashing.h"
#include "opt/opt.h"
#include "image.h"
#include "bounds.h"
#include "arcfour.h"
#include "randutil.h"

#include "sos-util.h"
#include "bhaskara-util.h"

using namespace std;

static constexpr bool CHECK_INVARIANTS = true;
static constexpr bool VERBOSE = false;
static constexpr bool VERY_VERBOSE = VERBOSE && false;
static constexpr bool GENERATE_IMAGE = true;
static constexpr int MAX_ITERS = -1;

#define TERM_A AFGCOLOR(39, 179, 214, "%s")
#define TERM_M AFGCOLOR(39, 214, 179, "%s")
#define TERM_B AFGCOLOR(232, 237, 173, "%s")
#define TERM_K AFGCOLOR(220, 173, 237, "%s")
#define TERM_N AFGCOLOR(227, 198, 143, "%s")
#define TERM_SQRTN AFGCOLOR(210, 200, 180, "%s")
#define TERM_E AFGCOLOR(200, 120, 140, "%s")

struct BK {
  BK() : BK(0, 0) {}
  BK(BigInt bb, BigInt kk) : b(std::move(bb)),
                             k(std::move(kk)) {}
  BK(int64_t bb, int64_t kk) : b(bb), k(kk) {}
  inline void Swap(BK *other) {
    b.Swap(&other->b);
    k.Swap(&other->k);
  }

  BK(const BK &other) : b(other.b), k(other.k) {}

  BK &operator =(const BK &other) {
    // Self-assignment does nothing.
    if (this == &other) return *this;
    b = other.b;
    k = other.k;
    return *this;
  }
  BK &operator =(BK &&other) {
    // other must remain valid.
    Swap(&other);
    return *this;
  }

  BigInt b, k;
};

struct HashBK {
  size_t operator()(const BK &bk) const {
    return (size_t)(BigInt::LowWord(bk.k) * 0x314159 +
                    BigInt::LowWord(bk.b));
  }
};

namespace std {
template <> struct hash<BK> {
  size_t operator()(const BK &bk) const {
    return HashBK()(bk);
  }
};
}

static inline bool operator ==(const BK &x, const BK &y) {
  return x.k == y.k && x.b == y.b;
}

// For sol = (a, b), compute k such that n * a^2 + k = b^2.
static BigInt Error(const BigInt &n, const BigInt &a, const BigInt &b) {
  return b * b - n * a * a;
}

using State = std::tuple<BigInt, BK, BK>;
using StateHash = Hashing<State>;

using StateSet = std::unordered_set<State, StateHash>;

// Given two equations of the form
//     n1a^2 + k1 = b1^2
//     n2a^2 + k2 = b2^2
// with n fixed, try to find a, b1, b2 that minimize k1 and k2.
static std::tuple<BigInt, BK, BK>
Comet(BigInt nleft, BigInt nright,
      BigInt a,
      BK left, BK right) {
  ArcFour rc(StringPrintf("comet.%llx", (uint64_t)time(nullptr)));

  BigInt sqrtnleft = BigInt::Sqrt(nleft);
  BigInt sqrtnright = BigInt::Sqrt(nright);

  if (VERBOSE) {
    printf(AWHITE(ABGCOLOR(0, 0, 50, "---------- on %s,%s ------------")) "\n",
           nleft.ToString().c_str(),
           nright.ToString().c_str());
  }

  Periodically image_per(10.0 * 60.0);
  // run after the first minute for quick feedback on experiments
  image_per.SetPeriodOnce(60.0);
  int image_idx = 0;

  Periodically bar_per(5.0);

  Timer timer;

  // Pair of states that we've already tried. We avoid exploring them
  // a second time, as this would lead to a cycle.
  StateSet seen;

  std::vector<State> history;
  BigInt best_total_k{1000};

  for (int iters = 0; true; iters ++) {
    if (CHECK_INVARIANTS) {
      CHECK(!seen.contains(std::make_tuple(a, left, right)));
    }
    seen.insert(std::make_tuple(a, left, right));
    if (GENERATE_IMAGE) {
      history.emplace_back(a, left, right);
    }

    // Only output progress bar when verbose mode is off.
    bar_per.RunIf([&]() {
        int max_b_digits = std::max(left.b.ToString().size(),
                                    right.b.ToString().size());
        int k_digits =
          (BigInt::Abs(left.k) + BigInt::Abs(right.k)).ToString().size();

        static constexpr int NUM_LINES = 2;
        for (int i = 0; i < NUM_LINES; i++) {
          printf(ANSI_PREVLINE
                 ANSI_CLEARLINE);
        }
        printf("%d its, dig: ks: %d max-b: %d\n"
               iters, k_digits,
               max_b_digits);
        double ips = iters / timer.Seconds();
        printf("Took %s (%.2fit/s)\n",
               ANSI::Time(timer.Seconds()).c_str(),
               ips);
      });
    }

  BigInt ak1 = BigInt::Abs(left.k);
  BigInt ak2 = BigInt::Abs(right.k);

  BigInt total_k = ak1 + ak2;

  // New best?
  if (total_k < best_total_k) {
    printf("\n\n\n" AGREEN("New best k1/k2") ": %s\n\n\n",
           total_k.ToString().c_str());
    FILE *f = fopen("comet-bestk.txt", "ab");
    fprintf(f,
            "** after %d iters: %s\n"
            "a = %s\n"
            "b1 = %s\n"
            "b2 = %s\n"
            "k1 = %s\n"
            "k2 = %s\n",
            iters, total_k.ToString().c_str(),
            a.ToString().c_str(),
            left.b.ToString().c_str(),
            right.b.ToString().c_str(),
            left.k.ToString().c_str(),
            right.k.ToString().c_str());
    fclose(f);
    best_total_k = total_k;
  }
  // Or maybe even done?
  if (ak1 == 1 && ak2 == 1) {
    printf(AGREEN("DONE!") " in " AWHITE("%d") " iters. Took %s.\n",
           iters, ANSI::Time(timer.Seconds()).c_str());
    return make_tuple(a, left, right);
  }

  // Prep each side.
  auto GetBaseM = [](const BigInt &n, BK &bk) -> BigInt {
      if (CHECK_INVARIANTS) {
        BigInt err = Error(n, a, bk.b);
        CHECK(err == bk.k) <<
          StringPrintf("GetBaseM("
                       TERM_N ", (" TERM_A ", " TERM_B ", " TERM_K "))\n"
                       "But got err: " ARED("%s") "\n",
                       n.ToString().c_str(),
                       a.ToString().c_str(),
                       bk.b.ToString().c_str(),
                       bk.k.ToString().c_str(),
                       err.ToString().c_str());
      }

      // Hmm, we're in trouble if a and b1 share a factor, but not b2.
      // Probably we should prohibit entering such states?
      BigInt gcd = BigInt::GCD(a, b);
      CHECK(gcd != 1) << "This is prohibited because we can't necessarily "
        "remove the factor from both b1 and b2.";

      // Now we need m such that am+b is divisible by k.
      // That's the same as saying that am mod k  =  -b mod k.
      // aka. am = -b (mod k).
      BigInt negbmodk = -bk.b % bk.k;

      // Now we can find the multiplicative inverse of a mod k,
      // using the extended euclidean algorithm.
      const auto [g, s, t] = BigInt::ExtendedGCD(a, bk.k);
      // now we have a*s + k*t = g.
      CHECK(g == 1) << "?? Don't know why this must be true, "
        "but it's seemingly assumed by descriptions of this?";

      // so if a*s + k*t = 1, then a*s mod k is 1 (because k*t is 0 mod k).
      // In other words, s is the multiplicative inverse of a (mod k).
      // so if am = -b (mod k), then a * (a^-1) * m = -b * (a^-1)  (mod k)
      // and thus m = -b * (a^-1)  (mod k), which is -b * s.
      BigInt base_m = (negbmodk * s) % bk.k;

      if (CHECK_INVARIANTS) {
        BigInt r = ((a * base_m + bk.b) % bk.k);
        CHECK(r == 0) <<
          StringPrintf("Expect k | (am + b). "
                       "But got remainder " ARED("%s") ".\n"
                       TERM_K " | (" TERM_A " * " TERM_M " + " TERM_B ")\n",
                       r.ToString().c_str(),
                       bk.k.ToString().c_str(),
                       a.ToString().c_str(),
                       base_m.ToString().c_str(),
                       bk.b.ToString().c_str());
      }

      if (VERBOSE) {
        printf("We have k | (am + b):\n"
               TERM_K " | (" TERM_A " * " TERM_M " + " TERM_B ")\n",
               bk.k.ToString().c_str(),
               a.ToString().c_str(),
               base_m.ToString().c_str(),
               bk.b.ToString().c_str());
      }

      return base_m;
    };

    // "Base" values for m.
    BigInt m1 = GetBaseM(nleft, left);
    BigInt m2 = GetBaseM(nright, right);

    // Now we have many choices (infinite, even) of mleft and mright
    // that yield new states.
    // Specifically mleft + x * left.k    and   mright + y * right.k
    // are divisible by the corresponding k, so they will work.

    // Now we want to choose x,y that minimize our metric. But for
    // the dual problem, we also need to choose x,y such that the
    // resulting a1 = a2.

    // The computation of a1, a2:
    // BigInt ml = mleft + x * left.k;
    // BigInt mr = mright + y * right.k;

    // BigInt new_a1 = (a * ml + left.b) / left.k;
    // BigInt new_a2 = (a * mr + right.b) / right.k;
    // so...
    // BigInt new_a1 = (a * (m1 + x * left.k) + left.b) / left.k;
    // BigInt new_a2 = (a * (m2 + y * right.k) + right.b) / right.k;

    // solving for x,y.
    // (a * (m1 + x * k1) + b1) / k1 = (a * (m2 + y * k2) + b2) / k2
    // (a * (m1 + x * k1) + b1) = k1 * (a * (m2 + y * k2) + b2) / k2
    // a * (m1 + x * k1) = (k1 * (a * (m2 + y * k2) + b2) / k2) - b1
    // (m1 + x * k1) = ((k1 * (a * (m2 + y * k2) + b2) / k2) - b1) / a
    // x * k1 = (((k1 * (a * (m2 + y * k2) + b2) / k2) - b1) / a - m1)
    // x = (((k1 * (a * (m2 + y * k2) + b2) / k2) - b1) / a - m1) / k1
    // distribute k1?
    // x = ((k1 * (a * (m2 + y * k2) + b2) / k2) - b1) / ak1 - m1/k1
    // x = (k1 * (a * (m2 + y * k2) + b2) / k2)/ak1 - b1/ak1 - m1/k1
    // x = (k1 * a * (m2 + y * k2))/ak1k2 + (k1 * b2) / ak1k2 - b1/ak1 - m1/k1
    // x = (m2 + y * k2)/k2 + b2/ak2 - b1/ak1 - m1/k1
    // x = m2/k2 + y + b2/ak2 - b1/ak1 - m1/k1
    // x = y + m2/k2 - m1/k1 + b2/ak2 - b1/ak1

    // ok so that's reasonably clean.

    // I don't love the division, so multiply by ak1k2 to get
    // x * ak1k2 = y * ak1k2 + m2ak1 - m1ak2 + b2k1 - b1k2
    // where everything's constant but x,y. So it becomes
    // a linear diophantine equation of the form
    // x * c = y * c + d
    // ... which is analytical. Sweet.

    const BigInt &b1 = left.b;
    const BigInt &b2 = right.b;
    const BigInt &k1 = left.k;
    const BigInt &k2 = right.k;
    BigInt ak1 = a * k1;
    BigInt ak1k2 = ak1 * k2;
    BigInt m2ak1 = m2 * ak1;
    BigInt m1ak2 = m1 * a * k2;
    BigInt b2k1 = b2 * k1;
    BigInt b1k2 = b1 * k2;

    const BitInt &c = ak1k2;
    BigInt d = m2ak1 - m1ak2 + b2k1 - b1k2;

    // HERE :) solve the linear diophantine equation.
    // ALSO we need to require that gcd(a1, b1) = 1, gcd(a2, b2) = 1

    // TODO: Take abs(a) when we're done; it gets squared.


    auto LeftTriple = [&nleft, &mleft, &left](const BigInt &x) {
        BigInt m = mleft + x * left.k;
        if (CHECK_INVARIANTS) {
          CHECK((left.a * m + left.b) % left.k == 0);
          CHECK((left.b * m + nleft * left.a) % left.k == 0);
          CHECK((m * m - nleft) % left.k == 0);
        }

        // PERF GMP has a faster version when we know the remainder is zero
        BigInt new_a = BigInt::Abs((left.a * m + left.b) / left.k);
        BigInt new_b = BigInt::Abs((left.b * m + nleft * left.a) / left.k);
        BigInt new_k = (m * m - nleft) / left.k;
        return Triple(std::move(new_a), std::move(new_b), std::move(new_k));
      };

    auto RightTriple = [&nright, &mright, &right](const BigInt &y) {
        BigInt m = mright + y * right.k;
        if (CHECK_INVARIANTS) {
          CHECK((right.a * m + right.b) % right.k == 0);
          CHECK((right.b * m + nright * right.a) % right.k == 0);
          CHECK((m * m - nright) % right.k == 0);
        }

        // PERF GMP has a faster version when we know the remainder is zero
        BigInt new_a = BigInt::Abs((right.a * m + right.b) / right.k);
        BigInt new_b = BigInt::Abs((right.b * m + nright * right.a) / right.k);
        BigInt new_k = (m * m - nright) / right.k;
        return Triple(std::move(new_a), std::move(new_b), std::move(new_k));
      };

    // So the question is, what values to try?
    // The metric is |left.k| + |right.k| + |left.a - right.a|.
    // the k's are (mleft^2 - nleft) and (mright^2 - nright).
    // the a's of the form (aleft * mleft + bleft) - (aright * mright + bright)
    // for original aleft, bleft.
    // Since this is algebraic we should be able to differentiate it and
    // search for a solution with SGD.

    // For now, let's get off the ground with a black-box optimizer.
    // This may actually be a poor choice because we may need a larger
    // range than double can represent for x and y. (Could use log
    // space?)
    last_repeats = 0;
    last_attempts = 0;

    using CompassBestPairFinder = KeyedBestPairFinder<std::pair<int, int>>;
    auto RunCompass = [&](CompassBestPairFinder *finder,
                          const std::initializer_list<int> &dxs,
                          const std::initializer_list<int> &dys) {
      // This one walks along compass directions, but greedily
      // keeps going if doubling the distance reduces the metric.
      for (int dx : dxs) {
        for (int dy : dys) {
          if (dx != 0 || dy != 0) {

            // walk until we find something new
            for (int x = 0, y = 0; ; x += dx, y += dy) {
              // only compute the center point once.
              if (x == 0 && y == 0 &&
                  dx != *dxs.begin() && dy != *dys.begin())
                continue;

              // This method is probably terrible, but in case we
              // wanted to optimize it, we can probably "increment"
              // triples in place.
              BigInt xx{x}, yy{y};
              Triple new_left = LeftTriple(xx);
              Triple new_right = RightTriple(yy);

              last_attempts++;
              if (!seen.contains(std::make_pair(new_left, new_right))) {
                BigInt prev_metric = Metric(new_left, new_right);
                finder->ObserveWithMetric(new_left, new_right, prev_metric);
                // Now greedily continue.
                for (;;) {
                  // PERF shift in place
                  xx = BigInt::LeftShift(xx, 1);
                  yy = BigInt::LeftShift(yy, 1);
                  Triple more_left = LeftTriple(xx);
                  Triple more_right = RightTriple(yy);

                  last_attempts++;
                  BigInt this_metric = Metric(more_left, more_right);
                  if (!seen.contains(std::make_pair(more_left, more_right))) {
                    // Only observe it if it hasn't been seen before,
                    // but keep doubling while we're headed in the
                    // right direction either way.
                    finder->ObserveWithMetric(
                        more_left, more_right, this_metric,
                        make_pair(dx, dy));
                  } else {
                    last_repeats++;
                  }

                  if (this_metric < prev_metric) {
                    prev_metric = this_metric;
                  } else {
                    break;
                  }
                }

                break;
              }
              last_repeats++;
            }
          }
        }
      }
    };

    // Generate new triples using the current method.
    Triple new_left, new_right;
    if (method == GREEDY_9) {
      BestPairFinder finder;

      for (int dx : {-1, 0, 1}) {
        for (int dy : {-1, 0, 1}) {
          if (dx != 0 || dy != 0) {

            // walk until we find something new
            for (int x = 0, y = 0; ; x += dx, y += dy) {
              // This method is probably terrible, but in case we
              // wanted to optimize it, we can probably "increment"
              // triples in place.
              Triple new_left = LeftTriple(BigInt{x});
              Triple new_right = RightTriple(BigInt{y});

              last_attempts++;
              if (!seen.contains(std::make_pair(new_left, new_right))) {
                finder.Observe(new_left, new_right);
                break;
              }
              last_repeats++;
            }
          }
        }
      }

      std::tie(new_left, new_right) = finder.Best();

    } else if (method == GRADIENT_DESCENT) {

      // Looking for x,y, integers.

      #if 0
      if (grad_image_per.ShouldRun()) {
        ImageRGBA img(128, 128);
        for (int sy = 0; sy < 128; sy++) {
          BigInt y{-(sy - 64)};
          for (int sx = 0; sx < 128; sx++) {
            BigInt x{sx - 64};
            const auto [dx, dy] =
              MetricDerivatives(nleft, nright, m1, m2, left, right, x, y);

          }
        }
      }
      #endif

      // PERF: Since we actually evaluate the derivative at (0, 0),
      // We could just bake these values into the function (and many
      // terms drop out).
      BigInt x{0}, y{0};
      BigInt dx, dy;
      std::tie(dx, dy) =
        MetricDerivatives(nleft, nright, mleft, mright, left, right, x, y);

      BestPairFinder finder;
      while (dx != 0 || dy != 0) {
        if (VERY_VERBOSE) {
          printf("dx " AGREEN("%s") " dy " AYELLOW("%s") "\n",
                 dx.ToString().c_str(), dy.ToString().c_str());
        }
        Triple tl = LeftTriple(-dx);
        Triple tr = RightTriple(-dy);

        if (!seen.contains(std::make_pair(tl, tr))) {
          finder.Observe(tl, tr);
        }

        dx = dx / BigInt{2};
        dy = dy / BigInt{2};
      }

      std::tie(new_left, new_right) = finder.Best();

    } else if (method == COMPASS_EXPONENTIAL) {

      CompassBestPairFinder finder;

      RunCompass(&finder, {-1, 0, 1}, {-1, 0, 1});

      std::tie(new_left, new_right) = finder.Best();

    } else if (method == COMPASS25_EXPONENTIAL) {

      CompassBestPairFinder finder;
      if (iters & 1) {
        RunCompass(&finder, {-3, -1, 0, 1, 2}, {-2, -1, 0, 1, 3});
      } else {
        RunCompass(&finder, {-2, -1, 0, 1, 3}, {-3, -1, 0, 1, 2});
      }
      std::tie(new_left, new_right) = finder.Best();

    } else if (method == RESTARTING_COMPASS) {

      static constexpr int RESTART_INTERVAL = 10000;

      // If we haven't improved the score in some time, choose a
      // new random point.
      if (recent_best_score.has_value() &&
          (iters - recent_best_iter) > RESTART_INTERVAL) {

        // XXX random bignums

        auto rr = [&rc]() -> uint64_t {
            return Rand64(&rc);
          };

        bool same_a = rc.Byte() < 200;

        BigInt a = BigInt::RandTo(rr, nleft * nright * restart_multiplier);
        BigInt al = a;
        BigInt ar = same_a ? a :
          BigInt::RandTo(rr, nleft * nright * restart_multiplier);

        BigInt bl, br;
        bool is_random = !!(rc.Byte() & 1);

        if (is_random) {
          // Start at random point
          // bl = BigInt::RandTo(rr, nleft * nleft * restart_multiplier);
          // br = BigInt::RandTo(rr, nright * nright * restart_multiplier);

          bl = BigInt(RandTo(&rc, (nleft * nleft).ToInt().value()));
          br = BigInt(RandTo(&rc, (nright * nright).ToInt().value()));

        } else {
          // Start with approximately the right value
          // Error = b * b - n * a * a;
          // so for 0 error,  n * a * a = b * b
          // (These nonetheless have huge errors!)
          bl = BigInt::Sqrt(BigInt::Sqrt(nleft * al * al));
          br = BigInt::Sqrt(BigInt::Sqrt(nright * ar * ar));
        }

        restart_multiplier = restart_multiplier * BigInt{RESTART_MULTIPLIER};

        BigInt kl = Error(nleft, al, bl);
        BigInt kr = Error(nright, ar, br);

        new_left = Triple(al, bl, kl);
        new_right = Triple(ar, br, kr);

        printf(
            "Num valid: %d Best: %s\n"
            "Ended on\n"
            "(" TERM_A "," TERM_B "," TERM_K ")\nand\n"
            "(" TERM_A "," TERM_B "," TERM_K ")\n"
            "Which has score " AWHITE("%s") ".\n"
            "Restart (%s) with\n"
            "(" TERM_A "," TERM_B "," TERM_K ")\nand\n"
            "(" TERM_A "," TERM_B "," TERM_K ")\n"
            "\n\n\n\n"
            ,
            valid_since_reset,
            best_valid_since_reset.has_value() ?
            LongNum(best_valid_since_reset.value()).c_str() : "?",
            LongNum(left.a).c_str(),
            LongNum(left.b).c_str(),
            LongNum(left.k).c_str(),
            LongNum(right.a).c_str(),
            LongNum(right.b).c_str(),
            LongNum(right.k).c_str(),
            LongNum(Metric(left, right)).c_str(),
            is_random ? ABLUE("random") : ACYAN("sqrt"),
            LongNum(a).c_str(), LongNum(bl).c_str(), LongNum(kl).c_str(),
            LongNum(a).c_str(), LongNum(br).c_str(), LongNum(kr).c_str());
        restarts++;
        last_restart = iters;

        if (CLEAR_HISTORY) {
          seen.clear();
        }

        // reset the counter
        recent_best_score = nullopt;
        valid_since_reset = 0;
        best_valid_since_reset = nullopt;
      } else {

        CompassBestPairFinder finder;

        auto Consider = [&](const BigInt &x, const BigInt &y) {
            Triple new_left = LeftTriple(x);
            Triple new_right = RightTriple(y);

            last_attempts++;
            if (!seen.contains(std::make_pair(new_left, new_right))) {
              finder.Observe(new_left, new_right);
            } else {
              last_repeats++;
            }
          };
        auto TryPoint = [&](const BigInt &sqrtn1,
                            const BigInt &sqrtn2) {

            BigInt diff1 = sqrtnleft - mleft;
            BigInt x1 = (diff1 / left.k) * left.k;
            BigInt diff2 = sqrtnright - mright;
            BigInt x2 = (diff2 / right.k) * right.k;

            const BigInt &k1 = left.k;
            const BigInt &k2 = right.k;
            Consider(x1, x2);
            Consider(x1 + k1, x2);
            Consider(x1 - k1, x2);
            Consider(x1, x2 - k2);
            Consider(x1 + k1, x2 - k2);
            Consider(x1 - k1, x2 - k2);
            Consider(x1, x2 + k2);
            Consider(x1 + k1, x2 + k2);
            Consider(x1 - k1, x2 + k2);
          };
        TryPoint(sqrtnleft, sqrtnright);

        if (iters & 1) {
          RunCompass(&finder, {-3, -1, 0, 1, 2}, {-2, -1, 0, 1, 3});
        } else {
          RunCompass(&finder, {-2, -1, 0, 1, 3}, {-3, -1, 0, 1, 2});
        }

        std::tie(new_left, new_right) = finder.Best();
        using CompassKey = std::pair<int, int>;
        const std::optional<std::pair<CompassKey, BigInt>> &best_key_score =
          finder.BestScore();

        if (best_key_score.has_value()) {
          if (!recent_best_score.has_value() ||
              best_key_score.value().second < recent_best_score.value()) {
            recent_best_iter = iters;
            recent_best_score = {best_key_score.value().second};
            if (last_compass_dir == best_key_score.value().first) {
              last_compass_count++;
            } else {
              last_compass_count = 1;
              last_compass_dir = best_key_score.value().first;
            }
          }
        }
      }

    } else if (method == RANDOM_WALK) {

      static constexpr int SAMPLES = 100;
      static constexpr int MAGNITUDE = 10;
      BestPairFinder finder;
      int samples = 0;
      while (samples < SAMPLES) {
        RandomGaussian gauss(&rc);
        int64_t x = (int)std::round(gauss.Next() * MAGNITUDE);
        int64_t y = (int)std::round(gauss.Next() * MAGNITUDE);
        Triple new_left = LeftTriple(BigInt{x});
        Triple new_right = RightTriple(BigInt{y});
        last_attempts++;
        if (seen.contains(std::make_pair(new_left, new_right))) {
          last_repeats++;
        } else {
          finder.Observe(new_left, new_right);
          samples++;
        }
      }

      std::tie(new_left, new_right) = finder.Best();

    } else if (method == BLACK_BOX) {

      const auto [xy, score] =
        Opt::Minimize2D(
            [&seen, &last_repeats, &last_attempts,
             &LeftTriple, &RightTriple](double xf, double yf) -> double {
              int64_t xi = xf;
              int64_t yi = yf;

              BigInt x{xi};
              BigInt y{yi};

              if (VERY_VERBOSE)
                printf("Try %.4f,%.4f\n", xf, yf);
              Triple new_left = LeftTriple(x);
              Triple new_right = RightTriple(y);

              // infeasible if a = 0 for either tuple.
              if (new_left.a == 0 ||
                  new_right.a == 0)
                return 1e200;

              // infeasible if already attempted.
              last_attempts++;
              if (seen.contains(std::make_pair(new_left, new_right))) {
                last_repeats++;
                return 1e200;
              }

              // should also skip degenerate solutions?
              BigInt metric = Metric(new_left, new_right);
              // Metric will be nonnegative, but avoid -oo.
              double score = BigInt::NaturalLog(metric + BigInt{1});
              if (VERY_VERBOSE)
                printf("Score: %.4f\n", score);
              return score;
            },
            std::make_tuple(-10000.0, -10000.0),
            std::make_tuple(+10000.0, +10000.0),
            // std::make_tuple(-1e53, 1e53),
            // std::make_tuple(-1e53, 1e53),
            1000);

      if (VERBOSE) {
        printf("Opt: Got %.3f, %.3f with score %.3f\n",
               std::get<0>(xy),
               std::get<1>(xy),
               score);
      }
      // PERF with Optimizer wrapper, we don't have to recompute the triple.
      // But there may be other costs with that.
      new_left = LeftTriple(BigInt((int64_t)std::get<0>(xy)));
      new_right = RightTriple(BigInt((int64_t)std::get<1>(xy)));
    }



    if (VERBOSE) {
      printf("Metric: " AWHITE("%s") "\n",
             Metric(new_left, new_right).ToString().c_str());
      printf("Left:  (" TERM_A "," TERM_B "," TERM_K ")\n"
             "Right: (" TERM_A "," TERM_B "," TERM_K ")\n",
             new_left.a.ToString().c_str(),
             new_left.b.ToString().c_str(),
             new_left.k.ToString().c_str(),
             new_right.a.ToString().c_str(),
             new_right.b.ToString().c_str(),
             new_right.k.ToString().c_str());
    }

    auto CheckTriple = [](const BigInt &n, const Triple &triple) {
        BigInt err = Error(n, triple.a, triple.b);
        CHECK(err == triple.k) <<
          StringPrintf("Invalid triple for n=" TERM_N ": ("
                       TERM_A "," TERM_B "," TERM_K ") but actual "
                       "error is " ARED("%s") "\n",
                       n.ToString().c_str(),
                       triple.a.ToString().c_str(),
                       triple.b.ToString().c_str(),
                       triple.k.ToString().c_str(),
                       err.ToString().c_str());
      };

    if (CHECK_INVARIANTS) {
      CheckTriple(nleft, new_left);
      CheckTriple(nright, new_right);
    }

    left = std::move(new_left);
    right = std::move(new_right);

    if (MAX_ITERS > 0) { CHECK(iters < MAX_ITERS); }
  }
}

static void DoPair(const BigInt &nleft,
                   const BigInt &nright,
                   const Triple &start_left,
                   const Triple &start_right) {
  Timer dual_timer;
  const auto [left, right] =
    DualBhaskara(nleft, nright, start_left, start_right);
  printf("Finished in %s\n", ANSI::Time(dual_timer.Seconds()).c_str());

  CHECK(left.a == right.a);
  // We allow +/- 1 for this version.
  CHECK(BigInt::Abs(left.k) == 1);
  printf("Derived solution for n = " TERM_N "\n"
         "a: " TERM_A "\n"
         "b: " TERM_B "\n",
         nleft.ToString().c_str(),
         left.a.ToString().c_str(),
         left.b.ToString().c_str());
  printf("And also simultaneously n = " TERM_N "\n"
         "a: " TERM_A "\n"
         "b: " TERM_B "\n",
         nright.ToString().c_str(),
         right.a.ToString().c_str(),
         right.b.ToString().c_str());

  CHECK(left.a != 0);
  CHECK(right.a != 0);
  CHECK(Error(nleft, left.a, left.b) == left.k);
  CHECK(Error(nright, right.a, right.b) == right.k);
  printf(AGREEN("OK") "\n");
}


static void RealProblem() {
  // two equations
  // 222121 x^2 + 1 = y_a^2
  // 360721 x^2 + 1 = y_h^2
  // (y_a is called a in the square; y_h is called h. but we are going
  // to use a,b,c for other coefficients here.)

  const BigInt n_left = 222121_b;
  const BigInt n_right = 360721_b;

  const BigInt left_a =
    96853990143729182446466254903787102920420186066745135266595909679523238492820905098594426380259780074224586852201498992449275127756378129855820172958584294747666448702649536007880590904630402994485054414777494200716316411350928848627022864018323664548200672490871343394924009459667481509707703598930232145417695483619768820532418380893489621163234820873999155059853985917579838562873820424199495555730452754055968329456327571681829182817764194271946670769735346243288252413916542521030793275016960_b;
  const BigInt left_b =
    45647009151151305718708252078329478619159688396560344940484846739545404737863346364814575382380658468268696696162077346578703916050375366743990587977745934350267627720790556553334680620537062793215771183142869525305229082597324857232212169256203590974623367552076861467765584622698250583658967988921977292909237331892140561912612147974391374706186555174904116425103638903793323393015436354152507544138006929555916470598773073491241252241089212693998884441309720519067489536318638130578697131025203199_b;

  BigInt left_err = Error(n_left, left_a, left_b);
  CHECK(left_err == 1_b);

  const BigInt right_a =
    5254301467178253668063501573805488115701470062129148344048204657019748885380657942526435850589822056436907162208858662833748764291164063392849591565232182340896344102417561460498588341287763598974028219631368919894791141180699352614842162329575092178804714936187264001803984435149307802237910897195756701561689733829416441654000294332579338938600777476665373690729282455769460482322102437685615931720435571771362615173743048328285921760676802224631583102137529433447859249096920912083501391265263092551585295016322744236018859511648734714069848447775860243716078071973487185423849887186837594810420676451477817334917442425724234656945231269179119186700637627642532511974556138751986944397915640033290288664241890733655443663188699611507335767790934980909769231545567661414370796993132434555657909168192875390617801233376652983464964576564440125236085759750607806749224929539111304657072598148941302658707149037639442679980053501547520718161071834317467291327489266555022779213264638058690142715120_b;
  const BigInt right_b =
    3155736260680638626215586515224779475000982538318510234796596948969542279640527333764830931669758997548264003306978761631985610163542280769957617791837115815855505466192424155483747195334026528686100467671456903838445918904622247229490490143150205748045220079365084818251874000588893203069462517365771614948335859034465574562253629688393366532513342848079469578491642318761744360133449340864599791431253295837903633977372300833275627599528361343594279999123827422243803042839623732782710017503150424671269804820044584384922764477211533947638524985602275604505195848101327992205477251957539278637282982381170957580513664760124760363535719750512488336024599849383281905125902008900085015792063893406749523426160808130369628761209934170648229913920676290078798350463180533834821461892201220681643823782682294011883387286116223752801467866509139817280697848323355423553433987427070952556267278422608907045802820393525018738409778215962756148887404031077236716404130655643413294151356469733756144336771201_b;

  BigInt right_err = Error(n_right, right_a, right_b);
  CHECK(right_err == 1_b) << right_err.ToString();

  // Triple start_left(left_a, left_b, left_err);
  // Triple start_right(right_a, right_b, right_err);
  // DoPair(n_left, n_right, start_left, start_right);


  ArcFour rc("hi");
  BigInt a(RandTo(&rc, (n_left * n_right).ToInt().value()));
  BigInt bl(RandTo(&rc, (n_left * n_left).ToInt().value()));
  BigInt br(RandTo(&rc, (n_right * n_right).ToInt().value()));

  BigInt kl = Error(n_left, a, bl);
  BigInt kr = Error(n_right, a, br);

  printf(
      "Start with:\n"
      "(" TERM_A "," TERM_B "," TERM_K ")\nand\n"
      "(" TERM_A "," TERM_B "," TERM_K ")\n"
      "\n\n\n\n" ,
      LongNum(a).c_str(),
      LongNum(bl).c_str(),
      LongNum(kl).c_str(),
      LongNum(a).c_str(),
      LongNum(br).c_str(),
      LongNum(kr).c_str());

  DoPair(n_left, n_right, Triple(a, bl, kl), Triple(a, br, kr));
}

// unknown whether this is solvable
static void SmallProblem() {
  BigInt nleft{12345678};
  BigInt nright{10001};
  Triple start_left(1_b, 8_b, Error(nleft, 1_b, 8_b));
  Triple start_right(1_b, 7_b, Error(nright, 1_b, 7_b));
  DoPair(nleft, nright, start_left, start_right);
}

// 7x^2 + 1 = y^2
// 11x^2 + 1 = z^2
// this is solvable with x = 3, y = 8, z = 10
static void ToyProblem() {
  BigInt nleft{7};
  BigInt nright{11};
  // (This is a solution)
  // BigInt left_a = 3_b, left_b = 8_b;
  // BigInt right_a = 3_b, right_b = 10_b;

  BigInt left_a = 1_b, left_b = 8_b;
  BigInt right_a = 1_b, right_b = 7_b;
  Triple start_left(left_a, left_b, Error(nleft, left_a, left_b));
  Triple start_right(right_a, right_b, Error(nright, right_a, right_b));

  DoPair(nleft, nright, start_left, start_right);
}

// This could easily be found with exhaustive search. 3108 and 5513 are
// relatively prime.
// 3108x^2 + 1 = y^2
// 5513x^2 + 1 = z^2
// solvable with x = 4,  y = 223,  z = 297
static void ToyProblem2() {
  BigInt nleft{3108};
  BigInt nright{5513};
  // (This is a solution)
  // BigInt left_a = 4_b, left_b = 223_b;
  // BigInt right_a = 4_b, right_b = 297_b;

  BigInt left_a = 1_b, left_b = 8_b;
  BigInt right_a = 1_b, right_b = 7_b;
  Triple start_left(left_a, left_b, Error(nleft, left_a, left_b));
  Triple start_right(right_a, right_b, Error(nright, right_a, right_b));

  DoPair(nleft, nright, start_left, start_right);
}

// Solvable, but now we get into territory where brute-force search
// would not be easy, at least. Solution is quite far off the diagonal
// (about 10:1).
static void NontrivialProblem() {
  BigInt nleft = 10000000040000000_b;
  BigInt nright = 100188184450263_b;
  // Solution
  // BigInt left_a  = 5_b, left_b  = 500000001_b ;
  // BigInt right_a = 5_b, right_b = 50047024_b;

  // BigInt left_a = 31337_b, left_b = 8_b;
  // BigInt right_a = 31337_b, right_b = 7_b;

  // with n1*a^2 - b1^2 = 0,
  // n1*a^2 = b1^2
  // sqrt(n1)*a = sqrt(b)
  BigInt a1 = 31337_b;
  BigInt a2 = 31337_b;
  BigInt b1 = BigInt::Sqrt(nleft); //  * a;
  BigInt b2 = BigInt::Sqrt(nright); //  * a;

  Triple start_left(a1, b1, Error(nleft, a1, b1));
  Triple start_right(a2, b2, Error(nright, a2, b2));

  DoPair(nleft, nright, start_left, start_right);
}

int main(int argc, char **argv) {
  ANSI::Init();
  printf("Start.\n");

  /*
  BigInt nleft{91};
  BigInt nright{90};
  Triple start_left(1_b, 8_b, Error(nleft, 1_b, 8_b));
  Triple start_right(1_b, 7_b, Error(nright, 1_b, 7_b));
  DoPair(nleft, nright, start_left, start_right);
  */

  // ToyProblem2();
  // NontrivialProblem();

  RealProblem();

  #if 0
  for (int ni = 2; ni < 150; ni++) {
    uint64_t s = Sqrt64(ni);
    // This algorithm doesn't work for perfect squares.
    if (s * s == ni)
      continue;

    BigInt n{ni};
    // Solve the same triple twice.
    Triple start_left(1_b, 8_b, Error(n, 1_b, 8_b));
    Triple start_right(1_b, 8_b, Error(n, 1_b, 8_b));

    DoPair(n, n, start_left, start_right);
  }
  #endif

  printf(AGREEN("OK") " :)\n");
  return 0;
}
