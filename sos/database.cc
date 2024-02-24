
#include "database.h"

#include <string>
#include <cstdint>
#include <array>
#include <vector>

#include "base/logging.h"
#include "base/stringprintf.h"

#include "numbers.h"
#include "ansi.h"
#include "util.h"
#include "interval-cover.h"
#include "re2/re2.h"
#include "vector-util.h"
#include "predict.h"

using namespace std;
using Square = Database::Square;

using ZeroVecFn = std::function<void(int64_t, int64_t,
                                     int64_t, int64_t,
                                     int64_t)>;
void Database::ForEveryZeroVec(
    const ZeroVecFn &f_a,
    const ZeroVecFn &f_h) const {

  auto VisitZeroes =
    [this]<size_t RADIX>(const ZeroVecFn &f,
                         int idx,
                         int64_t inner_sum,
                         int64_t err,
                         bool positive_slope,
                         std::array<std::pair<int64_t, int64_t>, RADIX> *prev) {
    const auto [prev_x, prev_y] = (*prev)[idx % RADIX];
    const int64_t cur_x = inner_sum;
    const int64_t cur_y = err;
    const bool correct_slope =
    positive_slope ? (prev_y < 0 && cur_y > 0) : (prev_y > 0 && cur_y < 0);
    if (prev_x != 0 && correct_slope) {
      double dx = cur_x - prev_x;
      double dy = cur_y - prev_y;
      double m = dy / dx;
      int64_t iceptx = prev_x + std::round(-prev_y / m);
      CHECK(iceptx >= prev_x) << prev_x << "," << prev_y << " -> "
                              << cur_x << "," << cur_y << " slope "
                              << m << " @ "
                              << iceptx;
      CHECK(iceptx <= cur_x);

      if (CompleteBetween(prev_x, cur_x)) {
        f(prev_x, prev_y, cur_x, cur_y, iceptx);
      }
    }
    (*prev)[idx % RADIX] = std::make_pair(cur_x, cur_y);
  };


  static constexpr size_t H_RADIX = 5;
  std::array<std::pair<int64_t, int64_t>, H_RADIX> prev_h;
  std::fill(prev_h.begin(), prev_h.end(), make_pair(0LL, 0LL));
  static constexpr size_t A_RADIX = 10;
  std::array<std::pair<int64_t, int64_t>, A_RADIX> prev_a;
  std::fill(prev_a.begin(), prev_a.end(), make_pair(0LL, 0LL));

  int idx = 0;
  for (const auto &[inner_sum, square] : almost2) {
    const auto [aa, bb, cc, dd, ee, ff, gg, hh, ii] = square;
    uint64_t a = Sqrt64(aa);
    uint64_t h = Sqrt64(hh);

    CHECK(inner_sum == bb + cc);

    int64_t herr1 = h * h - (int64_t)hh;
    int64_t herr2 = (h + 1) * (h + 1) - (int64_t)hh;

    int64_t aerr1 = a * a - (int64_t)aa;
    int64_t aerr2 = (a + 1) * (a + 1) - (int64_t)aa;

    int64_t herr = std::abs(herr1) < std::abs(herr2) ? herr1 : herr2;
    int64_t aerr = std::abs(aerr1) < std::abs(aerr2) ? aerr1 : aerr2;

    VisitZeroes.template operator()<A_RADIX>(
        f_a, idx, inner_sum, aerr, true, &prev_a);
    VisitZeroes.template operator()<H_RADIX>(
        f_h, idx, inner_sum, herr, false, &prev_h);
    idx++;
  }
}


std::pair<std::vector<int64_t>,
          std::vector<int64_t>> Database::GetZeroes() {
  // Intercepts where we actually have the point before and after.
  std::vector<int64_t> azeroes, hzeroes;
  ForEveryZeroVec([&azeroes](int64_t x0_, int64_t y0_,
                             int64_t x1_, int64_t y1_,
                             int64_t z) { azeroes.push_back(z); },
                  [&hzeroes](int64_t x0_, int64_t y0_,
                             int64_t x1_, int64_t y1_,
                             int64_t z) { hzeroes.push_back(z); });
  return make_pair(azeroes, hzeroes);
}

void Database::AddEpoch(uint64_t start, uint64_t size) {
  // printf("AddEpoch %llu, %llu\n", start, start + size);
  done.SetSpan(start, start + size, true);
}

