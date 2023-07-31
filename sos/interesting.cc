
#include <array>
#include <vector>
#include <string>
#include <unordered_set>
#include <cstdint>

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
#include "predict.h"
#include "factorization.h"
#include "map-util.h"
#include "set-util.h"

using namespace std;

using re2::RE2;

static void PlotSquareValues(const Database &db) {
  static constexpr std::array<uint32_t, 9> COLOR = {
    0xAA3333FF, // a
    0x0088FFFF, // b
    0xFF8800FF, // c
    0xFF0088FF, // d
    0xAA33AAFF, // e
    0x33AA33FF, // f
    0x33FFFFFF, // g
    0x3333AAFF, // h
    0xAAAA33FF, // i
  };

  Bounds bounds;
  bounds.Bound(0, 0);
  for (const auto &[isum, square] : db.Almost2()) {
    const int64_t sum = square[0] + square[1] + square[2];
    bounds.Bound(isum, sum);
    for (uint64_t ss : square) {
      bounds.Bound(isum, ss);
    }
  }
  int WIDTH = 3000;
  int HEIGHT = 1800;
  bounds.AddMarginsFrac(0.01, 0.05, 0.01, 0.00);
  Bounds::Scaler scaler = bounds.Stretch(WIDTH, HEIGHT).FlipY();
  ImageRGBA plot(WIDTH, HEIGHT);
  plot.Clear32(0x000000FF);

  for (const auto &[isum, square] : db.Almost2()) {
    const int64_t sum = square[0] + square[1] + square[2];
    {
      const auto &[sx, sy] = scaler.Scale(isum, sum);
      plot.BlendFilledCircleAA32(sx, sy, 2, 0xFFFFFF33);
    }

    for (int c = 0; c < 9; c++) {
      const auto &[sx, sy] = scaler.Scale(isum, square[c]);
      plot.BlendFilledCircleAA32(sx, sy, 2, COLOR[c] & 0xFFFFFF7A);
    }
  }

  // legend
  static constexpr int LEGEND_X = 10;
  static constexpr int LEGEND_Y = 10;
  static constexpr int SPACEW = ImageRGBA::TEXT2X_WIDTH + 4;
  static constexpr int SPACEH = ImageRGBA::TEXT2X_HEIGHT + 4;
  for (int y = 0; y < 3; y++) {
    for (int x = 0; x < 3; x++) {
      int c = y * 3 + x;
      plot.BlendText2x32(LEGEND_X + x * SPACEW,
                         LEGEND_Y + y * SPACEH,
                         COLOR[c],
                         StringPrintf("%c", 'a' + c));
    }
  }

  plot.Save("squarevalues.png");
}

template<typename F>
static void CellFactors(const Database &db,
                        const std::string &what,
                        const F &get) {
  const auto &almost2 = db.Almost2();
  if (almost2.empty()) return;

  uint64_t first_n = get(almost2.begin()->second);

  std::unordered_set<uint64_t> seen;
  std::unordered_map<uint64_t, int> always;
  for (const auto &[b, e] : Factorization::Factorize(first_n))
    always[b] = e;

  int num_left = 100;

  for (const auto &[isum, square] : db.Almost2()) {
    uint64_t n = get(square);

    bool PRINT_EACH = num_left > 0;
    if (PRINT_EACH) num_left--;

    if (PRINT_EACH) printf(ACYAN("%llu") " Factors:", n);
    std::vector<std::pair<uint64_t, int>> factors =
      Factorization::Factorize(n);

    std::unordered_map<uint64_t, int> mfactors;
    for (const auto &[b, e] : factors) {
      mfactors[b] = e;
      seen.insert(b);
    }

    std::unordered_map<uint64_t, int> new_always;
    for (const auto &[b, e] : always) {
      if (mfactors.contains(b)) {
        new_always[b] = std::min(mfactors[b], e);
      }
    }
    always = std::move(new_always);

    if (PRINT_EACH) {
      for (const auto &[b, e] : factors) {
        if (e == 1) {
          printf(" " AWHITE("%llu"), b);
        } else {
          printf(" " AWHITE("%llu") AGREY("^") AYELLOW("%d"), b, e);
        }
      }
      printf("\n");
    }
  }

  printf("[" APURPLE("%s") "] All factors seen:", what.c_str());
  /*
  std::vector<uint64_t> all = SetToSortedVec(seen);
  for (int p = 2; p < all[all.size() - 1]; p++) {
    if (Factorization::IsPrime(p)) {
      if (seen.contains(p)) {
        printf(" " AGREEN("%d"), p);
      } else {
        printf(" " ARED("%d"), p);
      }
    }
  }
  */

  for (const uint64_t f : SetToSortedVec(seen))
    printf(" " AWHITE("%llu"), f);
  printf("\n");

  printf("[" APURPLE("%s") "] Factors always seen:", what.c_str());
  for (const auto &[b, e] : MapToSortedVec(always)) {
    if (e == 1) {
      printf(" " AWHITE("%llu"), b);
    } else {
      printf(" " AWHITE("%llu") AGREY("^") AYELLOW("%d"), b, e);
    }
  }
  printf("\n");
}

