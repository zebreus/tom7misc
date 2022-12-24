
#include <memory>
#include <string>
#include <cstdio>

#include "pactom-util.h"
#include "pactom.h"
#include "util.h"
#include "base/stringprintf.h"
#include "base/logging.h"
#include "color-util.h"

using namespace std;

static constexpr int START_YEAR = 2006;

int main(int argc, char **argv) {

  std::unique_ptr<PacTom> pactom = PacTomUtil::Load(true);

  double total_miles = 0.0;
  for (const PacTom::Run &run : pactom->runs) {
    total_miles += PacTom::RunMiles(run);
  }
  printf("Total miles: %.5f\n", total_miles);

  int num_with_date = 0;
  // Count
  std::map<int, int> by_year;
  // Distance. Key is 2006 + 12 * month
  std::map<int, double> by_month;

  {
    string out;
    for (const PacTom::Run &run : pactom->runs) {
      if (run.year > 0) {
        double miles = PacTom::RunMiles(run, true);
        num_with_date++;
        by_year[run.year]++;
        by_month[(run.year - 2006) * 12 + (run.month - 1)] += miles;
        StringAppendF(&out, "%04d-%02d-%02d\t%.5f\n",
                      run.year, run.month, run.day,
                      miles);
      }
    }

    Util::WriteFile("distances.tsv", out);
  }

  {
    ImageRGBA image(1920, 1080);
    image.Clear32(0x000000FF);

    Bounds bounds;
    int max_idx = 0;
    {
      bounds.Bound(0, 0);
      // Room for date label. (Hack! Should add this with some
      // version of AddMargin that works in output space, not input?)
      bounds.Bound(0, -5.0);
      int run_idx = 0;
      for (const PacTom::Run &run : pactom->runs) {
        if (run.year > 0) {
          bounds.Bound(run_idx, PacTom::RunMiles(run, true));
          run_idx++;
        }
      }
      max_idx = run_idx;
    }
    bounds.AddMarginFrac(0.05);

    Bounds::Scaler scaler = bounds.Stretch(1920, 1080).FlipY();

    // Scale and legend (miles).
    for (const int m : {0, 5, 10, 15, 20, 25, 30, 35}) {
      const int x0 = scaler.ScaleX(-0.5);
      const auto [x1, y] = scaler.Scale(max_idx + 0.75, m);
      const int x2 = scaler.ScaleX(max_idx + 1.5);
      image.BlendLine32(x0, y, x1, y, 0xAAAA22FF);
      image.BlendText2x32(x2, y - 8, 0xAAAA22FF,
                          StringPrintf("%d", m));
    }

    {
      int run_idx = 0;
      int prev_x = -100;
      const int origin = scaler.ScaleY(0);
      for (const PacTom::Run &run : pactom->runs) {
        if (run.year > 0) {
          double miles = PacTom::RunMiles(run, true);
          string date_label =
            StringPrintf("%04d-%02d-%02d",
                         run.year, run.month, run.day);

          const int x = scaler.ScaleX(run_idx);
          const int next_x = scaler.ScaleX(run_idx + 1);
          const int y = scaler.ScaleY(miles);

          // Label if we have room
          if (x - prev_x >= 10) {
            prev_x = x;
            image.BlendTextVert32(x, origin + date_label.size() * 9 + 3,
                                  true, 0xFFFFFFFF, date_label);
          }

          double t = std::lerp(0.20, 1.0, miles / 35.0);
          uint32 bar_color =
            ColorUtil::LinearGradient32(ColorUtil::HEATED_METAL, t);

          /*
          printf("%d %d  -> %.3f %.3f (y: %.3f, o: %.3f)\n",
                 run_idx, run_idx + 1, x, next_x, y, origin);
          */

          // Always the run bar.
          const int w = (next_x - x) - 2;
          const int h = origin - y;
          image.BlendRect32(x, y, w, h, bar_color & 0xFFFFFFAA);

          run_idx++;
        }
      }
    }

    image.Save("distances.png");
  }

  {
    ImageRGBA image(1920, 1080);
    image.Clear32(0x000000FF);

    Bounds bounds;
    double max_dist = 0.0;
    {
      bounds.Bound(0, 0);
      // Room for date label. (Hack! Should add this with some
      // version of AddMargin that works in output space, not input?)
      bounds.Bound(0, -7.0);

      bounds.Bound((2022 - START_YEAR) * 12 + 12, 0.0);

      for (const auto [midx, dist] : by_month) {
        max_dist = std::max(max_dist, dist);
        bounds.Bound(midx, dist);
      }
    }
    bounds.AddMarginFrac(0.02);

    Bounds::Scaler scaler = bounds.Stretch(1920, 1080).FlipY();

    {
      const int origin = scaler.ScaleY(0);
      int run_idx = 0;

      // Draw year start (there may not be a run in january!)
      for (int year = START_YEAR; year <= 2022; year++) {
        const int midx = (year - START_YEAR) * 12;
        const int x = scaler.ScaleX(midx);

        string date_label = StringPrintf("%04d", year);

        image.BlendText2x32(x, origin + 4,
                            0xFFFFFFFF, date_label);
      }

      for (const auto [midx, dist] : by_month) {
        int year = midx / 12;

        const int x = scaler.ScaleX(midx);
        const int next_x = scaler.ScaleX(midx + 1);
        const int y = scaler.ScaleY(dist);


        double t = std::lerp(0.20, 1.0, dist / max_dist);
        uint32 bar_color =
          ColorUtil::LinearGradient32(ColorUtil::HEATED_METAL, t);

        // Always the run bar.
        const int w = (next_x - x) - 2;
        const int h = origin - y;
        image.BlendRect32(x, y, w, h, bar_color & 0xFFFFFFAA);
      }
    }

    image.Save("distance-by-month.png");
  }

  for (const auto &[year, count] : by_year) {
    printf("%04d: %d\n", year, count);
  }

  return 0;
}