void Database::AddAlmost2(const std::array<uint64_t, 9> &square) {
  const auto [aa, bb, cc, dd, ee, ff, gg, hh, ii] = square;
  const uint64_t sum = aa + bb + cc;
  CHECK(sum == dd + ee + ff);
  CHECK(sum == gg + hh + ii);
  CHECK(sum == aa + dd + gg);
  CHECK(sum == bb + ee + hh);
  CHECK(sum == cc + ff + ii);
  CHECK(sum == aa + ee + ii);
  CHECK(sum == gg + ee + cc);

  [[maybe_unused]]
  uint64_t a = Sqrt64(aa);
  uint64_t b = Sqrt64(bb);
  uint64_t c = Sqrt64(cc);
  uint64_t d = Sqrt64(dd);
  uint64_t e = Sqrt64(ee);
  uint64_t f = Sqrt64(ff);
  uint64_t g = Sqrt64(gg);
  [[maybe_unused]]
  uint64_t h = Sqrt64(hh);
  uint64_t i = Sqrt64(ii);

  // Invariant for almost2-type squares.
  // (Note: We now output other chiralities if we find them, like
  // where h is a square but f is not. So these assertions fail,
  // then we need to store those as another type, or allow and
  // distinguish them here, or something like that.)
  CHECK(b * b == bb);
  CHECK(c * c == cc);
  CHECK(d * d == dd);
  CHECK(e * e == ee);
  CHECK(f * f == ff);
  CHECK(g * g == gg);
  CHECK(i * i == ii);

  [[maybe_unused]]
  uint64_t inner_sum = bb + cc;

  // TODO: Do something with it.
  // This could be possible? But usually this happens because a
  // process was interrupted after finding squares, which are then
  // re-found when it is started again.
  CHECK(almost2.find(inner_sum) == almost2.end()) << "Unexpected: "
    "two squares with the same inner sum (inner: " << inner_sum << ")"
    " (full: " << sum << ")";
  almost2[inner_sum] = square;
}

Database Database::FromInterestingFile(const std::string &filename) {
  Database db;
  RE2 almost2_re("\\(!\\) "
                 "(\\d+) (\\d+) (\\d+) "
                 "(\\d+) (\\d+) (\\d+) "
                 "(\\d+) (\\d+) (\\d+)");

  // At various times in the past there were more/different stats here,
  // so we just require the prefix to match.
  // EPOCH 15000000000 1000000000 42305845 ...
  RE2 epoch_re("EPOCH (\\d+) (\\d+).*");
  std::vector<std::string> lines = Util::ReadFileToLines("interesting.txt");

  for (const std::string &line : lines) {
    uint64_t aa, bb, cc, dd, ee, ff, gg, hh, ii;
    uint64_t start, size;
    if (RE2::FullMatch(line, almost2_re,
                       &aa, &bb, &cc, &dd, &ee, &ff, &gg, &hh, &ii)) {
      std::array<uint64_t, 9> square =
        std::array{aa, bb, cc, dd, ee, ff, gg, hh, ii};
      db.AddAlmost2(square);
    } else if (RE2::FullMatch(line, epoch_re, &start, &size)) {
      db.AddEpoch(start, size);
    }
  }

  return db;
}

string Database::Epochs() const {
  string ret;
  for (uint64_t pt = done.First();
       !done.IsAfterLast(pt);
       pt = done.Next(pt)) {
    IntervalCover<bool>::Span span = done.GetPoint(pt);

    StringAppendF(&ret, "[%llu, %llu) %s\n",
                  span.start, span.end,
                  span.data ? AGREEN("DONE") : AGREY("NO"));
  }
  return ret;
}

std::pair<uint64_t, uint64_t> Database::NextGapAfter(uint64_t start,
                                                     uint64_t max_size) const {
  for (uint64_t pt = start;
       !done.IsAfterLast(pt);
       pt = done.Next(pt)) {
    IntervalCover<bool>::Span span = done.GetPoint(pt);
    if (!span.data) {
      // If this is the first one, it may be inside the interval.
      uint64_t actual_start = std::max(pt, span.start);
      uint64_t actual_end = span.end;
      CHECK(actual_end > actual_start);
      uint64_t size = actual_end - actual_start;
      uint64_t msize = std::min(max_size, size);

      return make_pair(actual_start, msize);
    }
  }

  CHECK(false) << "Exhausted 64-bit ints?!";
}

