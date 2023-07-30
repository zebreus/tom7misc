
#include "database.h"

#include <string>
#include <cstdint>
#include <array>
#include <vector>

#include "base/logging.h"
#include "base/stringprintf.h"

#include "ansi.h"
#include "sos-util.h"
#include "util.h"
#include "interval-cover.h"
#include "re2/re2.h"
#include "vector-util.h"

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

  ReverseVector(&ret);
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
