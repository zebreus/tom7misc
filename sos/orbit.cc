
// Use a chakravala-like method to try to solve two pell-like
// equations at once. No idea if this will work!

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

#include "sos-util.h"

using namespace std;

static constexpr bool CHECK_INVARIANTS = true;
static constexpr bool VERBOSE = false;
static constexpr bool VERY_VERBOSE = VERBOSE && false;

#define TERM_A AFGCOLOR(39, 179, 214, "%s")
#define TERM_M AFGCOLOR(39, 214, 179, "%s")
#define TERM_B AFGCOLOR(232, 237, 173, "%s")
#define TERM_K AFGCOLOR(220, 173, 237, "%s")
#define TERM_N AFGCOLOR(227, 198, 143, "%s")
#define TERM_SQRTN AFGCOLOR(210, 200, 180, "%s")
#define TERM_E AFGCOLOR(200, 120, 140, "%s")


struct Triple {
  Triple() : Triple(0, 0, 0) {}
  Triple(BigInt aa, BigInt bb, BigInt kk) : a(std::move(aa)),
                                            b(std::move(bb)),
                                            k(std::move(kk)) {}
  Triple(int64_t aa, int64_t bb, int64_t kk) : a(aa), b(bb), k(kk) {}
  inline void Swap(Triple *other) {
    a.Swap(&other->a);
    b.Swap(&other->b);
    k.Swap(&other->k);
  }

  Triple(const Triple &other) : a(other.a), b(other.b), k(other.k) {}

  Triple &operator =(const Triple &other) {
    // Self-assignment does nothing.
    if (this == &other) return *this;
    a = other.a;
    b = other.b;
    k = other.k;
    return *this;
  }
  Triple &operator =(Triple &&other) {
    // other must remain valid.
    Swap(&other);
    return *this;
  }

  BigInt a, b, k;
};

struct HashTriple {
  size_t operator()(const Triple &tri) const {
    return (size_t)(BigInt::LowWord(tri.k) * 0x314159 +
                    BigInt::LowWord(tri.a) * 0x7FFFFFFF +
                    BigInt::LowWord(tri.b));
  }
};

namespace std {
template <> struct hash<Triple> {
  size_t operator()(const Triple &tri) const {
    return HashTriple()(tri);
  }
};
}

static inline bool operator ==(const Triple &x, const Triple &y) {
  return x.k == y.k &&
    x.a == y.a &&
    x.b == y.b;
}

// For sol = (x, y), compute k such that n * x^2 + k = y^2.
static BigInt Error(const BigInt &n, const BigInt &x, const BigInt &y) {
  return y * y - n * x * x;
}

#if 0

static Sol CombineSelf(const BigInt &n, const Sol &sol) {
  const auto &[x, y] = sol;
  return make_pair(
      2_b * x * y,
      y * y + n * x * x);
  // and also the degenerate (0, y^2 - nx^2)
}

static std::pair<Sol, Sol> Combine(
    const BigInt &n, const Sol &sol1, const Sol &sol2) {
  const auto &[a, b] = sol1;
  const auto &[c, d] = sol2;

  return
    make_pair(
        make_pair(b * c + a * d, b * d + n * a * c),
        make_pair(b * c - a * d, b * d - n * a * c));
}

struct HashSol {
  size_t operator ()(const Sol &sol) const {
    return (size_t)BigInt::LowWord(sol.first - sol.second);
  }
};

struct EqSol {
  bool operator ()(const Sol &a, const Sol &b) const {
    return a.first == b.first && a.second == b.second;
  }
};

static std::string SolString(const Sol &a) {
  return StringPrintf("(" TERM_A "," TERM_B ")",
                      a.first.ToString().c_str(),
                      a.second.ToString().c_str());
}
#endif

using TripleSet = std::unordered_set<Triple, HashTriple>;

using TriplePairHash = Hashing<std::pair<Triple, Triple>>;

using TriplePairSet = std::unordered_set<std::pair<Triple, Triple>,
                                         TriplePairHash>;