static void PrintFactors(const Database &db) {
  if (db.Almost2().empty()) return;

  // Prime factors always present. Really should have exponents, but
  // we know that it's at least a subset of this:
  // This is the factors of 319754, isum for the smallest almost2 square.
  std::unordered_set<uint64_t> always = {
    2, 29, 37, 149
  };

  std::unordered_set<uint64_t> seen;

  static constexpr bool PRINT_EACH = false;
  for (const auto &[isum, square] : db.Almost2()) {
    if (PRINT_EACH) printf(ACYAN("%llu") " Factors:", isum);
    std::vector<std::pair<uint64_t, int>> factors =
      Factorization::Factorize(isum);

    std::unordered_set<uint64_t> unique_factors;
    for (const auto &[b, e] : factors) unique_factors.insert(b);
    for (const uint64_t f : unique_factors) seen.insert(f);

    std::unordered_set<uint64_t> new_always;
    for (const uint64_t a : always) {
      if (unique_factors.contains(a)) new_always.insert(a);
    }
    always = std::move(new_always);

    if (PRINT_EACH) {
      for (const auto &[b, e] : factors) {
        if (e == 1) {
          printf(" " AWHITE("%llu"), b);
        } else {
          printf(" " AWHITE("%llu") AGREY("^") AYELLOW("%d"), b, e);
        }
      }
      printf("\n");
    }
  }

  if (false) {
    printf("All factors seen:");
    std::vector<uint64_t> all = SetToSortedVec(seen);
    for (int p = 2; p < all[all.size() - 1]; p++) {
      if (Factorization::IsPrime(p)) {
        if (seen.contains(p)) {
          printf(" " AGREEN("%d"), p);
        } else {
          printf(" " ARED("%d"), p);
        }
      }
    }

    // for (const uint64_t f : SetToSortedVec(seen))
    // printf(" " AWHITE("%llu"), f);
    printf("\n");
  }

  printf("Factors always seen:");
  for (const uint64_t f : SetToSortedVec(always))
    printf(" " AWHITE("%llu"), f);
  printf("\n");
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
  PlotSquareValues(db);

  PrintFactors(db);

  /*
  CellFactors(db, "ee", [](const Database::Square &square) {
      const auto &[aa, bb, cc, dd, ee, ff, gg, hh, ii] = square;
      return ee;
    });
  */

  CellFactors(db, "hh", [](const Database::Square &square) {
      const auto &[aa, bb, cc, dd, ee, ff, gg, hh, ii] = square;
      return hh;
    });


  const auto &[azeroes, hzeroes] = db.GetZeroes();

  // static constexpr size_t H_RADIX = 5;
  static constexpr size_t A_RADIX = 10;

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

  static constexpr int64_t MAX_PLOT_ISUM = 140'000'000'000'000LL;

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

    // XXX
    if (inner_sum > MAX_PLOT_ISUM)
      continue;

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

  for (const int64_t x : azeroes)
    StringAppendF(&azeroes_csv, "%lld\n", x);
  for (const int64_t x : hzeroes)
    StringAppendF(&hzeroes_csv, "%lld\n", x);

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

  printf("Get next hzero:\n");
  const int64_t next_hzero = Predict::NextInDensePrefixSeries(db, hzeroes);
  printf("Next hzero: " APURPLE("%lld") "\n", next_hzero);
  if (next_hzero < MAX_PLOT_ISUM) {
    plot_bounds.Bound(next_hzero, 0.0);
  }

  const int64_t next_azero = Predict::NextInDensePrefixSeries(db, azeroes);
  printf("Next azero: " APURPLE("%lld") "\n", next_azero);
  if (next_hzero < MAX_PLOT_ISUM) {
    plot_bounds.Bound(next_azero, 0.0);
  }

  int WIDTH = 3000;
  int HEIGHT = 1800;
  plot_bounds.AddMarginsFrac(0.01, 0.05, 0.01, 0.00);
  Bounds::Scaler scaler = plot_bounds.Stretch(WIDTH, HEIGHT).FlipY();
  ImageRGBA plot(WIDTH, HEIGHT);
  plot.Clear32(0x000000FF);

  // TODO: ensure slivers as much as possible, whether missing or done.
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

  // vertical ticks every 10 trillion
  const int64_t TICK_EVERY = 10'000'000'000'000LL;
  int xclearance = 0;
  for (int64_t x = 0; x < plot_bounds.MaxX(); x += TICK_EVERY) {
    const auto [x0, y0] = scaler.Scale(x, plot_bounds.MinY());
    const auto [x1, y1] = scaler.Scale(x, plot_bounds.MaxY());
    plot.BlendLine32(x0, y0, x1, y1, 0xFFFFFF22);

    int tx = x0 + 3;
    int ty = HEIGHT - ImageRGBA::TEXT_HEIGHT - 2;
    int d = x / 1'000'000'000'000LL;
    if (tx > xclearance) {
      plot.BlendText32(
          tx, ty,
          0xFFFFFF66,
          StringPrintf("%lld", d).c_str());
      int w = (d > 9 ? 2 : 1);
      plot.BlendText32(
          tx + ImageRGBA::TEXT_WIDTH * w, ty,
          0xFFFFFF33, "T");
      xclearance = tx + ImageRGBA::TEXT_WIDTH * (w + 1);
    }
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
    if (x < MAX_PLOT_ISUM) {
      int xx = scaler.ScaleX(x);
      int yy = scaler.ScaleY(0);
      plot.BlendLine32(xx, yy - 5, xx, yy + 5, 0x00FF0099);
    }
  }

  for (const double x : azeroes) {
    if (x < MAX_PLOT_ISUM) {
      int xx = scaler.ScaleX(x);
      int yy = scaler.ScaleY(0);
      plot.BlendLine32(xx, yy - 5, xx, yy + 5, 0xFF4400CC);
    }
  }

  {
    if (next_hzero < MAX_PLOT_ISUM) {
      int xx = scaler.ScaleX(next_hzero);
      int yy = scaler.ScaleY(0);
      plot.BlendLine32(xx, yy - 15, xx, yy - 10, 0x00FFFFCC);
      plot.BlendLine32(xx, yy + 10, xx, yy + 15, 0x00FFFFCC);
    }
  }

  {
    if (next_azero < MAX_PLOT_ISUM) {
      int xx = scaler.ScaleX(next_azero);
      int yy = scaler.ScaleY(0);
      plot.BlendLine32(xx, yy - 15, xx, yy - 10, 0xFF8800CC);
      plot.BlendLine32(xx, yy + 10, xx, yy + 15, 0xFF8800CC);
    }
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
