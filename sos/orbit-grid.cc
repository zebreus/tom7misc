
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
#include "threadutil.h"
#include "color-util.h"
#include "atomic-util.h"

#include "sos-util.h"

using namespace std;

static constexpr bool CHECK_INVARIANTS = false;
static constexpr bool VERBOSE = false;
static constexpr bool VERY_VERBOSE = VERBOSE && false;
static constexpr bool GENERATE_IMAGE = true;

static constexpr int STUCK_ITERS = 10000;
static constexpr int MAX_ITERS = 1'000'000;

#define TERM_A AFGCOLOR(39, 179, 214, "%s")
#define TERM_M AFGCOLOR(39, 214, 179, "%s")
#define TERM_B AFGCOLOR(232, 237, 173, "%s")
#define TERM_K AFGCOLOR(220, 173, 237, "%s")
#define TERM_N AFGCOLOR(227, 198, 143, "%s")
#define TERM_SQRTN AFGCOLOR(210, 200, 180, "%s")
#define TERM_E AFGCOLOR(200, 120, 140, "%s")

enum Method {
  GREEDY_9,
  COMPASS_EXPONENTIAL,
  COMPASS25_EXPONENTIAL,
  RESTARTING_COMPASS,
};

static constexpr Method method = RESTARTING_COMPASS;