// This is the value we try to minimize on each step.
static BigInt Metric(const Triple &left, const Triple &right) {
  // k is a multiple of the quantity m^2-n which is minimized
  // in the original Chakravala algorithm.

  BigInt res =
    // Try to get a k of +/- 1.
    BigInt::Abs(left.k) +
    BigInt::Abs(right.k) +
    // But also try to minimize the difference between the first
    // components of the triple. bs are unconstrained.
    BigInt::Abs(left.a - right.a);

  if (VERY_VERBOSE) {
    printf("Metric = " ACYAN("%s") "\n"
           "Left:  (" TERM_A "," TERM_B "," TERM_K ")\n"
           "Right: (" TERM_A "," TERM_B "," TERM_K ")\n",
           res.ToString().c_str(),
           left.a.ToString().c_str(),
           left.b.ToString().c_str(),
           left.k.ToString().c_str(),
           right.a.ToString().c_str(),
           right.b.ToString().c_str(),
           right.k.ToString().c_str());
  }

  return res;
}

// Given two equations of the form
//     na^2 + k = b^2
// where (a, b, k) is the "triple",
// Try to find two triples t1, t2 where t1.a=t2.a and t1.k = t2.k = 1.
static std::pair<Triple, Triple>
DualBhaskara(BigInt nleft, BigInt nright, Triple left, Triple right) {

  if (VERBOSE)
    printf(AWHITE(ABGCOLOR(0, 0, 50, "---------- on %s,%s ------------")) "\n",
           nleft.ToString().c_str(),
           nright.ToString().c_str());

  Periodically bar_per(1.0);
  bool first_progress = true;
  const BigInt start_metric = Metric(left, right);
  const int start_metric_size = start_metric.ToString().size();

  Timer timer;

  // Pair of triples (left, right) that we've already tried. We avoid
  // exploring them a second time, as this would lead to a cycle.
  TriplePairSet seen;

  for (int iters = 0; true; iters ++) {
    if (CHECK_INVARIANTS) {
      CHECK(!seen.contains(std::make_pair(left, right)));
    }
    seen.insert(std::make_pair(left, right));

    BigInt a_diff = left.a - right.a;

    if (VERBOSE) {
      printf(AWHITE(ABGCOLOR(80, 0, 0, "== %d iters ==")) "\n"
             "Left: (" TERM_A "," TERM_B "," TERM_K ")\n"
             "Right: (" TERM_A "," TERM_B "," TERM_K ")\n",
             iters,
             left.a.ToString().c_str(),
             left.b.ToString().c_str(),
             left.k.ToString().c_str(),
             right.a.ToString().c_str(),
             right.b.ToString().c_str(),
             right.k.ToString().c_str());
    } else {
      // Only output progress bar when verbose mode is off.
      bar_per.RunIf([&first_progress, &start_metric, start_metric_size,
                     &a_diff,
                     &timer, &iters, &left, &right]() {
        if (first_progress) {
          printf("\n");
          first_progress = false;
        }
        BigInt cur_metric = Metric(left, right);
        BigInt metric_diff = start_metric - cur_metric;
        if (metric_diff < BigInt{0}) metric_diff = BigInt{0};

        int digits_total = start_metric_size;
        int digits_done = metric_diff.ToString().size();

        int max_b_digits = std::max(left.b.ToString().size(),
                                    right.b.ToString().size());
        int a_diff_digits = BigInt::Abs(a_diff).ToString().size();
        int k_digits =
          (BigInt::Abs(left.k) + BigInt::Abs(right.k)).ToString().size();

        printf(ANSI_PREVLINE
               ANSI_CLEARLINE
               "%s\n",
               ANSI::ProgressBar(
                   digits_done, digits_total,
                   StringPrintf("%d its, digits: ks: %d diff: %d max-b: %d",
                                iters, k_digits,
                                a_diff_digits,
                                max_b_digits),
                   timer.Seconds()).c_str());
      });
    }

    // Are we done?
    if (a_diff == BigInt{0} &&
        BigInt::Abs(left.k) == BigInt{1} &&
        BigInt::Abs(right.k) == BigInt{1}) {
      printf(AGREEN("DONE!") " in " AWHITE("%d") " iters. Took %s.\n",
             iters, ANSI::Time(timer.Seconds()).c_str());
      return make_pair(left, right);
    }

    // Prep each triple.
    auto GetBaseM = [](const BigInt &n, Triple &triple) -> BigInt {
        if (CHECK_INVARIANTS) {
          BigInt err = Error(n, triple.a, triple.b);
          CHECK(err == triple.k) <<
            StringPrintf("GetBaseM("
                         TERM_N ", (" TERM_A ", " TERM_B ", " TERM_K "))\n"
                         "But got err: " ARED("%s") "\n",
                         n.ToString().c_str(),
                         triple.a.ToString().c_str(),
                         triple.b.ToString().c_str(),
                         triple.k.ToString().c_str(),
                         err.ToString().c_str());
        }

        BigInt gcd = BigInt::GCD(triple.a, triple.b);
        if (gcd != BigInt{1}) {
          if (CHECK_INVARIANTS) {
            CHECK(triple.a % gcd == BigInt{0});
            CHECK(triple.b % gcd == BigInt{0});
          }
          triple.a = triple.a / gcd;
          triple.b = triple.b / gcd;
          CHECK(false) << "XXX not k??";

          if (CHECK_INVARIANTS) {
            CHECK(Error(n, triple.a, triple.b) == triple.k);
          }
        }


        // Now we need m such that am+b is divisible by k.
        // That's the same as saying that am mod k  =  -b mod k.
        // aka. am = -b (mod k).
        BigInt negbmodk = -triple.b % triple.k;

        // Now we can find the multiplicative inverse of a mod k,
        // using the extended euclidean algorithm.
        const auto [g, s, t] = BigInt::ExtendedGCD(triple.a, triple.k);
        // now we have a*s + k*t = g.
        CHECK(g == BigInt{1}) << "?? Don't know why this must be true, "
          "but it's seemingly assumed by descriptions of this?";

        // so if a*s + k*t = 1, then a*s mod k is 1 (because k*t is 0 mod k).
        // In other words, s is the multiplicative inverse of a (mod k).
        // so if am = -b (mod k), then a * (a^-1) * m = -b * (a^-1)  (mod k)
        // and thus m = -b * (a^-1)  (mod k), which is -b * s.
        BigInt base_m = (negbmodk * s) % triple.k;

        if (CHECK_INVARIANTS) {
          BigInt r = ((triple.a * base_m + triple.b) % triple.k);
          CHECK(r == BigInt{0}) <<
            StringPrintf("Expect k | (am + b). "
                         "But got remainder " ARED("%s") ".\n"
                         TERM_K " | (" TERM_A " * " TERM_M " + " TERM_B ")\n",
                         r.ToString().c_str(),
                         triple.k.ToString().c_str(),
                         triple.a.ToString().c_str(),
                         base_m.ToString().c_str(),
                         triple.b.ToString().c_str());
        }

        if (VERBOSE) {
          printf("We have k | (am + b):\n"
                 TERM_K " | (" TERM_A " * " TERM_M " + " TERM_B ")\n",
                 triple.k.ToString().c_str(),
                 triple.a.ToString().c_str(),
                 base_m.ToString().c_str(),
                 triple.b.ToString().c_str());
        }

        return base_m;
      };

    // "Base" values for m.
    BigInt mleft = GetBaseM(nleft, left);
    BigInt mright = GetBaseM(nright, right);

    // Now we have many choices (infinite, even) of mleft and mright
    // that yield new triples.
    // Specifically mleft + x * left.k    and   mright + y * right.k
    // are divisible by the corresponding k, so they will work.
    // Next, we choose the values of m (i.e. x,y) that minimize the metric.

    auto LeftTriple = [&nleft, &mleft, &left](const BigInt &x) {
        BigInt m = mleft + x * left.k;
        if (CHECK_INVARIANTS) {
          CHECK((left.a * m + left.b) % left.k == BigInt{0});
          CHECK((left.b * m + nleft * left.a) % left.k == BigInt{0});
          CHECK((m * m - nleft) % left.k == BigInt{0});
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
          CHECK((right.a * m + right.b) % right.k == BigInt{0});
          CHECK((right.b * m + nright * right.a) % right.k == BigInt{0});
          CHECK((m * m - nright) % right.k == BigInt{0});
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
    const auto [xy, score] =
      Opt::Minimize2D(
          [&seen, &LeftTriple, &RightTriple](double xf, double yf) -> double {
            int64_t xi = xf;
            int64_t yi = yf;

            BigInt x{xi};
            BigInt y{yi};

            if (VERY_VERBOSE)
              printf("Try %.4f,%.4f\n", xf, yf);
            Triple new_left = LeftTriple(x);
            Triple new_right = RightTriple(y);

            // infeasible if a = 0 for either tuple.
            if (new_left.a == BigInt{0} ||
                new_right.a == BigInt{0})
              return 1e200;

            // infeasible if already attempted.
            if (seen.contains(std::make_pair(new_left, new_right)))
              return 1e200;

            // should also skip degenerate solutions?
            BigInt metric = Metric(new_left, new_right);
            // Metric will be nonnegative, but avoid -oo.
            double score = BigInt::NaturalLog(metric + BigInt{1});
            if (VERY_VERBOSE)
              printf("Score: %.4f\n", score);
            return score;
          },
          std::make_tuple(-10.0, -10.0),
          std::make_tuple(+10.0, +10.0),
          // std::make_tuple(-1e53, 1e53),
          // std::make_tuple(-1e53, 1e53),
          1000);

    if (VERBOSE) {
      printf("Opt: Got %.3f, %.3f with score %.3f\n",
             std::get<0>(xy),
             std::get<1>(xy),
             score);
    }

    // PERF with Optimizer, we don't have to recompute the triple.
    // But there may be other costs with that.
    Triple new_left = LeftTriple(BigInt((int64_t)std::get<0>(xy)));
    Triple new_right = RightTriple(BigInt((int64_t)std::get<1>(xy)));

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
                       TERM_A "," TERM_B "," TERM_K ") but acutal "
                       "error is " ARED("%s") "\n",
                       n.ToString().c_str(),
                       triple.a.ToString().c_str(),
                       triple.b.ToString().c_str(),
                       triple.k.ToString().c_str(),
                       err.ToString().c_str());
      };

    CheckTriple(nleft, new_left);
    CheckTriple(nright, new_right);

    left = std::move(new_left);
    right = std::move(new_right);
  }
}