std::optional<std::pair<uint64_t, uint64_t>>
Database::NextGapBefore(uint64_t x,
                        uint64_t max_size) const {

  for (uint64_t pt = done.GetPoint(x).start;
       pt != 0;
       pt = done.Prev(pt)) {
    IntervalCover<bool>::Span span = done.GetPoint(pt);
    if (!span.data) {

      // If the first one, this may be inside the interval.
      uint64_t actual_end = std::min(x, span.end);
      CHECK(span.start < actual_end);

      uint64_t actual_size = actual_end - span.start;
      if (actual_size < max_size) {
        // Entire interval.
        return {make_pair(span.start, actual_size)};
      }

      // Otherwise, take the end of the interval.
      uint64_t start = actual_end - max_size;
      CHECK(start >= span.start);
      return {make_pair(start, max_size)};
    }
  }

  return nullopt;
}


std::pair<uint64_t, uint64_t> Database::NextToDo(uint64_t max_size) const {
  return NextGapAfter(0, max_size);
}

std::vector<std::pair<uint64_t, Square>> Database::LastN(int n) const {
  std::vector<std::pair<uint64_t, Square>> ret;
  ret.reserve(n);

  for (auto it = almost2.rbegin(); it != almost2.rend(); ++it) {
    ret.emplace_back(it->first, it->second);
    if (ret.size() == n) break;
  }

  VectorReverse(&ret);
  return ret;
}

bool Database::CompleteBetween(uint64_t a, uint64_t b) const {
  // It's only exhaustive if these are the same span (otherwise there
  // would be some non-empty span between them that has not been
  // completed) and it's completed.

  IntervalCover<bool>::Span span_a = done.GetPoint(a);
  IntervalCover<bool>::Span span_b = done.GetPoint(b);
  return span_a.start == span_b.start &&
    span_a.end == span_b.end &&
    span_a.data;
}

bool Database::IsComplete(uint64_t a) const {
  return done.GetPoint(a).data;
}

int64_t Database::GetHerr(const Square &square) {
  const auto &[aa, bb, cc, dd, ee, ff, gg, hh, ii] = square;
  uint64_t h = Sqrt64(hh);

  // int64_t inner_sum = bb + cc;

  // How close are we to a square? The computed sqrt is a lower bound, so
  // it could actually be closer to the next square.
  int64_t herr1 = h * h - (int64_t)hh;
  int64_t herr2 = (h + 1) * (h + 1) - (int64_t)hh;
  int64_t herr = std::abs(herr1) < std::abs(herr2) ? herr1 : herr2;
  return herr;
}

int64_t Database::GetAerr(const Square &square) {
  const auto &[aa, bb, cc, dd, ee, ff, gg, hh, ii] = square;
  uint64_t a = Sqrt64(aa);

  int64_t aerr1 = a * a - (int64_t)aa;
  int64_t aerr2 = (a + 1) * (a + 1) - (int64_t)aa;
  int64_t aerr = std::abs(aerr1) < std::abs(aerr2) ? aerr1 : aerr2;
  return aerr;
}

// f(x0, y0, x1, y1, iceptx);
void Database::ForEveryHVec(uint64_t pt,
                            const std::function<void(int64_t, int64_t,
                                                     int64_t, int64_t,
                                                     int64_t)> &f) {
  // Look for all the squares in this span.
  auto span = done.GetPoint(pt);
  if (!span.data) return;

  auto it = almost2.lower_bound(span.start);

  auto it5 = it;
  for (int i = 0; i < 5; i++) {
    if (it5 == almost2.end()) {
      // Not enough squares left!
      return;
    }
    it5 = std::next(it5);
  }

  while (it5 != almost2.end()) {
    // Also if we run off the span.
    if (it5->first >= span.end)
      return;

    // So we have one vec. Compute and call.
    const int64_t x0 = it->first;
    const int64_t x1 = it5->first;
    const int64_t y0 = Database::GetHerr(it->second);
    const int64_t y1 = Database::GetHerr(it5->second);

    // Predicted intercept.
    const double dx = x1 - x0;
    const double dy = y1 - y0;
    const double m = dy / dx;
    const int64_t iceptx = x0 + std::round(-y0 / m);

    f(x0, y0, x1, y1, iceptx);

    ++it;
    ++it5;
  }
}

