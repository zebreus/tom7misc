
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

using namespace std;

void Database::AddEpoch(uint64_t start, uint64_t size) {
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