#if 0
int main(int argc, char **argv) {
  ANSI::Init();
  printf("Start.\n");

  // two equations
  // 222121 x^2 + 1 = y_a^2
  // 360721 x^2 + 1 = y_h^2
  // (y_a is called a in the square; y_h is called h. but we are going
  // to use a,b,c for other coefficients here.)

  const BigInt n_a = 222121_b;
  const BigInt n_h = 360721_b;

  printf("n_a: %s\n", n_a.ToString().c_str());
  printf("n_h: %s\n", n_h.ToString().c_str());

  const Sol sol_a0 =
    make_pair(96853990143729182446466254903787102920420186066745135266595909679523238492820905098594426380259780074224586852201498992449275127756378129855820172958584294747666448702649536007880590904630402994485054414777494200716316411350928848627022864018323664548200672490871343394924009459667481509707703598930232145417695483619768820532418380893489621163234820873999155059853985917579838562873820424199495555730452754055968329456327571681829182817764194271946670769735346243288252413916542521030793275016960_b,
              45647009151151305718708252078329478619159688396560344940484846739545404737863346364814575382380658468268696696162077346578703916050375366743990587977745934350267627720790556553334680620537062793215771183142869525305229082597324857232212169256203590974623367552076861467765584622698250583658967988921977292909237331892140561912612147974391374706186555174904116425103638903793323393015436354152507544138006929555916470598773073491241252241089212693998884441309720519067489536318638130578697131025203199_b);

  CHECK(Error(n_a, sol_a0) == 1_b);

  const Sol sol_h0 =
    make_pair(5254301467178253668063501573805488115701470062129148344048204657019748885380657942526435850589822056436907162208858662833748764291164063392849591565232182340896344102417561460498588341287763598974028219631368919894791141180699352614842162329575092178804714936187264001803984435149307802237910897195756701561689733829416441654000294332579338938600777476665373690729282455769460482322102437685615931720435571771362615173743048328285921760676802224631583102137529433447859249096920912083501391265263092551585295016322744236018859511648734714069848447775860243716078071973487185423849887186837594810420676451477817334917442425724234656945231269179119186700637627642532511974556138751986944397915640033290288664241890733655443663188699611507335767790934980909769231545567661414370796993132434555657909168192875390617801233376652983464964576564440125236085759750607806749224929539111304657072598148941302658707149037639442679980053501547520718161071834317467291327489266555022779213264638058690142715120_b,
              3155736260680638626215586515224779475000982538318510234796596948969542279640527333764830931669758997548264003306978761631985610163542280769957617791837115815855505466192424155483747195334026528686100467671456903838445918904622247229490490143150205748045220079365084818251874000588893203069462517365771614948335859034465574562253629688393366532513342848079469578491642318761744360133449340864599791431253295837903633977372300833275627599528361343594279999123827422243803042839623732782710017503150424671269804820044584384922764477211533947638524985602275604505195848101327992205477251957539278637282982381170957580513664760124760363535719750512488336024599849383281905125902008900085015792063893406749523426160808130369628761209934170648229913920676290078798350463180533834821461892201220681643823782682294011883387286116223752801467866509139817280697848323355423553433987427070952556267278422608907045802820393525018738409778215962756148887404031077236716404130655643413294151356469733756144336771201_b);

  BigInt err1 = Error(n_h, sol_h0);
  CHECK(err1 == 1_b) << err1.ToString();

  /*
  printf("sol_a0: %s\n", SolString(sol_a0).c_str());
  printf("sol_h0: %s\n", SolString(sol_h0).c_str());
  */

  for (int ni = 2; ni < 150; ni++) {
    uint64_t s = Sqrt64(ni);
    // This algorithm doesn't work for perfect squares.
    if (s * s == ni)
      continue;

    BigInt n{ni};
    Sol start_sol = make_pair(1_b, 8_b);
    Sol bsol = Bhaskara(n, Error(n, start_sol), start_sol);

    printf("Derived solution for n = " TERM_N "\n"
           "a: " TERM_A "\n"
           "b: " TERM_B "\n",
           n.ToString().c_str(),
           bsol.first.ToString().c_str(),
           bsol.second.ToString().c_str());
    CHECK(bsol.first != BigInt{0});
    CHECK(Error(n, bsol) == BigInt{1});
    printf(AGREEN("OK") "\n");
  }

  {
    Sol bsol = Bhaskara(n_a, Error(n_a, sol_h0), sol_h0);
    printf("Derived solution for n_a = " TERM_N "\n"
           "a: " TERM_A "\n"
           "b: " TERM_B "\n",
           n_a.ToString().c_str(),
           bsol.first.ToString().c_str(),
           bsol.second.ToString().c_str());
    CHECK(bsol.first != BigInt{0});
    CHECK(Error(n_a, bsol) == BigInt{1});
    printf(AGREEN("OK") "\n");
  }

  {
    Sol bsol = Bhaskara(n_h, Error(n_h, sol_a0), sol_a0);
    printf("Derived solution for n_h = " TERM_N "\n"
           "a: " TERM_A "\n"
           "b: " TERM_B "\n",
           n_h.ToString().c_str(),
           bsol.first.ToString().c_str(),
           bsol.second.ToString().c_str());
    CHECK(bsol.first != BigInt{0});
    CHECK(Error(n_h, bsol) == BigInt{1});
    printf(AGREEN("OK") "\n");
  }


  printf(AGREEN("OK") " :)\n");
  return 0;
}

#endif

int main(int argc, char **argv) {
  ANSI::Init();
  printf("Start.\n");

  for (int ni = 2; ni < 150; ni++) {
    uint64_t s = Sqrt64(ni);
    // This algorithm doesn't work for perfect squares.
    if (s * s == ni)
      continue;

    BigInt n{ni};
    // Solve the same triple twice.
    Triple start_left(1_b, 8_b, Error(n, 1_b, 8_b));
    Triple start_right(1_b, 8_b, Error(n, 1_b, 8_b));

    const auto [left, right] =
      DualBhaskara(n, n, start_left, start_right);

    // Not actually guaranteed??
    CHECK(left == right);
    // We allow +/- 1 for this version.
    CHECK(BigInt::Abs(left.k) == BigInt{1});
    printf("Derived solution for n = " TERM_N "\n"
           "a: " TERM_A "\n"
           "b: " TERM_B "\n",
           n.ToString().c_str(),
           left.a.ToString().c_str(),
           left.b.ToString().c_str());
    CHECK(left.a != BigInt{0});
    CHECK(Error(n, left.a, left.b) == left.k);
    printf(AGREEN("OK") "\n");
  }


  printf(AGREEN("OK") " :)\n");
  return 0;
}