// f(x0, y0, x1, y1, iceptx);
void Database::ForEveryAVec(uint64_t pt,
                            const std::function<void(int64_t, int64_t,
                                                     int64_t, int64_t,
                                                     int64_t)> &f) {
  // Look for all the squares in this span.
  auto span = done.GetPoint(pt);
  if (!span.data) return;

  auto it = almost2.lower_bound(span.start);

  auto it10 = it;
  for (int i = 0; i < 10; i++) {
    if (it10 == almost2.end()) {
      // Not enough squares left!
      return;
    }
    it10 = std::next(it10);
  }

  while (it10 != almost2.end()) {
    // Also if we run off the span.
    if (it10->first >= span.end)
      return;

    // So we have one vec. Compute and call.
    const int64_t x0 = it->first;
    const int64_t x1 = it10->first;
    const int64_t y0 = GetAerr(it->second);
    const int64_t y1 = GetAerr(it10->second);

    // Predicted intercept.
    const double dx = x1 - x0;
    const double dy = y1 - y0;
    const double m = dy / dx;
    const int64_t iceptx = x0 + std::round(-y0 / m);

    f(x0, y0, x1, y1, iceptx);

    ++it;
    ++it10;
  }
}


Database::IslandZero Database::IslandHZero(int64_t pt) {
  if (!done.GetPoint(pt).data) return IslandZero::NONE;

  bool really_done = false;
  // Prefer extending to the left until we have some evidence, since
  // this seems to usually be an overestimate.
  bool extend_right = false;
  int64_t closest_dist = std::numeric_limits<int64_t>::max();
  bool has_points = false;

  ForEveryHVec(pt,
               [&](int64_t x0, int64_t y0,
                   int64_t x1, int64_t y1,
                   int64_t iceptx) {
                 has_points = true;
                 if (y0 > 0 && y1 < 0) {
                   really_done = true;
                 } else {
                   if (y1 > 0) {
                     // above the axis, so we'd
                     // extend to the right to find zero.
                     if (y1 < closest_dist) {
                       extend_right = true;
                       closest_dist = y1;
                     }
                   } else {
                     CHECK(y0 < 0);
                     if (-y0 < closest_dist) {
                       extend_right = false;
                       closest_dist = -y0;
                     }
                   }
                 }
               });

  if (really_done) return IslandZero::HAS_ZERO;
  else if (!has_points) return IslandZero::NO_POINTS;

  return extend_right ? IslandZero::GO_RIGHT : IslandZero::GO_LEFT;
}

Database::IslandZero Database::IslandAZero(int64_t pt) {
  if (!done.GetPoint(pt).data) return IslandZero::NONE;

  bool really_done = false;
  // Prefer extending to the left until we have some evidence, since
  // this seems to usually be an overestimate.
  bool extend_right = false;
  int64_t closest_dist = std::numeric_limits<int64_t>::max();
  bool has_points = false;

  ForEveryAVec(pt,
               [&](int64_t x0, int64_t y0,
                   int64_t x1, int64_t y1,
                   int64_t iceptx) {
                 has_points = true;
                 if (y0 < 0 && y1 > 0) {
                   really_done = true;
                 } else {
                   if (y1 < 0) {
                     // above the axis, so we'd
                     // extend to the right to find zero.
                     if (-y1 < closest_dist) {
                       extend_right = true;
                       closest_dist = -y1;
                     }
                   } else {
                     CHECK(y0 > 0);
                     if (y0 < closest_dist) {
                       extend_right = false;
                       closest_dist = y0;
                     }
                   }
                 }
               });

  if (really_done) return IslandZero::HAS_ZERO;
  else if (!has_points) return IslandZero::NO_POINTS;

  return extend_right ? IslandZero::GO_RIGHT : IslandZero::GO_LEFT;
}


static void PrintGaps(const Database &db) {
  int64_t max_agap = 0, max_hgap = 0;
  db.ForEveryZeroVec(
      [&max_agap](int64_t x0, int64_t y0,
                  int64_t x1, int64_t y1,
                  int64_t iceptx) {
        max_agap = std::max(max_agap, x1 - x0);
      },
      [&max_hgap](int64_t x0, int64_t y0,
                  int64_t x1, int64_t y1,
                  int64_t iceptx) {
        max_hgap = std::max(max_hgap, x1 - x0);
      });

  printf("Max agap: %lld.\n"
         "Max hgap: %lld.\n",
         max_agap, max_hgap);
}

