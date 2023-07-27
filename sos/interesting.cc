
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
#include "image.h"
#include "bounds.h"
#include "color-util.h"

using namespace std;

using re2::RE2;

template<size_t RADIX>
static void RecordZeroes(
    const Database &db,
    int idx,
    int64_t inner_sum,
    int64_t err,
    bool positive_slope,
    std::array<std::pair<int64_t, int64_t>, RADIX> *prev,
    std::vector<int64_t> *zeroes,
    std::string *zeroes_csv) {
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

    if (db.CompleteBetween(prev_x, cur_x)) {
      zeroes->push_back(iceptx);
      StringAppendF(zeroes_csv, "%lld\n", iceptx);
    }

  }
  (*prev)[idx % RADIX] = std::make_pair(cur_x, cur_y);
}


static void Interesting() {
  //     (!) 115147526400 274233600 165486240000 143974713600 93636000000 43297286400 21785760000 186997766400 72124473600
  RE2 almost2("\\(!\\) (\\d+) (\\d+) (\\d+) (\\d+) (\\d+) (\\d+) (\\d+) (\\d+) (\\d+)");

  int64_t best_err = 99999999999;
  int64_t best_herr = 99999999999;
  int64_t best_aerr = 99999999999;
  std::array<uint64_t, 9> best_square = {0, 0, 0, 0, 0, 0, 0, 0, 0};
  std::array<uint64_t, 9> best_hsquare = {0, 0, 0, 0, 0, 0, 0, 0, 0};
  std::array<uint64_t, 9> best_asquare = {0, 0, 0, 0, 0, 0, 0, 0, 0};

  string csv = "inner sum, dh, da, err\n";
  string intercept_csv = "intercept\n";
  string fives_csv = "inner sum, h0, h1, h2, h3, h4\n";
  string hzeroes_csv = "hzero x\n";
  string azeroes_csv = "azero x\n";

  int64_t num_almost2 = 0, num_almost1 = 0;
  std::vector<std::string> lines = Util::ReadFileToLines("interesting.txt");

  Database db = Database::FromInterestingFile("interesting.txt");
  printf("Database:\n"
         "%s\n", db.Epochs().c_str());

  // std::optional<std::pair<int64_t, int64_t>> prev_aerr;
  static constexpr size_t H_RADIX = 5;
  std::array<std::pair<int64_t, int64_t>, H_RADIX> prev_h;
  std::fill(prev_h.begin(), prev_h.end(), make_pair(0LL, 0LL));
  static constexpr size_t A_RADIX = 10;
  std::array<std::pair<int64_t, int64_t>, A_RADIX> prev_a;
  std::fill(prev_a.begin(), prev_a.end(), make_pair(0LL, 0LL));

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

  // Points for plot.
  // h, a, abs sum
  std::vector<std::tuple<int64_t, int64_t, int64_t, int64_t>> errors;
  // Intercepts where we actually have the point before and after.
  std::vector<int64_t> hzeroes;
  std::vector<int64_t> azeroes;

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

    int64_t err = std::abs(herr) + std::abs(aerr);

    errors.emplace_back(inner_sum, herr, aerr, err);

    RecordZeroes<H_RADIX>(db, idx, inner_sum, herr, false,
                          &prev_h, &hzeroes, &hzeroes_csv);
    RecordZeroes<A_RADIX>(db, idx, inner_sum, aerr, true,
                          &prev_a, &azeroes, &azeroes_csv);

    StringAppendF(&csv, "%lld,%lld,%lld,%lld\n",
                  inner_sum, herr, aerr, err);
    if (err < best_err) {
      best_err = err;
      best_square = std::array<uint64_t, 9>{aa, bb, cc,
                                            dd, ee, ff,
                                            gg, hh, ii};
    }

    if (std::abs(herr) < std::abs(best_herr)) {
      best_herr = herr;
      best_hsquare = std::array<uint64_t, 9>{aa, bb, cc,
                                             dd, ee, ff,
                                             gg, hh, ii};
    }

    if (std::abs(aerr) < std::abs(best_aerr)) {
      best_aerr = aerr;
      best_asquare = std::array<uint64_t, 9>{aa, bb, cc,
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
  Util::WriteFile("hzeroes.csv", hzeroes_csv);
  Util::WriteFile("azeroes.csv", azeroes_csv);

  // Prep bounds.
  Bounds plot_bounds;
  for (const auto &[x, herr, aerr, err] : errors) {
    plot_bounds.Bound(x, herr);
    plot_bounds.Bound(x, aerr);
    plot_bounds.Bound(x, err);
  }

  int WIDTH = 3000;
  int HEIGHT = 1800;
  plot_bounds.AddMarginsFrac(0.01, 0.05, 0.01, 0.00);
  Bounds::Scaler scaler = plot_bounds.Stretch(WIDTH, HEIGHT).FlipY();
  ImageRGBA plot(WIDTH, HEIGHT);
  plot.Clear32(0x000000FF);

  // missing regions
  {
    const IntervalCover<bool> done = db.Done();
    for (uint64_t s = done.First();
         !done.IsAfterLast(s);
         s = done.Next(s)) {
      const auto span = done.GetPoint(s);
      if (!span.data) {
       const double x0d = scaler.ScaleX(span.start);
       const double x1d = scaler.ScaleX(span.end);
       int x0 = (int)std::round(x0d);
       int w = std::max((int)std::round(x1d) - x0, 1);
       plot.BlendRect32(x0, 0, w, HEIGHT, 0x111111FF);
      }
    }
  }


  // x axis
  {
    const auto [x0, y0] = scaler.Scale(plot_bounds.MinX(), 0);
    const auto [x1, y1] = scaler.Scale(plot_bounds.MaxX(), 0);
    plot.BlendLine32(x0, y0, x1, y1, 0xFFFFFF33);
  }

  // vertical ticks every 1 trillion
  for (int64_t x = 0; x < plot_bounds.MaxX(); x += 1'000'000'000'000LL) {
    const auto [x0, y0] = scaler.Scale(x, plot_bounds.MinY());
    const auto [x1, y1] = scaler.Scale(x, plot_bounds.MaxY());
    plot.BlendLine32(x0, y0, x1, y1, 0xFFFFFF22);

    int tx = x0 + 3;
    int ty = HEIGHT - ImageRGBA::TEXT2X_HEIGHT - 2;
    int d = x / 1'000'000'000'000LL;
    plot.BlendText2x32(
        tx, ty,
        0xFFFFFF66,
        StringPrintf("%lld", d).c_str());
    plot.BlendText2x32(
        tx + ImageRGBA::TEXT2X_WIDTH * (d > 9 ? 2 : 1), ty,
        0xFFFFFF33, "T");
  }


  static constexpr bool RAINBOW_A = false;
  {
    int idx = 0;
    std::array<std::pair<int64_t, int64_t>, A_RADIX> lasta;
    std::fill(lasta.begin(), lasta.end(), std::make_pair(0.0, 0.0));
    for (const auto &[x, herr_, y, err_] : errors) {
      const auto &[lx, ly] = lasta[idx];
      const double sx0 = scaler.ScaleX(lx);
      const double sy0 = scaler.ScaleY(ly);
      const double sx1 = scaler.ScaleX(x);
      const double sy1 = scaler.ScaleY(y);
      uint32_t acolor =
        RAINBOW_A ?
        ColorUtil::HSVAToRGBA32((idx % A_RADIX) / (float)A_RADIX,
                                1.0f,
                                0.8f,
                                0.25f) :
        0xAA333340;

      // Don't draw the wrap-around.
      if (ly < y) {
        // And, must be in the same span.
        if (db.CompleteBetween(lx, x)) {
          plot.BlendLineAA32(sx0, sy0, sx1, sy1, acolor);
        }
      }
      lasta[idx] = make_pair(x, y);
      idx++;
      idx %= A_RADIX;
    }
  }


  int idx = 0;
  for (const auto &[x, herr, aerr, err] : errors) {
    const double xx = scaler.ScaleX(x);
    const double hh = scaler.ScaleY(herr);
    const double aa = scaler.ScaleY(aerr);
    const double ee = scaler.ScaleY(err);

    uint32_t acolor =
      RAINBOW_A ?
      ColorUtil::HSVAToRGBA32((idx % A_RADIX) / (float)A_RADIX,
                              1.0f,
                              0.8f,
                              0.75f) :
      0xAA3333AA;

    plot.BlendFilledCircleAA32(xx, aa, 3, acolor);
    plot.BlendFilledCircleAA32(xx, ee, 3, 0xAAAA33AA);
    plot.BlendFilledCircleAA32(xx, hh, 3, 0x3333AAAA);
    idx++;
  }

  for (const double x : hzeroes) {
    int xx = scaler.ScaleX(x);
    int yy = scaler.ScaleY(0);
    plot.BlendLine32(xx, yy - 5, xx, yy + 5, 0x00FF0099);
  }

  for (const double x : azeroes) {
    int xx = scaler.ScaleX(x);
    int yy = scaler.ScaleY(0);
    plot.BlendLine32(xx, yy - 5, xx, yy + 5, 0xFF4400CC);
  }

  plot.Save("plot.png");
  printf("Wrote plot.png\n");

  auto PrintBest = [](const char *what,
                      const Database::Square &square, int64_t err) {
    const auto [aa, bb, cc, dd, ee, ff, gg, hh, ii] = square;
    printf("Best %s (err: %lld):\n"
           "%llu %llu %llu\n"
           "%llu %llu %llu\n"
           "%llu %llu %llu\n"
           AGREY("Inner sum: %llu") "\n",
           what,
           err,
           aa, bb, cc,
           dd, ee, ff,
           gg, hh, ii,
           bb + cc);
  };

  PrintBest(APURPLE("overall"), best_square, best_err);
  PrintBest(ABLUE("a-err"), best_asquare, best_aerr);
  PrintBest(ACYAN("h-err"), best_hsquare, best_herr);

  printf("%lld almost2, %lld almost1\n", num_almost2, num_almost1);
}

int main(int argc, char **argv) {
  ANSI::Init();

  Interesting();

  return 0;
}
