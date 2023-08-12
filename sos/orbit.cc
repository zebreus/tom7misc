
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
#include "image.h"
#include "bounds.h"
#include "arcfour.h"
#include "randutil.h"

#include "sos-util.h"

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

enum Method {
  SOLVE,
  BLACK_BOX,
  GREEDY_9,
  RANDOM_WALK,
  COMPASS_EXPONENTIAL,
};

static constexpr Method method = RANDOM_WALK;

// Map a bigint to a reasonable range for plotting in graphics.
// (or a.ToDouble(), or some hybrid?)
static double MapBig(BigInt z) {
  if (z == BigInt{0}) return 0.0;
  double sign = 1.0;
  if (z < BigInt{0}) {
    sign = -1.0;
    z = BigInt::Abs(z);
  }

  double d = sign * BigInt::NaturalLog(z);
  CHECK(!std::isnan(d));
  CHECK(std::isfinite(d));
  return d;
}

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

// PERF avoid copying
struct BestPairFinder {
  std::pair<Triple, Triple> best;
  std::optional<BigInt> best_score;

  void ObserveWithMetric(const Triple &left, const Triple &right,
                         const BigInt &metric) {
    if (left.a == BigInt{0} || right.a == BigInt{0})
      return;

    if (!best_score.has_value() || metric < best_score.value()) {
      best = make_pair(left, right);
      best_score = {metric};
    }
  }

  void Observe(const Triple &left, const Triple &right) {
    BigInt metric = Metric(left, right);
    ObserveWithMetric(left, right, metric);
  }

  // Zeros if there were no valid observations.
  std::pair<Triple, Triple> Best() {
    return best;
  }

};

