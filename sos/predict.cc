
#include "predict.h"

#include <unordered_set>
#include <utility>
#include <vector>
#include <cstdint>

#include "opt/opt.h"
#include "sos-util.h"
#include "ansi.h"

using namespace std;

static constexpr bool VERBOSE_FIT = false;

std::pair<double, double>
Predict::SquaredModel(const std::vector<int64_t> &ys) {
  std::vector<double> yd;
  yd.reserve(ys.size() + 1);
  // Require zero intercept.
  yd.push_back(0.0);
  // XXX precision issues? Clearly not all 64-bit ints can
  // be represented as doubles, although we're not THAT high
  // yet...
  for (int64_t y : ys) yd.push_back(std::sqrt(y));

  /*
  printf("Fit:\n");
  for (int x = 0; x < yd.size(); x++) {
    printf("f(%d) = %f\n", x, yd[x]);
  }
  */

  // Now just a linear fit (least squares)
  const auto &[c, score] =
    Opt::Minimize1D([&yd](double c) {
        double score = 0;
        for (int x = 0; x < yd.size(); x++) {
          double ay = yd[x];
          double py = c * x;
          double d = (ay - py);
          score += d * d;
        }
        // printf("With c=%f score=%f\n", c, score);
        return score;
      },
    // -1.0e30, +1.0e30,
    -250000.0, 250000.0,
    10000,
    1,
    100);

  if (VERBOSE_FIT) {
    printf("Fit:\n");
    double prev = 0.0;
    for (int x = 0; x < yd.size(); x++) {
      double err = (c * x) - yd[x];
      printf("f(%d) = %f (delta %f) (err %s%f" ANSI_RESET ")\n",
             x, yd[x], yd[x] - prev,
             err > 0.0 ? ANSI_FG(160, 200, 160) : ANSI_FG(200, 160, 160),
             err);
      prev = yd[x];
    }
  }

  return make_pair(c, score);
}

// If not dense, we could use points from consecutive regions by
// fitting lines to them individually, then doing a weighted average.
// But the main use is the herrors, which we generally have a dense
// prefix of anyway.

std::pair<double, double> Predict::DenseZeroesModel(
    const std::vector<int64_t> &xs) {
  return SquaredModel(xs);
}

std::pair<double, double> Predict::DensePrefixZeroesModel(
    const Database &db,
    const std::vector<int64_t> &xs) {
  auto span = db.Done().GetPoint(0);
  CHECK(span.data);
  uint64_t limit = span.end;
  std::vector<int64_t> dense_xs;
  for (int64_t x : xs) {
    if (x >= limit) break;
    dense_xs.push_back(x);
  }
  return DenseZeroesModel(dense_xs);
}

int64_t Predict::NextInDenseSeries(const std::vector<int64_t> &zeroes) {
  const auto &[c, score] = DenseZeroesModel(zeroes);

  printf("y = (" ACYAN("%.9f") " * x)^2\n", c);
  printf("Error: %.9f\n", score);

  double r = c * (zeroes.size() + 1);
  return r * r;
}

// Basically we want to use the dense prefix to predict the curve,
// then sample it, but if the sample falls in a region that already
// has a zero, go to the next one. Actually I guess we could use the
// passed-in zeroes?
int64_t Predict::NextInDensePrefixSeries(
    const Database &db,
    const std::vector<int64_t> &zeroes) {
  // Use the dense prefix to predict the curve.
  const auto &[c, score] = DensePrefixZeroesModel(db, zeroes);

  // Intervals that have a zero.
  std::unordered_set<int64_t> starts;
  for (int64_t x : zeroes) {
    const auto span = db.Done().GetPoint(x);
    // Expect this to be done, but if it isn't, it's better to
    // just ignore it.
    if (span.data) starts.insert(span.start);
  }

  printf("y = (" ACYAN("%.9f") " * x)^2\n", c);
  printf("Error: %.9f\n", score);

  // Now predict from the series. This must terminate because we have
  // a finite set of zeroes.
  for (int x = 1; true; x++) {
    double r = c * x;
    double z = r * r;
    // If we've already seen all the zeroes, then it will certainly
    // be a new prediction. (But do we even need this test?)
    if (x > zeroes.size())
      return z;

    // Did we already find this one? We could be more exact about it,
    // but we assume that we have if the prediction falls in an interval
    // that has a zero.
    int64_t start = db.Done().GetPoint(z).start;
    if (starts.find(start) == starts.end())
      return z;
  }
}

std::vector<std::pair<int64_t, double>>
Predict::FutureCloseCalls(
    const Database &db,
    const std::vector<int64_t> &azeroes,
    const std::vector<int64_t> &hzeroes,
    uint64_t max_inner_sum) {

  // Use the dense prefix to predict the curve.
  const auto &[ca, scorea] = DensePrefixZeroesModel(db, azeroes);
  printf("ya = (" ACYAN("%.9f") " * x)^2\n", ca);
  printf("(Error: %.9f)\n", scorea);

  const auto &[ch, scoreh] = DensePrefixZeroesModel(db, hzeroes);
  printf("yh = (" ACYAN("%.9f") " * x)^2\n", ch);
  printf("(Error: %.9f)\n", scoreh);

  std::vector<std::pair<int64_t, double>> out;

  // Now we have two series. Whenever pairs are close, we want to
  // consider that candidate. Since we know that h grows much faster
  // than a, it suffices to just look at elements of the h series and
  // take the closest from the a series.

  int64_t xa = 0;
  for (int64_t xh = 0; true; xh++) {
    double rh = ch * xh;
    double zh = rh * rh;

    double best_dist = zh;
    double best = 0.0;
    for (;; xa++) {
      double ra = ca * xa;
      double za = ra * ra;

      // The closest can only be the one immediately before
      // or after, but it's simplest to just test each as
      // we go.
      double dist = std::abs(za - zh);
      if (dist < best_dist) {
        best_dist = dist;
        best = za;
      }

      // .. including the one after.
      if (za > zh) {
        // Could in principle be closest to the *next*
        // one, so include it for the next round.
        xa--;
        break;
      }
    }

    out.emplace_back((int64_t)std::round(best), best_dist);

    if (zh > max_inner_sum) {
      break;
    }
  }

  // Sort by distance ascending.
  std::sort(out.begin(), out.end(),
            [](const std::pair<int64_t, double> &a,
               const std::pair<int64_t, double> &b) {
              return a.second < b.second;
            });

  return out;
}