std::pair<uint64_t, uint64_t> Database::PredictNextNewton(
    uint64_t max_epoch_size) {
  // This uses the (as yet unexplained) observation that every fifth
  // error on the h term follows a nice curve which is nearly linear.
  // Use these to predict zeroes and explore regions near the predicted
  // zeroes that haven't yet been explored.

  // To predict zeroes, we need 6 *consecutive* almost2 squares.
  // We should get smarter about this, as we'd always rather explore
  // smaller numbers. But for now we just look at the last ones that
  // were processed, since the database is dense as of starting this
  // idea out.

  // PERF not all of them!
  printf("db ranges:\n%s\n", Epochs().c_str());

  // XXX improve heuristics so I don't need to keep increasing this
  static constexpr int64_t INTERCEPT_LB = 8'000'000'000'000;

  const auto &almost2 = Almost2();
  auto it = almost2.begin();
  while (it->first < INTERCEPT_LB) ++it;

  // Iterator ahead by five.
  auto it5 = it;
  for (int i = 0; i < 5; i++) {
    if (it5 == almost2.end()) {
      printf("Don't even have 5 squares yet?\n");
      return NextToDo(max_epoch_size);
    }
    it5 = std::next(it5);
  }

  int64_t best_score = 99999999999999;
  int64_t best_start = 0;
  uint64_t best_size = 0;
  auto Consider = [&best_score, &best_start, &best_size](
      int64_t score, int64_t start, uint64_t size) {
      printf("Consider score " ACYAN("%lld") " @" APURPLE("%lld") "\n",
             score, start);
      if (score < best_score) {
        best_score = score;
        best_start = start;
        best_size = size;
      }
    };

  while (it5 != almost2.end()) {
    const int64_t x0 = it->first;
    const int64_t x1 = it5->first;

    // Must be dense here or else we don't know if these are on
    // the same arc.
    if (CompleteBetween(x0, x1)) {
      const int64_t y0 = Database::GetHerr(it->second);
      const int64_t y1 = Database::GetHerr(it5->second);

      // The closer we are to the axis, the better the
      // prediction will be. This also keeps us from going
      // way out on the number line.
      // (Another option would be to just prefer lower x.)
      const int64_t dist = std::min(std::abs(y0), std::abs(y1));

      // Predicted intercept.
      double dx = x1 - x0;
      double dy = y1 - y0;
      double m = dy / dx;
      int64_t iceptx = x0 + std::round(-y0 / m);

      auto PrVec = [&](const char *msg) {
          if (dist < 200000) {
            printf("(%lld, %lld) -> (%lld, %lld)\n"
                   "  slope %.8f icept "
                   AWHITE("%lld") " score " ACYAN("%lld") " %s\n",
                   x0, y0, x1, y1, m, iceptx, dist, msg);
          }
        };

      // Consider the appropriate action on the island unless it's
      // already done.
      using enum Database::IslandZero;
      if (iceptx > 0) {
        switch (IslandHZero(iceptx)) {
        case NONE: {
          // If none, explore that point.

          // Round to avoid fragmentation.
          int64_t c = iceptx;
          c /= max_epoch_size;
          c *= max_epoch_size;

          Consider(dist, c, max_epoch_size);
          PrVec("NEW");
          break;
        }
        case HAS_ZERO:
          PrVec("REALLY DONE");
          break;

        case NO_POINTS:
        case GO_LEFT: {
          auto go = NextGapBefore(iceptx, max_epoch_size);
          if (go.has_value()) {
            Consider(dist, go.value().first, go.value().second);
            PrVec("BEFORE");
          }
          break;
        }

        case GO_RIGHT: {
          auto nga = NextGapAfter(iceptx, max_epoch_size);
          Consider(dist, nga.first, nga.second);
          PrVec("AFTER");
          break;
        }
        }
      }
    }

    ++it;
    ++it5;
  }

  if (best_start <= 0 || best_size == 0) {
    printf("\n" ARED("No valid intercepts?") "\n");
    return NextToDo(max_epoch_size);

  } else {
    printf("\nNext guess: " APURPLE("%llu") " +" ACYAN("%llu") "\n",
           best_start, best_size);

    CHECK(best_size <= max_epoch_size);

    // The above is supposed to produce an interval that still
    // remains to be done, but skip to the next if not.
    return NextGapAfter(best_start, best_size);
  }
}

