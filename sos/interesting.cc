
#include <array>
#include <vector>
#include <string>

#include "base/logging.h"
#include "base/stringprintf.h"

#include "util.h"
#include "sos-util.h"
#include "ansi.h"
#include "re2/re2.h"
#include "database.h"

using namespace std;

using re2::RE2;

static void Interesting() {
  //     (!) 115147526400 274233600 165486240000 143974713600 93636000000 43297286400 21785760000 186997766400 72124473600
  RE2 almost2("\\(!\\) (\\d+) (\\d+) (\\d+) (\\d+) (\\d+) (\\d+) (\\d+) (\\d+) (\\d+)");

  int64_t best_err = 99999999999;
  std::array<uint64_t, 9> best_square = {0, 0, 0, 0, 0, 0, 0, 0, 0};

  string csv = "inner sum, dh, da, err\n";
  string intercept_csv = "intercept\n";
  string fives_csv = "inner sum, h0, h1, h2, h3, h4\n";

  int64_t num_almost2 = 0, num_almost1 = 0;
  std::vector<std::string> lines = Util::ReadFileToLines("interesting.txt");

  Database db = Database::FromInterestingFile("interesting.txt");
  printf("Database:\n"
         "%s\n", db.Epochs().c_str());

  // std::optional<std::pair<int64_t, int64_t>> prev_aerr;
  std::array<std::pair<int64_t, int64_t>, 5> prev_h = {
    make_pair(0LL, 0LL),
    make_pair(0LL, 0LL),
    make_pair(0LL, 0LL),
    make_pair(0LL, 0LL),
    make_pair(0LL, 0LL),
  };

  std::vector<std::array<uint64_t, 9>> rows;

  for (const std::string &line : lines) {
    uint64_t aa, bb, cc, dd, ee, ff, gg, hh, ii;
    if (RE2::FullMatch(line, almost2,
                       &aa, &bb, &cc, &dd, &ee, &ff, &gg, &hh, &ii)) {
      rows.emplace_back(std::array{aa, bb, cc, dd, ee, ff, gg, hh, ii});
    }
  }

  // Sort by inner sum, ascending.
  std::sort(rows.begin(), rows.end(),
            [](const std::array<uint64_t, 9> &r1,
               const std::array<uint64_t, 9> &r2) {
              // bb + cc
              return r1[1] + r1[2] < r2[1] + r2[2];
            });

  for (int idx = 0; idx < rows.size(); idx++) {
    const auto &row = rows[idx];
    const auto [aa, bb, cc, dd, ee, ff, gg, hh, ii] = row;
    uint64_t a = Sqrt64(aa);
    uint64_t b = Sqrt64(bb);
    uint64_t c = Sqrt64(cc);
    uint64_t d = Sqrt64(dd);
    uint64_t e = Sqrt64(ee);
    uint64_t f = Sqrt64(ff);
    uint64_t g = Sqrt64(gg);
    uint64_t h = Sqrt64(hh);
    uint64_t i = Sqrt64(ii);

    // a b c
    // d e f
    // g h i
    const uint64_t sum = aa + bb + cc;
    CHECK(sum == dd + ee + ff);
    CHECK(sum == gg + hh + ii);
    CHECK(sum == aa + dd + gg);
    CHECK(sum == bb + ee + hh);
    CHECK(sum == cc + ff + ii);
    CHECK(sum == aa + ee + ii);
    CHECK(sum == gg + ee + cc);

    // It is only written to "interesting" if these are squares.
    CHECK(b * b == bb);
    CHECK(c * c == cc);
    CHECK(d * d == dd);
    CHECK(e * e == ee);
    CHECK(f * f == ff);
    CHECK(g * g == gg);
    CHECK(i * i == ii);
    num_almost2++;

    // Precondition in the data, although a bug here would be
    // a pleasant surprise :)
    CHECK(h * h != hh);
    if (a * a == aa) num_almost1++;

    int64_t inner_sum = bb + cc;

    // How close are we to a square? The computed sqrt is a lower bound, so
    // it could actually be closer to the next square.
    int64_t herr1 = h * h - (int64_t)hh;
    int64_t herr2 = (h + 1) * (h + 1) - (int64_t)hh;

    int64_t aerr1 = a * a - (int64_t)aa;
    int64_t aerr2 = (a + 1) * (a + 1) - (int64_t)aa;

    int64_t herr = std::abs(herr1) < std::abs(herr2) ? herr1 : herr2;
    int64_t aerr = std::abs(aerr1) < std::abs(aerr2) ? aerr1 : aerr2;

    #if 0
    // Track an intercept.
    // XXX can just do mod 5?
    if (std::abs(aerr) < 10000) {
      printf("Saw %s %lld\n",
             Util::UnsignedWithCommas(inner_sum).c_str(),
             aerr);

      if (prev_aerr.has_value()) {
        const auto [prev_x, prev_y] = prev_aerr.value();
        if (prev_y > 0 && aerr < 0) {
          // approximate locally with a line to get
          // zero-intercept.
          double dx = inner_sum - prev_x;
          double dy = aerr - prev_y;

          int64_t iceptx = prev_x + (int64_t)((dx / dy) * -prev_y);
          printf("cross %s %lld -> %s %lld (icept at %s)\n",
                 Util::UnsignedWithCommas(prev_x).c_str(), prev_y,
                 Util::UnsignedWithCommas(inner_sum).c_str(), aerr,
                 Util::UnsignedWithCommas(iceptx).c_str());
          StringAppendF(&intercept_csv, "%lld\n", iceptx);
        }
      }

      prev_aerr = {make_pair(inner_sum, aerr)};
    }
    #endif
    {
      const auto [prev_x, prev_y] = prev_h[idx % 5];
      const int64_t cur_x = inner_sum;
      const int64_t cur_y = herr;
      if (prev_x != 0 && prev_y > 0 && cur_y < 0) {
        double dx = cur_x - prev_x;
        double dy = cur_y - prev_y;
        double m = dy / dx;
        int64_t iceptx = prev_x + std::round(-prev_y / m);
        CHECK(iceptx >= prev_x) << prev_x << "," << prev_y << " -> "
                                << cur_x << "," << cur_y << " slope "
                                << m << " @ "
                                << iceptx;
        CHECK(iceptx <= cur_x);
        // if ((idx % 5) == 0) printf("intercept %lld\n", iceptx);
        StringAppendF(&intercept_csv, "%lld\n", iceptx);
      }
      prev_h[idx % 5] = std::make_pair(cur_x, cur_y);
      if ((idx % 5) == 0) {
        // printf("%lld,%lld\n", cur_x, cur_y);
      }
    }

    int64_t err = std::abs(herr) + std::abs(aerr);
    StringAppendF(&csv, "%lld,%lld,%lld,%lld\n",
                  inner_sum, herr, aerr, err);
    if (err < best_err) {
      best_err = err;
      best_square = std::array<uint64_t, 9>{aa, bb, cc,
                                            dd, ee, ff,
                                            gg, hh, ii};
    }

    int col = idx % 5;
    // Group into fives, but sparse.
    StringAppendF(&fives_csv, "%lld,", inner_sum);
    for (int i = 0; i < col; i++) StringAppendF(&fives_csv, ",");
    StringAppendF(&fives_csv, "%lld,", herr);
    for (int i = col + 1; i < 5; i++) StringAppendF(&fives_csv, ",");
    StringAppendF(&fives_csv, "\n");
  }

  Util::WriteFile("density.csv", csv);
  Util::WriteFile("intercept.csv", intercept_csv);
  Util::WriteFile("fives.csv", fives_csv);

  {
    const auto [aa, bb, cc, dd, ee, ff, gg, hh, ii] = best_square;
    printf("Best square (err: %lld):\n"
           "%llu %llu %llu\n"
           "%llu %llu %llu\n"
           "%llu %llu %llu\n\n",
           best_err,
           aa, bb, cc,
           dd, ee, ff,
           gg, hh, ii);
  }

  printf("%lld almost2, %lld almost1\n", num_almost2, num_almost1);
}

int main(int argc, char **argv) {
  ANSI::Init();

  Interesting();

  return 0;
}