// Map a bigint to a reasonable range for plotting in graphics.
// (or a.ToDouble(), or some hybrid?)
static double MapBig(BigInt z) {
  if (z == 0) return 0.0;
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

// New metric uses squares instead of absolute value, which is
// easier to differentiate.
static BigInt Metric(const Triple &left, const Triple &right) {
  BigInt lkk = left.k * left.k;
  BigInt rkk = right.k * right.k;
  BigInt adiff = left.a - right.a;
  BigInt res =
    // Try to get a k of +/- 1.
    lkk + rkk +
    // But also try to minimize the difference between the first
    // components of the triple. bs are unconstrained.
    (adiff * adiff);
  return res;
}

// PERF avoid copying
template<typename Key>
struct KeyedBestPairFinder {
  std::pair<Triple, Triple> best;
  std::optional<std::pair<Key, BigInt>> best_score;

  void ObserveWithMetric(const Triple &left, const Triple &right,
                         const BigInt &metric, Key key = Key{}) {
    bool forbidden = left.a == 0 || right.a == 0;
    if (VERY_VERBOSE) {
      printf("Observe (" TERM_A "," TERM_B "," TERM_K
             ") and (" TERM_A "," TERM_B "," TERM_K "). Metric: "
             TERM_M "%s\n",
             left.a.ToString().c_str(),
             left.b.ToString().c_str(),
             left.k.ToString().c_str(),
             right.a.ToString().c_str(),
             right.b.ToString().c_str(),
             right.k.ToString().c_str(),
             metric.ToString().c_str(),
             forbidden ? ARED(" NO") : "");
    }

    if (forbidden)
      return;

    if (!best_score.has_value() || metric < best_score.value().second) {
      best = make_pair(left, right);
      best_score = {make_pair(key, metric)};
    }
  }

  void Observe(const Triple &left, const Triple &right, Key key = Key{}) {
    BigInt metric = Metric(left, right);
    ObserveWithMetric(left, right, metric, key);
  }

  // Zeros if there were no valid observations.
  const std::pair<Triple, Triple> &Best() {
    return best;
  }

  const std::optional<std::pair<Key, BigInt>> &BestScore() {
    return best_score;
  }
};

using BestPairFinder = KeyedBestPairFinder<char>;

static std::string LongNum(const BigInt &a) {
  std::string num = a.ToString();
  if (num.size() > 80) {
    static constexpr int SHOW_SIDE = 8;
    int skipped = num.size() - (SHOW_SIDE * 2);
    return StringPrintf("%s…(%d)…%s",
                        num.substr(0, SHOW_SIDE).c_str(),
                        skipped,
                        num.substr(num.size() - SHOW_SIDE,
                                   string::npos).c_str());
  } else {
    return num;
  }
}

static std::mutex best_k_mutex;
// Not interested in anything unless it's better than this.
static int64_t best_total_k = 760;

// Return the number of iterations before we get "stuck."
static int64_t
DualBhaskara(BigInt nleft, BigInt nright, Triple left, Triple right) {
  BigInt sqrtnleft = BigInt::Sqrt(nleft);
  BigInt sqrtnright = BigInt::Sqrt(nright);

  // Pair of triples (left, right) that we've already tried. We avoid
  // exploring them a second time, as this would lead to a cycle.
  // Necessary?
  TriplePairSet seen;

  std::optional<BigInt> recent_best_score;
  int recent_best_iter = 0;

  int valid_since_reset = 0;
  std::optional<BigInt> best_valid_since_reset;
  for (int iters = 0; true; iters ++) {
    if (CHECK_INVARIANTS) {
      CHECK(!seen.contains(std::make_pair(left, right)));
    }
    seen.insert(std::make_pair(left, right));

    if (left.a == 0 || right.a == 0) {
      // Not allowed.
      return iters;
    }

    BigInt a_diff = left.a - right.a;

    // Are we done?
    if (a_diff == 0) {
      BigInt ak1 = BigInt::Abs(left.k);
      BigInt ak2 = BigInt::Abs(right.k);
      BigInt total_k = ak1 + ak2;
      // Only care about small k.
      std::optional<int64_t> total_ko = total_k.ToInt();

      valid_since_reset++;
      if (!best_valid_since_reset.has_value() ||
          total_k < best_valid_since_reset.value()) {
        best_valid_since_reset = {total_k};
      }

      if (total_ko.has_value()) {
        MutexLock ml(&best_k_mutex);

        if (total_ko.value() < best_total_k) {
          printf("\n\n\n" AGREEN("New best k1/k2") ": %s\n\n\n",
                 total_k.ToString().c_str());
          FILE *f = fopen("grid-bestk.txt", "ab");
          fprintf(f,
                  "** after %d iters: %s\n"
                  "a = %s\n"
                  "b1 = %s\n"
                  "b2 = %s\n"
                  "k1 = %s\n"
                  "k2 = %s\n",
                  iters,
                  total_k.ToString().c_str(),
                  left.a.ToString().c_str(),
                  left.b.ToString().c_str(),
                  right.b.ToString().c_str(),
                  left.k.ToString().c_str(),
                  right.k.ToString().c_str());
          fclose(f);
          best_total_k = total_ko.value();
        }
      }
      if (ak1 == 1 && ak2 == 1) {
        printf("** after %d iters: %s\n"
               "a = %s\n"
               "b1 = %s\n"
               "b2 = %s\n"
               "k1 = %s\n"
               "k2 = %s\n",
               iters,
               total_k.ToString().c_str(),
               left.a.ToString().c_str(),
               left.b.ToString().c_str(),
               right.b.ToString().c_str(),
               left.k.ToString().c_str(),
               right.k.ToString().c_str());

        CHECK(false) << AGREEN("Success??");
        return 0;
      }
    }

    // If we haven't improved the score in some time, choose a
    // new random point.
    if (recent_best_score.has_value() &&
        (iters - recent_best_iter) > STUCK_ITERS) {
      return iters;
    }

    // Cap iterations too.
    if (iters >= MAX_ITERS) return MAX_ITERS;

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
        if (gcd != 1) {
          if (CHECK_INVARIANTS) {
            CHECK(triple.a % gcd == 0);
            CHECK(triple.b % gcd == 0);
          }
          triple.a = triple.a / gcd;
          triple.b = triple.b / gcd;

          triple.k = Error(n, triple.a, triple.b);
        }

        // Now we need m such that am+b is divisible by k.
        // That's the same as saying that am mod k  =  -b mod k.
        // aka. am = -b (mod k).
        BigInt negbmodk = -triple.b % triple.k;

        // Now we can find the multiplicative inverse of a mod k,
        // using the extended euclidean algorithm.
        const auto [g, s, t] = BigInt::ExtendedGCD(triple.a, triple.k);
        // now we have a*s + k*t = g.
        CHECK(g == 1) << "?? Don't know why this must be true, "
          "but it's seemingly assumed by descriptions of this?";

        // so if a*s + k*t = 1, then a*s mod k is 1 (because k*t is 0 mod k).
        // In other words, s is the multiplicative inverse of a (mod k).
        // so if am = -b (mod k), then a * (a^-1) * m = -b * (a^-1)  (mod k)
        // and thus m = -b * (a^-1)  (mod k), which is -b * s.
        BigInt base_m = (negbmodk * s) % triple.k;

        if (CHECK_INVARIANTS) {
          BigInt r = ((triple.a * base_m + triple.b) % triple.k);
          CHECK(r == 0) <<
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

                  BigInt this_metric = Metric(more_left, more_right);
                  if (!seen.contains(std::make_pair(more_left, more_right))) {
                    // Only observe it if it hasn't been seen before,
                    // but keep doubling while we're headed in the
                    // right direction either way.
                    finder->ObserveWithMetric(
                        more_left, more_right, this_metric,
                        make_pair(dx, dy));
                  }

                  if (this_metric < prev_metric) {
                    prev_metric = this_metric;
                  } else {
                    break;
                  }
                }

                break;
              }
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

              if (!seen.contains(std::make_pair(new_left, new_right))) {
                finder.Observe(new_left, new_right);
                break;
              }
            }
          }
        }
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

      CompassBestPairFinder finder;

      auto Consider = [&](const BigInt &x, const BigInt &y) {
          Triple new_left = LeftTriple(x);
          Triple new_right = RightTriple(y);

          if (!seen.contains(std::make_pair(new_left, new_right))) {
            finder.Observe(new_left, new_right);
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
        }
      }
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
  }
}

