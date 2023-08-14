
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

static constexpr bool CLEAR_HISTORY = true;
static constexpr int RESTART_MULTIPLIER = 2;

#define TERM_A AFGCOLOR(39, 179, 214, "%s")
#define TERM_M AFGCOLOR(39, 214, 179, "%s")
#define TERM_B AFGCOLOR(232, 237, 173, "%s")
#define TERM_K AFGCOLOR(220, 173, 237, "%s")
#define TERM_N AFGCOLOR(227, 198, 143, "%s")
#define TERM_SQRTN AFGCOLOR(210, 200, 180, "%s")
#define TERM_E AFGCOLOR(200, 120, 140, "%s")

enum Method {
  GRADIENT_DESCENT,
  BLACK_BOX,
  GREEDY_9,
  RANDOM_WALK,
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

// This is the value we try to minimize on each step.
static BigInt MetricAbs(const Triple &left, const Triple &right) {
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

inline static BigInt operator *(int x, const BigInt &y) {
  // PERF
  return BigInt{x} * y;
}

static std::pair<BigInt, BigInt> MetricDerivatives(
    const BigInt &n1, const BigInt &n2,
    const BigInt &m1, const BigInt &m2,
    const Triple &left, const Triple &right,
    const BigInt &x1, const BigInt &x2) {

  const BigInt &a1 = left.a;
  const BigInt &b1 = left.b;
  const BigInt &k1 = left.k;

  const BigInt &a2 = right.a;
  const BigInt &b2 = right.b;
  const BigInt &k2 = right.k;

  // generated with algebra.cc (and wrapped by hand here).

  // d/dx1:
  const BigInt a1_e2 = a1 * a1;
  const BigInt a1_e3 = a1 * a1_e2;
  const BigInt a1_e4 = a1 * a1_e3;
  const BigInt a2_e2 = a2 * a2;
  const BigInt b1_e2 = b1 * b1;
  const BigInt b1_e3 = b1 * b1_e2;
  const BigInt b2_e2 = b2 * b2;
  const BigInt k1_e2 = k1 * k1;
  const BigInt k1_e3 = k1 * k1_e2;
  const BigInt k2_e2 = k2 * k2;
  const BigInt m1_e2 = m1 * m1;
  const BigInt m1_e3 = m1 * m1_e2;
  const BigInt m2_e2 = m2 * m2;
  const BigInt x1_e2 = x1 * x1;
  const BigInt x1_e3 = x1 * x1_e2;
  const BigInt x2_e2 = x2 * x2;
  // 34 summands
  BigInt ps_0 = (-8 * a1 * a2 * b1 * b2 * m2) / (k1 * k2_e2);
  BigInt ps_1 = (-8 * a1 * a2 * b1 * b2 * x2) / (k1 * k2);
  BigInt ps_2 = (-4 * a1 * a2_e2 * b1 * m2_e2) / (k1 * k2_e2);
  BigInt ps_3 = (-8 * a1 * a2_e2 * b1 * m2 * x2) / (k1 * k2);
  BigInt ps_4 = (-4 * a1 * a2_e2 * b1 * x2_e2) / k1;
  BigInt ps_5 = (-4 * a1 * b1 * b2_e2) / (k1 * k2_e2);
  BigInt ps_6 = (4 * a1 * b1_e3) / k1_e3;
  BigInt ps_7 = (-8 * a1_e2 * a2 * b2 * m1 * m2) / (k1 * k2_e2);
  BigInt ps_8 = (-8 * a1_e2 * a2 * b2 * m1 * x2) / (k1 * k2);
  BigInt ps_9 = (-8 * a1_e2 * a2 * b2 * m2 * x1) / k2_e2;
  BigInt ps_10 = (-8 * a1_e2 * a2 * b2 * x1 * x2) / k2;
  BigInt ps_11 = (-4 * a1_e2 * a2_e2 * m1 * m2_e2) / (k1 * k2_e2);
  BigInt ps_12 = (-8 * a1_e2 * a2_e2 * m1 * m2 * x2) / (k1 * k2);
  BigInt ps_13 = (-4 * a1_e2 * a2_e2 * m1 * x2_e2) / k1;
  BigInt ps_14 = (-4 * a1_e2 * a2_e2 * m2_e2 * x1) / k2_e2;
  BigInt ps_15 = (-8 * a1_e2 * a2_e2 * m2 * x1 * x2) / k2;
  BigInt ps_16 = -4 * a1_e2 * a2_e2 * x1 * x2_e2;
  BigInt ps_17 = (12 * a1_e2 * b1_e2 * m1) / k1_e3;
  BigInt ps_18 = (12 * a1_e2 * b1_e2 * x1) / k1_e2;
  BigInt ps_19 = (-4 * a1_e2 * b2_e2 * m1) / (k1 * k2_e2);
  BigInt ps_20 = (-4 * a1_e2 * b2_e2 * x1) / k2_e2;
  BigInt ps_21 = (12 * a1_e3 * b1 * m1_e2) / k1_e3;
  BigInt ps_22 = (24 * a1_e3 * b1 * m1 * x1) / k1_e2;
  BigInt ps_23 = (12 * a1_e3 * b1 * x1_e2) / k1;
  BigInt ps_24 = (4 * a1_e4 * m1_e3) / k1_e3;
  BigInt ps_25 = (12 * a1_e4 * m1_e2 * x1) / k1_e2;
  BigInt ps_26 = (12 * a1_e4 * m1 * x1_e2) / k1;
  BigInt ps_27 = 4 * a1_e4 * x1_e3;
  BigInt ps_28 = (-4 * m1 * n1) / k1;
  BigInt ps_29 = (4 * m1_e3) / k1;
  BigInt ps_30 = 12 * k1 * m1 * x1_e2;
  BigInt ps_31 = 4 * k1_e2 * x1_e3;
  BigInt ps_32 = 12 * m1_e2 * x1;
  BigInt ps_33 = -4 * n1 * x1;

  BigInt dx1 = ps_0 + ps_1 + ps_2 + ps_3 + ps_4 + ps_5 + ps_6 + ps_7 + ps_8 + ps_9 + ps_10 + ps_11 + ps_12 + ps_13 + ps_14 + ps_15 + ps_16 + ps_17 + ps_18 + ps_19 + ps_20 + ps_21 + ps_22 + ps_23 + ps_24 + ps_25 + ps_26 + ps_27 + ps_28 + ps_29 + ps_30 + ps_31 + ps_32 + ps_33;

  // d/dx2:
  // const BigInt a1_e2 = a1 * a1;
  // const BigInt a2_e2 = a2 * a2;
  const BigInt a2_e3 = a2 * a2_e2;
  const BigInt a2_e4 = a2 * a2_e3;
  // const BigInt b1_e2 = b1 * b1;
  // const BigInt b2_e2 = b2 * b2;
  const BigInt b2_e3 = b2 * b2_e2;
  // const BigInt k1_e2 = k1 * k1;
  // const BigInt k2_e2 = k2 * k2;
  const BigInt k2_e3 = k2 * k2_e2;
  // const BigInt m1_e2 = m1 * m1;
  // const BigInt m2_e2 = m2 * m2;
  const BigInt m2_e3 = m2 * m2_e2;
  // const BigInt x1_e2 = x1 * x1;
  // const BigInt x2_e2 = x2 * x2;
  const BigInt x2_e3 = x2 * x2_e2;
  // 34 summands
  BigInt ps2_0 = (-8 * a1 * a2 * b1 * b2 * m1) / (k1_e2 * k2);
  BigInt ps2_1 = (-8 * a1 * a2 * b1 * b2 * x1) / (k1 * k2);
  BigInt ps2_2 = (-8 * a1 * a2_e2 * b1 * m1 * m2) / (k1_e2 * k2);
  BigInt ps2_3 = (-8 * a1 * a2_e2 * b1 * m1 * x2) / k1_e2;
  BigInt ps2_4 = (-8 * a1 * a2_e2 * b1 * m2 * x1) / (k1 * k2);
  BigInt ps2_5 = (-8 * a1 * a2_e2 * b1 * x1 * x2) / k1;
  BigInt ps2_6 = (-4 * a1_e2 * a2 * b2 * m1_e2) / (k1_e2 * k2);
  BigInt ps2_7 = (-8 * a1_e2 * a2 * b2 * m1 * x1) / (k1 * k2);
  BigInt ps2_8 = (-4 * a1_e2 * a2 * b2 * x1_e2) / k2;
  BigInt ps2_9 = (-4 * a1_e2 * a2_e2 * m1_e2 * m2) / (k1_e2 * k2);
  BigInt ps2_10 = (-4 * a1_e2 * a2_e2 * m1_e2 * x2) / k1_e2;
  BigInt ps2_11 = (-8 * a1_e2 * a2_e2 * m1 * m2 * x1) / (k1 * k2);
  BigInt ps2_12 = (-8 * a1_e2 * a2_e2 * m1 * x1 * x2) / k1;
  BigInt ps2_13 = (-4 * a1_e2 * a2_e2 * m2 * x1_e2) / k2;
  BigInt ps2_14 = -4 * a1_e2 * a2_e2 * x1_e2 * x2;
  BigInt ps2_15 = (-4 * a2 * b1_e2 * b2) / (k1_e2 * k2);
  BigInt ps2_16 = (4 * a2 * b2_e3) / k2_e3;
  BigInt ps2_17 = (-4 * a2_e2 * b1_e2 * m2) / (k1_e2 * k2);
  BigInt ps2_18 = (-4 * a2_e2 * b1_e2 * x2) / k1_e2;
  BigInt ps2_19 = (12 * a2_e2 * b2_e2 * m2) / k2_e3;
  BigInt ps2_20 = (12 * a2_e2 * b2_e2 * x2) / k2_e2;
  BigInt ps2_21 = (12 * a2_e3 * b2 * m2_e2) / k2_e3;
  BigInt ps2_22 = (24 * a2_e3 * b2 * m2 * x2) / k2_e2;
  BigInt ps2_23 = (12 * a2_e3 * b2 * x2_e2) / k2;
  BigInt ps2_24 = (4 * a2_e4 * m2_e3) / k2_e3;
  BigInt ps2_25 = (12 * a2_e4 * m2_e2 * x2) / k2_e2;
  BigInt ps2_26 = (12 * a2_e4 * m2 * x2_e2) / k2;
  BigInt ps2_27 = 4 * a2_e4 * x2_e3;
  BigInt ps2_28 = (-4 * m2 * n2) / k2;
  BigInt ps2_29 = (4 * m2_e3) / k2;
  BigInt ps2_30 = 12 * k2 * m2 * x2_e2;
  BigInt ps2_31 = 4 * k2_e2 * x2_e3;
  BigInt ps2_32 = 12 * m2_e2 * x2;
  BigInt ps2_33 = -4 * n2 * x2;

  BigInt dx2 = ps2_0 + ps2_1 + ps2_2 + ps2_3 + ps2_4 + ps2_5 + ps2_6 + ps2_7 + ps2_8 + ps2_9 + ps2_10 + ps2_11 + ps2_12 + ps2_13 + ps2_14 + ps2_15 + ps2_16 + ps2_17 + ps2_18 + ps2_19 + ps2_20 + ps2_21 + ps2_22 + ps2_23 + ps2_24 + ps2_25 + ps2_26 + ps2_27 + ps2_28 + ps2_29 + ps2_30 + ps2_31 + ps2_32 + ps2_33;

  return make_pair(dx1, dx2);
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

// Given two equations of the form
//     na^2 + k = b^2
// where (a, b, k) is the "triple",
// Try to find two triples t1, t2 where t1.a=t2.a and t1.k = t2.k = 1.
static std::pair<Triple, Triple>
DualBhaskara(BigInt nleft, BigInt nright, Triple left, Triple right) {
  ArcFour rc("dualbhaskara");

  BigInt sqrtnleft = BigInt::Sqrt(nleft);
  BigInt sqrtnright = BigInt::Sqrt(nright);

  if (VERBOSE)
    printf(AWHITE(ABGCOLOR(0, 0, 50, "---------- on %s,%s ------------")) "\n",
           nleft.ToString().c_str(),
           nright.ToString().c_str());

  Periodically image_per(10.0 * 60.0);
  // run after the first minute for quick feedback on experiments
  image_per.SetPeriodOnce(60.0);
  int image_idx = 0;

  int last_restart = 0;
  int restarts = 0;
  BigInt restart_multiplier{1};

  Periodically bar_per(1.0);
  bool first_progress = true;
  const BigInt start_metric = Metric(left, right);
  const int start_metric_size = start_metric.ToString().size();

  Timer timer;

  // Pair of triples (left, right) that we've already tried. We avoid
  // exploring them a second time, as this would lead to a cycle.
  TriplePairSet seen;

  std::vector<std::pair<Triple, Triple>> history;

  std::optional<BigInt> recent_best_score;
  int recent_best_iter = 0;

  int64_t last_compass_count = 1;
  std::pair<int, int> last_compass_dir = make_pair(-999, 999);

  BigInt best_total_k{1000};

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
                     &last_repeats, &last_attempts, &restarts,
                     &recent_best_score, &recent_best_iter, &a_diff,
                     &last_compass_dir, &last_compass_count, &last_restart,
                     &timer, &iters, &left, &right]() {
        if (first_progress) {
          printf("\n");
          first_progress = false;
        }
        BigInt cur_metric = Metric(left, right);
        BigInt metric_diff = start_metric - cur_metric;
        if (metric_diff < BigInt{0}) metric_diff = BigInt{0};

        // int digits_total = start_metric_size;
        // int digits_done = metric_diff.ToString().size();

        int max_b_digits = std::max(left.b.ToString().size(),
                                    right.b.ToString().size());
        int a_diff_digits = BigInt::Abs(a_diff).ToString().size();
        int k_digits =
          (BigInt::Abs(left.k) + BigInt::Abs(right.k)).ToString().size();

        static constexpr int NUM_LINES = 3;
        for (int i = 0; i < NUM_LINES; i++) {
          printf(ANSI_PREVLINE
                 ANSI_CLEARLINE);
        }
        printf("%d its, dig: ks: %d diff: %d max-b: %d"
               " rep: %d/%d restarts %d\n",
               iters, k_digits,
               a_diff_digits,
               max_b_digits,
               last_repeats, last_attempts,
               restarts);
        double ips = iters / timer.Seconds();
        printf("Took %s (%.2fit/s)\n",
               ANSI::Time(timer.Seconds()).c_str(),
               ips);

        printf("(%d,%d)" AGREY("x") "%lld " AGREEN("%d") " rbs_iter: %d score: %s\n",
               last_compass_dir.first,
               last_compass_dir.second,
               last_compass_count,
               iters - last_restart,
               recent_best_iter,
               recent_best_score.has_value() ?
               recent_best_score.value().ToString().c_str() : "?");
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
                img.BlendRect32(sx - 1, sy - 1, 3, 3, color);
                break;
              }
            };

          Plot(t1.a, t2.a, 0xFF333355, DISC);
          Plot(t1.b, t2.b, 0x77FF7755, CIRCLE);
          Plot(t1.k, t2.k, 0x5555FF55, FILLSQUARE);
        }

