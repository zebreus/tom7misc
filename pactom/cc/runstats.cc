
#include <memory>
#include <string>
#include <cstdio>

#include "pactom-util.h"
#include "pactom.h"
#include "util.h"
#include "base/stringprintf.h"
#include "base/logging.h"

using namespace std;

int main(int argc, char **argv) {

  std::unique_ptr<PacTom> pactom = PacTomUtil::Load(true);

  double total_miles = 0.0;
  for (const PacTom::Run &run : pactom->runs) {
    total_miles += PacTom::RunMiles(run);
  }
  printf("Total miles: %.5f\n", total_miles);

  int num_with_date = 0;
  std::map<int, int> by_year;
  {
    string out;
    for (const PacTom::Run &run : pactom->runs) {
      if (run.year > 0) {
        num_with_date++;
        by_year[run.year]++;
        StringAppendF(&out, "%04d-%02d-%02d\t%.5f\n",
                      run.year, run.month, run.day,
                      PacTom::RunMiles(run, true));
      }
    }

    Util::WriteFile("distances.tsv", out);
  }

  {
    ImageRGBA image(1920, 1080);
    image.Clear32(0x000000FF);

    Bounds bounds;
    {
      bounds.Bound(0, 0);
      // Room for date label.
      bounds.Bound(0, (8 + 1) * 9);
      int run_idx = 0;
      for (const PacTom::Run &run : pactom->runs) {
        if (run.year > 0) {
          bounds.Bound(run_idx, PacTom::RunMiles(run, true));
        }
        run_idx++;
      }
    }
    bounds.AddMarginFrac(0.02);

    Bounds::Scaler scaler = bounds.Stretch(1920, 1080).FlipY();

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
            image.BlendTextVert32(x, origin - date_label.size() * 9 + 3,
                                  true, 0xFFFFFFFF, date_label);
          }

          printf("%d %d  -> %.3f %.3f (y: %.3f, o: %.3f)\n",
                 run_idx, run_idx + 1, x, next_x, y, origin);

          // Always the run bar.
          const int w = (next_x - x) - 2;
          const int h = origin - y;
          image.BlendRect32(x, y, w, h, 0xCCCCFF33);

          run_idx++;
        }
      }
    }

    image.Save("distances.png");
  }

  for (const auto &[year, count] : by_year) {
    printf("%04d: %d\n", year, count);
  }

  return 0;
}