static void MakeGrid(const BigInt &n_left, const BigInt &n_right) {
  ArcFour rc("makegrid");
  static constexpr int WIDTH = 384;
  static constexpr int HEIGHT = 384;

  DECLARE_COUNTERS(done, u1_, u2_, u3_, u4_, u5_, u6_, u7_);

  // std::array<int64_t, WIDTH * HEIGHT> count;
  // for (int64_t &i : count) i = 0;
  std::vector<int64_t> count(WIDTH * HEIGHT, -1);

  Timer run_timer;
  Periodically bar_per(1.0);
  Periodically img_per(10.0 * 60.0);
  img_per.SetPeriodOnce(60.0);

  static constexpr int NUM_THREADS = 12;

  string filename =
    StringPrintf("orbit-grid_%s_%s.png",
                 n_left.ToString().c_str(),
                 n_right.ToString().c_str());
  auto SaveImage = [&count, &filename]() {
      // Now make image.
      int64_t min_its = 9999999999999, max_its = 0;
      for (const int64_t i : count) {
        max_its = std::max(max_its, i);
        if (i >= 0) {
          min_its = std::min(min_its, i);
        }
      }
      printf("Iterations in: [%lld, %lld]\n", min_its, max_its);
      double norm = max_its - min_its;

      ImageRGBA img(WIDTH, HEIGHT);
      for (int sy = 0; sy < HEIGHT; sy++) {
        for (int sx = 0; sx < WIDTH; sx++) {
          const int x = sx;
          const int y = (HEIGHT - 1) - sy;
          double d = (count[y * WIDTH + x] - min_its) / norm;
          uint32_t color = ColorUtil::LinearGradient32(
              ColorUtil::HEATED_METAL,
              d);
          img.SetPixel32(sx, sy, color);
        }
      }
      img.ScaleBy(2).Save(filename);
      printf("Wrote " ACYAN("%s") "\n", filename.c_str());
    };

  std::vector<int> indices;
  indices.reserve(WIDTH * HEIGHT);
  for (int i = 0; i < WIDTH * HEIGHT; i++) indices.push_back(i);
  Shuffle(&rc, &indices);

  const BigInt sqrtnleft = BigInt::Sqrt(n_left);
  const BigInt sqrtnright = BigInt::Sqrt(n_right);

  const BigInt a{n_left * n_right};

  ParallelComp(
      WIDTH * HEIGHT,
      [&](int64_t idxidx) {
        int idx = indices[idxidx];
        // array coordinates
        const int x = idx % WIDTH;
        const int y = idx / WIDTH;

        // Sampling powers of two might be too regular...
        // BigInt bl = BigInt::LeftShift(BigInt{1}, x);
        // BigInt br = BigInt::LeftShift(BigInt{1}, y);
        BigInt bl = BigInt{x} * 9997777;
        BigInt br = BigInt{y} * 9997777;

        // with n1*a^2 - b1^2 = 0,
        // n1*a^2 = b1^2
        // sqrt(n1)*a = sqrt(b)

        // BigInt al{BigInt::Sqrt(bl) / sqrtnleft};
        // BigInt ar{BigInt::Sqrt(br) / sqrtnright};

        // BigInt a = (al + ar) / 2;
        // if (a == 0) a = BigInt{1};


        BigInt kl = Error(n_left, a, bl);
        BigInt kr = Error(n_right, a, br);

        if (VERBOSE) {
          printf("%d,%d Run (" TERM_A "," TERM_B "," TERM_K
                 ") and (" TERM_A "," TERM_B "," TERM_K ").\n",
                 x, y,
                 a.ToString().c_str(),
                 bl.ToString().c_str(),
                 kl.ToString().c_str(),
                 a.ToString().c_str(),
                 br.ToString().c_str(),
                 kr.ToString().c_str());
        }
        fflush(stdout);

        int64_t iters = DualBhaskara(n_left, n_right,
                                     Triple(a, bl, kl),
                                     Triple(a, br, kr));

        // Takes at least STUCK_ITERS to end, so subtract that off.
        // int64_t its = iters - STUCK_ITERS;
        int64_t its = std::max(iters, 0LL);

        done++;
        if (VERBOSE) {
          printf("Finished %d,%d in %lld iters. Total done %lld\n",
                 x, y, iters, done.Read());
        }

        count[y * WIDTH + x] = its;
        bar_per.RunIf([&]() {
            printf(ANSI_UP
                   "%s\n",
                   ANSI::ProgressBar(done.Read(),
                                     WIDTH * HEIGHT,
                                     StringPrintf("(%d,%d) took %lld",
                                                  x, y, its),
                                     run_timer.Seconds()).c_str());
          });

        img_per.RunIf([&]() {
            SaveImage();
          });
      },
      NUM_THREADS);

  printf("Parallel comp done\n");

  SaveImage();
}

int main(int argc, char **argv) {
  ANSI::Init();
  printf("Start.\n");

  const BigInt n_left = 222121_b;
  const BigInt n_right = 360721_b;
  printf("Made nums: %s %s\n",
         n_left.ToString().c_str(),
         n_right.ToString().c_str()
         );

  MakeGrid(n_left, n_right);

  printf(AGREEN("OK") " :)\n");
  return 0;
}