// Given two equations of the form
//     na^2 + k = b^2
// where (a, b, k) is the "triple",
// Try to find two triples t1, t2 where t1.a=t2.a and t1.k = t2.k = 1.
static std::pair<Triple, Triple>
DualBhaskara(BigInt nleft, BigInt nright, Triple left, Triple right) {
  ArcFour rc("dualbhaskara");

  if (VERBOSE)
    printf(AWHITE(ABGCOLOR(0, 0, 50, "---------- on %s,%s ------------")) "\n",
           nleft.ToString().c_str(),
           nright.ToString().c_str());

  Periodically image_per(10.0 * 60.0);
  // run after the first minute for quick feedback on experiments
  image_per.SetPeriodOnce(60.0);
  int image_idx = 0;

  Periodically bar_per(1.0);
  bool first_progress = true;
  const BigInt start_metric = Metric(left, right);
  const int start_metric_size = start_metric.ToString().size();

  Timer timer;

  // Pair of triples (left, right) that we've already tried. We avoid
  // exploring them a second time, as this would lead to a cycle.
  TriplePairSet seen;

  std::vector<std::pair<Triple, Triple>> history;

  int last_repeats = 0, last_attempts = 0;
  for (int iters = 0; true; iters ++) {
    if (CHECK_INVARIANTS) {
      CHECK(!seen.contains(std::make_pair(left, right)));
    }
    seen.insert(std::make_pair(left, right));
    if (GENERATE_IMAGE) {
      history.emplace_back(left, right);
    }

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
                     &last_repeats, &last_attempts, &a_diff,
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
                   StringPrintf("%d its, dig: ks: %d diff: %d max-b: %d"
                                " rep: %d/%d",
                                iters, k_digits,
                                a_diff_digits,
                                max_b_digits,
                                last_repeats, last_attempts),
                   timer.Seconds()).c_str());
      });
    }

    if (GENERATE_IMAGE && image_per.ShouldRun()) {
      enum Shape {
        DISC,
        CIRCLE,
        FILLSQUARE,
      };

      image_idx++;

      // xy plot
      {
        printf("x/y plot\n");
        Bounds bounds;
        bounds.Bound(0, 0);

        for (int x = 0; x < history.size(); x++) {
          const auto &[t1, t2] = history[x];
          bounds.Bound(MapBig(t1.a), MapBig(t2.a));
          bounds.Bound(MapBig(t1.b), MapBig(t2.b));
          bounds.Bound(MapBig(t1.k), MapBig(t2.k));
        }
        if (bounds.Empty()) {
          printf("Bounds empty.\n");
        } else {
          printf("Bounds: %.3f,%.3f -- %.3f,%.3f\n",
                 bounds.MinX(), bounds.MinY(),
                 bounds.MaxX(), bounds.MaxY());
        }


        const int WIDTH = 1900;
        const int HEIGHT = 1900;
        ImageRGBA img(WIDTH, HEIGHT);
        img.Clear32(0x000000FF);
        Bounds::Scaler scaler = bounds.Stretch(WIDTH, HEIGHT).FlipY();

        const auto [x0, y0] = scaler.Scale(0, 0);
        img.BlendLine32(x0, 0, x0, HEIGHT - 1, 0xFFFFF22);
        img.BlendLine32(0, y0, WIDTH - 1, y0, 0xFFFFF22);

        for (int i = 0; i < history.size(); i++) {
          const auto &[t1, t2] = history[i];

          auto Plot = [&img, &scaler](const BigInt &x, const BigInt &y,
                                      uint32_t color, Shape shape) {
              auto [sx, sy] = scaler.Scale(MapBig(x), MapBig(y));

              switch (shape) {
              case DISC:
                img.BlendFilledCircleAA32(sx, sy, 2.0f, color);
                break;
              case CIRCLE:
                img.BlendCircle32(sx, sy, 3, color);
                break;
              case FILLSQUARE:
                img.BlendRect32(sx, sy, 3, 3, color);
                break;
              }
            };

          Plot(t1.a, t2.a, 0xFF333355, DISC);
          Plot(t1.b, t2.b, 0x77FF7755, CIRCLE);
          Plot(t1.k, t2.k, 0x5555FF55, FILLSQUARE);
        }

        const int textx = 32;
        int texty = 32;
        img.BlendText32(textx + 18, texty, 0x555555FF,
                        StringPrintf("Iters: %d", iters));
        texty += ImageRGBA::TEXT2X_HEIGHT + 2;
        img.BlendFilledCircleAA32(textx + 8, texty + 8, 6.0f, 0xFF3333AA);
        img.BlendText2x32(textx + 18, texty, 0xFF3333AA, "a");
        texty += ImageRGBA::TEXT2X_HEIGHT + 2;
        img.BlendCircle32(textx + 7, texty + 7, 5, 0x77FF77AA);
        img.BlendText2x32(textx + 18, texty, 0x77FF77AA, "b");
        texty += ImageRGBA::TEXT2X_HEIGHT + 2;
        img.BlendRect32(textx + 2, texty + 2, 11, 11, 0x5555FFAA);
        img.BlendText2x32(textx + 18, texty, 0x5555FFAA, "k");

        string filename = StringPrintf("xyplot-%d.png", image_idx);
        img.Save(filename);
        printf("Wrote " ACYAN("%s"), filename.c_str());
      }

      // seismograph
      {
        printf("seismograph\n");
        Bounds bounds;
        bounds.Bound(0, 0);

        auto BoundFiniteY = [&bounds](double d) {
            if (std::isfinite(d)) bounds.BoundY(d);
          };
        auto BoundTriple = [&BoundFiniteY](const Triple &triple) {
            BoundFiniteY(MapBig(triple.a));
            BoundFiniteY(MapBig(triple.b));
            BoundFiniteY(MapBig(triple.k));
          };

        for (int x = 0; x < history.size(); x++) {
          const auto &[t1, t2] = history[x];
          BoundTriple(t1);
          BoundTriple(t2);
        }
        bounds.BoundX(0);
        bounds.BoundX(history.size() - 1);

        const int WIDTH = 1024 * 3;
        const int HEIGHT = 1024;
        ImageRGBA img(WIDTH, HEIGHT);
        img.Clear32(0x000000FF);
        Bounds::Scaler scaler = bounds.Stretch(WIDTH, HEIGHT).FlipY();

        BigInt max_a{0}, max_b{0}, max_k{0};
        std::optional<BigInt> best_k = nullopt;

        for (int x = 0; x < history.size(); x++) {
          auto Plot = [x, &img, &scaler, WIDTH, HEIGHT](
              const BigInt by, uint32_t rgb, Shape shape) {

              uint32_t color = (rgb << 8) | 0x80;

              double y = MapBig(by);
              int sx = scaler.ScaleX(x);
              int sy = 0;
              if (!std::isfinite(y)) {
                if (y > 0.0) sy = 0;
                else sy = HEIGHT - 1;
              } else {
                sy = scaler.ScaleY(y);
              }
              switch (shape) {
              case DISC:
                img.BlendFilledCircleAA32(sx, sy, 2.0f, color);
                break;
              case CIRCLE:
                img.BlendCircle32(sx, sy, 3, color);
                break;
              case FILLSQUARE:
                img.BlendRect32(sx, sy, 3, 3, color);
                break;
              }
            };

          const auto &[t1, t2] = history[x];
          if (t1.a > max_a) max_a = t1.a;
          if (t1.b > max_a) max_b = t1.b;
          if (BigInt::Abs(t1.k) > max_k) max_k = BigInt::Abs(t1.k);
          Plot(t1.a, 0xFF0000, CIRCLE);
          Plot(t1.b, 0xFF3300, CIRCLE);
          Plot(t1.k, 0xFF7700, CIRCLE);

          if (t2.a > max_a) max_a = t2.a;
          if (t2.b > max_a) max_b = t2.b;
          if (BigInt::Abs(t2.k) > max_k) max_k = BigInt::Abs(t2.k);
          Plot(t2.a, 0x0000FF, DISC);
          Plot(t2.b, 0x0033FF, DISC);
          Plot(t2.k, 0x0077FF, DISC);

          if (t1.a == t2.a) {
            BigInt totalk = BigInt::Abs(t1.k) + BigInt::Abs(t2.k);
            if (!best_k.has_value() ||
                totalk < best_k.value()) best_k = {std::move(totalk)};
          }
        }
        int texty = 0;
        img.BlendText32(4, texty, 0x555555FF,
                        StringPrintf("Iters: %d", iters));
        texty += ImageRGBA::TEXT_HEIGHT + 1;
        img.BlendText32(4, texty, 0xFF0000FF,
                        StringPrintf("Max a: %s", max_a.ToString().c_str()));
        texty += ImageRGBA::TEXT_HEIGHT + 1;
        img.BlendText32(4, texty, 0xFFFF00FF,
                        StringPrintf("Max b: %s", max_b.ToString().c_str()));
        texty += ImageRGBA::TEXT_HEIGHT + 1;
        img.BlendText32(4, texty, 0x77FF00FF,
                        StringPrintf("Max k: %s", max_k.ToString().c_str()));
        texty += ImageRGBA::TEXT_HEIGHT + 1;
        img.BlendText32(4, texty, 0x77FF00FF,
                        StringPrintf("Best k: %s",
                                     best_k.has_value() ?
                                     best_k.value().ToString().c_str() :
                                     "(none)"));
        texty += ImageRGBA::TEXT_HEIGHT + 1;

        string filename = StringPrintf("seismograph-%d.png", image_idx);
        img.Save(filename);
        printf("Wrote " ACYAN("%s"), filename.c_str());
      }
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
    last_repeats = 0;
    last_attempts = 0;

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

    } else if (method == SOLVE) {

      // Looking for x,y, integers.

      BigInt m = mleft + x * left.k;
      BigInt new_a = BigInt::Abs((left.a * m + left.b) / left.k);
      BigInt new_b = BigInt::Abs((left.b * m + nleft * left.a) / left.k);
      BigInt new_k = (m * m - nleft) / left.k;

      // m1' = m1 + x1 * k1
      // m2' = m2 + x2 * k2

      // a1' = |(a1 * m1' + b1) / k1|
      // a2' = |(a2 * m2' + b2) / k2|
      // which is
      // a1' = |(a1 * (m1 + x1 * k1) + b1) / k1|
      // a1' = |(a1 * m1 + a1 * x1 * k1 + b1) / k1|
      // a1' = |(a1 * m1 + b1) / k1  +  a1 * x1 * k1 / k1|
      //  so
      // a1' = |(a1 * m1 + b1) / k1  +  a1 * x1|
      // a2' = |(a2 * m2 + b2) / k2  +  a2 * x2|
      //
      // Note that only the second part depends on x1,x2.
      //
      // minimize |(a1 * m1 + b1) / k1  +  a1 * x1| -
      //          |(a2 * m2 + b2) / k2  +  a2 * x2|
      //
      // for each component, only k and x can be negative, and we already
      // know k. So we could expand those cases to get functions to minimize.

      // when k and x have the same sign,
      //   |(a * m + b) / k| + |a * x|
      // which is just
      //   (a * m + b) / |k| + a * |x|


      // Actually instead of abs let's try minimizing squares.
      //  (((a1 * m1 + b1) / k1  +  a1 * x1)^2 -
      //   ((a2 * m2 + b2) / k2  +  a2 * x2)^2)^2
      //
      // ((a1 * m1 + b1) / k1  +  a1 * x1)^2  multiplied out...
      //   (a * m + b)^2 / k^2 +
      //   2 * (a * x)(a * m + b) / k +
      //   a^2 * x^2
      // which is
      //   (a^2m^2 + 2abm + b^2)/k^2 +
      //   2ax(am + b)/k +
      //   a^2x^2
      // so that term minus its sibling, squared
      //  ( (a1^2m1^2 + 2a1b1m1 + b1^2)/k1^2 +
      //    2a1x1(a1m1 + b1)/k1 +
      //    a1^2x1^2  -
      //    (a2^2m2^2 + 2a2b2m2 + b2^2)/k2^2 +
      //    2a2x2(a2m2 + b2)/k2 +
      //    a2^2x2^2 )^2
      // u1 = (a1^2m1^2 + 2a1b1m1 + b1^2)/k1^2
      // v1 = 2a1x1(a1m1 + b1)/k1
      // w1 = a1^2x1^2
      // t1 = (u1 + v1 + w1)
      //
      // (t1 - t2)^2 =
      // t1^2 - 2t1t2 + t2^2
      // (u1 + v1 + w1)^2 - 2(u1 + v1 + w1)(u2 + v2 + w2) + (u2 + v2 + w2)^2
      //
      // cripes...
      // the squared terms (first and last) are the same.
      //  u1^2 + u1v1 + u1w1 + v1^2 + v1u1 + v1w1 + w1^2 + w1u1 + w1u1
      // which is just
      //  u1^2 + v1^2 + w1^2 + 2u1v1 + 2u1w1 + 2v1w1
      // and then the middle term
      //  2(u1u2 + u1v2 + u1w2  +  v1u2 + v1v2 + v1w2  +  w1u2 + w1v2 + w1w2)
      // so together that's
      //
      // u1^2 + v1^2 + w1^2 + 2u1v1 + 2u1w1 + 2v1w1 +
      // u2^2 + v2^2 + w2^2 + 2u2v2 + 2u2w2 + 2v2w2 +
      // -2(u1u2 + u1v2 + u1w2  +  v1u2 + v1v2 + v1w2  +  w1u2 + w1v2 + w1w2)
      //
      // Perhaps not too crazy? Starting to think I should build a little
      // tool to multiply out polynomials though.

      BigInt diff = sqrtn - m;
        BigInt d = (diff / k) * k;
        BigInt m1 = m + d;
        if (VERBOSE) {
          printf("For target " TERM_SQRTN " have diff = " AYELLOW("%s")
                 " and d = " APURPLE("%s") " and m1 = " TERM_M " +/- k\n",
                 sqrtn.ToString().c_str(),
                 diff.ToString().c_str(),
                 d.ToString().c_str(),
                 m1.ToString().c_str());
        }
        Consider(m1);
        Consider(m1 + k);
        Consider(m1 - k);
      };


    } else if (method == COMPASS_EXPONENTIAL) {

      BestPairFinder finder;

      // This one walks along compass directions, but greedily
      // keeps going if doubling the distance reduces the metric.
      for (int dx : {-1, 0, 1}) {
        for (int dy : {-1, 0, 1}) {
          if (dx != 0 || dy != 0) {

            // walk until we find something new
            for (int x = 0, y = 0; ; x += dx, y += dy) {
              // This method is probably terrible, but in case we
              // wanted to optimize it, we can probably "increment"
              // triples in place.
              BigInt xx{x}, yy{y};
              Triple new_left = LeftTriple(xx);
              Triple new_right = RightTriple(yy);

              last_attempts++;
              if (!seen.contains(std::make_pair(new_left, new_right))) {
                BigInt prev_metric = Metric(new_left, new_right);
                finder.ObserveWithMetric(new_left, new_right, prev_metric);
                // Now greedily continue.
                for (;;) {
                  // PERF shift in place
                  xx = xx * BigInt{2};
                  yy = yy * BigInt{2};
                  Triple more_left = LeftTriple(xx);
                  Triple more_right = RightTriple(yy);

                  last_attempts++;
                  BigInt this_metric = Metric(more_left, more_right);
                  if (!seen.contains(std::make_pair(more_left, more_right))) {
                    // Only observe it if it hasn't been seen before,
                    // but keep doubling while we're headed in the
                    // right direction either way.
                    finder.ObserveWithMetric(
                        more_left, more_right, this_metric);
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

      std::tie(new_left, new_right) = finder.Best();



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
              if (new_left.a == BigInt{0} ||
                  new_right.a == BigInt{0})
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

    CheckTriple(nleft, new_left);
    CheckTriple(nright, new_right);

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
  CHECK(BigInt::Abs(left.k) == BigInt{1});
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

  CHECK(left.a != BigInt{0});
  CHECK(right.a != BigInt{0});
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

  Triple start_left(left_a, left_b, left_err);
  Triple start_right(right_a, right_b, right_err);
  DoPair(n_left, n_right, start_left, start_right);

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

  BigInt left_a = 1_b, left_b = 8_b;
  BigInt right_a = 1_b, right_b = 7_b;
  Triple start_left(left_a, left_b, Error(nleft, left_a, left_b));
  Triple start_right(right_a, right_b, Error(nright, right_a, right_b));

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
  NontrivialProblem();

  // RealProblem();

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