        const int textx = 32;
        int texty = 32;
        img.BlendText2x32(textx + 18, texty, 0x555555FF,
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
                img.BlendRect32(sx - 1, sy - 1, 3, 3, color);
                break;
              }
            };

          const auto &[t1, t2] = history[x];
          if (t1.a > max_a) max_a = t1.a;
          if (t1.b > max_b) max_b = t1.b;
          if (BigInt::Abs(t1.k) > max_k) max_k = BigInt::Abs(t1.k);
          Plot(t1.a, 0xFF0000, CIRCLE);
          Plot(t1.b, 0xFF3300, CIRCLE);
          Plot(t1.k, 0xFF7700, CIRCLE);

          if (t2.a > max_a) max_a = t2.a;
          if (t2.b > max_b) max_b = t2.b;
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

      if (CLEAR_HISTORY) {
        history.clear();
      }
    }

    // Are we done?
    if (a_diff == 0) {
      BigInt ak1 = BigInt::Abs(left.k);
      BigInt ak2 = BigInt::Abs(right.k);
      BigInt total_k = ak1 + ak2;
      if (total_k < best_total_k) {
        printf("\n\n\nNew best k1/k2: %s\n\n\n",
               total_k.ToString().c_str());
        FILE *f = fopen("bestk.txt", "ab");
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
        best_total_k = total_k;
      }
      if (ak1 == 1 && ak2 == 1) {
        printf(AGREEN("DONE!") " in " AWHITE("%d") " iters. Took %s.\n",
               iters, ANSI::Time(timer.Seconds()).c_str());
        return make_pair(left, right);
      }
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
        if (gcd != 1) {
          if (CHECK_INVARIANTS) {
            CHECK(triple.a % gcd == 0);
            CHECK(triple.b % gcd == 0);
          }
          triple.a = triple.a / gcd;
          triple.b = triple.b / gcd;

          triple.k = Error(n, triple.a, triple.b);

          // I just set it!
          // if (CHECK_INVARIANTS) {
          // CHECK(Error(n, triple.a, triple.b) == triple.k);
          // }
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

        BigInt a = BigInt::RandTo(rr, nleft * nright * restart_multiplier);
        BigInt bl = BigInt::RandTo(rr, nleft * nleft * restart_multiplier);
        BigInt br = BigInt::RandTo(rr, nright * nright * restart_multiplier);

        restart_multiplier = restart_multiplier * BigInt{RESTART_MULTIPLIER};

        BigInt kl = Error(nleft, a, bl);
        BigInt kr = Error(nright, a, br);

        new_left = Triple(a, bl, kl);
        new_right = Triple(a, br, kr);

        printf(
            "Restart with\n"
            "(" TERM_A "," TERM_B "," TERM_K ")\nand\n "
            "(" TERM_A "," TERM_B "," TERM_K ")\n",
            a.ToString().c_str(), bl.ToString().c_str(), kl.ToString().c_str(),
            a.ToString().c_str(), br.ToString().c_str(), kr.ToString().c_str());
        restarts++;
        last_restart = iters;

        if (CLEAR_HISTORY) {
          seen.clear();
        }

        // reset the counter
        recent_best_score = nullopt;
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