std::pair<uint64_t, uint64_t> Database::PredictNextRegression(
    uint64_t max_epoch_size,
    bool predict_h) {
  PrintGaps(*this);

  printf("Get zero for " AWHITE("%s") " using regression...\n",
         predict_h ? "H" : "A");
  const auto &[azeroes, hzeroes] = GetZeroes();
  const int64_t iceptx = predict_h ?
    Predict::NextInDenseSeries(hzeroes) :
    Predict::NextInDensePrefixSeries(*this, azeroes);
  printf("Predict next zero at: " ABLUE("%lld") "\n", iceptx);

  if (!IsComplete(iceptx)) {
    // If none, explore that point.

    // Round to avoid fragmentation.
    int64_t c = iceptx;
    c /= max_epoch_size;
    c *= max_epoch_size;

    printf("Haven't done it yet, so run " APURPLE("%lld") "+\n", c);
    return make_pair(c, max_epoch_size);
  } else {

    bool really_done = false;
    // Prefer extending to the left until we have some evidence, since
    // this seems to usually be an overestimate.
    bool extend_right = false;
    int64_t closest_dist = 999999999999;

    // XXX this is wrong for predict_h = false. ForEverVec is specifically
    // looping over h vecs.
    // TODO: Use Island
    ForEveryHVec(iceptx,
                    [&](int64_t x0, int64_t y0,
                        int64_t x1, int64_t y1,
                        int64_t iceptx) {
                      if (y0 > 0 && y1 < 0) {
                        really_done = true;
                      } else {
                        if (y1 > 0) {
                          // above the axis, so we'd
                          // extend to the right to find zero.
                          if (y1 < closest_dist) {
                            extend_right = true;
                            closest_dist = y1;
                          }
                        } else {
                          CHECK(y0 < 0);
                          if (-y0 < closest_dist) {
                            extend_right = false;
                            closest_dist = -y0;
                          }
                        }
                      }
                    });

    // a has an upward slope, so we need to reverse the direction
    // if we are looking for a zeroes.
    // XXX clean this up
    if (!predict_h) extend_right = !extend_right;

    if (really_done) {
      // ???
      printf(ARED("Unexpected") ": We have a zero here but regression "
             "predicted it elsewhere?");
      return NextGapAfter(0, max_epoch_size);
    }

    if (extend_right) {
      printf("Extend " AGREEN("right") ". Closest %lld\n", closest_dist);
      return NextGapAfter(iceptx, max_epoch_size);
    } else {
      auto go = NextGapBefore(iceptx, max_epoch_size);
      if (go.has_value()) {
        printf("Extend " AGREEN("left") ". Closest %lld\n", closest_dist);
        return go.value();
      } else {
        printf("Extend " AORANGE("right") " (no space). "
               "Closest %lld\n", closest_dist);
        return NextGapAfter(iceptx, max_epoch_size);
      }
    }
  }
}

std::pair<uint64_t, uint64_t> Database::PredictNextClose(
    uint64_t max_epoch_size) {
  PrintGaps(*this);

  const auto &[azeroes, hzeroes] = GetZeroes();
  printf("Get close calls...\n");
  static constexpr int64_t MAX_INNER_SUM = 10'000'000'000'000'000LL;
  std::vector<std::pair<int64_t, double>> close =
    Predict::FutureCloseCalls(*this, azeroes, hzeroes, MAX_INNER_SUM);

  // Top close calls..
  printf("Closest:\n");
  for (int i = 0; i < close.size(); i++) {
    const auto [iceptx, score] = close[i];

    using enum Database::IslandZero;
    if (iceptx > 0) {
      printf("  %s (dist %f) ",
             Util::UnsignedWithCommas(iceptx).c_str(), score);

      switch (IslandHZero(iceptx)) {
      case NONE: {
        // If none, explore that point.

        // Round to avoid fragmentation.
        int64_t c = iceptx;
        c /= max_epoch_size;
        c *= max_epoch_size;

        printf(AGREEN("NEW") "\n");
        return make_pair(c, max_epoch_size);
      }
      case HAS_ZERO:
        printf(AGREY("DONE") "\n");
        break;

      case NO_POINTS:
      case GO_LEFT: {
        auto go = NextGapBefore(iceptx, max_epoch_size);
        if (go.has_value()) {
          printf(ABLUE("BEFORE") "\n");
          return go.value();
        }
        break;
      }

      case GO_RIGHT: {
        printf(AYELLOW("AFTER") "\n");
        return NextGapAfter(iceptx, max_epoch_size);
      }
      }
    }
  }

  printf(ARED("No candidates beneath %lld!") "\n", MAX_INNER_SUM);
  return NextGapAfter(0, max_epoch_size);
}
