
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

// f(x0, y0, x1, y1, iceptx);
void Database::ForEveryVec(uint64_t pt,
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
