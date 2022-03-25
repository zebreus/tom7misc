
#include <string>
#include <cmath>
#include <cstdint>
#include <vector>

#include "textsvg.h"
#include "geom/hilbert-curve.h"
#include "base/stringprintf.h"
#include "threadutil.h"

using namespace std;
using uint32 = uint32_t;
using int64 = int64_t;

static constexpr int THREADS = 4;

int main(int argc, char **argv) {

  std::vector<string> cells =
    ParallelTabulate(256, [](int64_t a) {
        const uint64_t pos1 = (uint32)a << 24;
        const auto [x32, y32] = HilbertCurve::To2D(16, pos1);

        int xmin = (int)x32, xmax = (int)x32;
        int ymin = (int)y32, ymax = (int)y32;
    
        // Brutish way to compute area a.*.*.*
        // This doesn't actually work; two corners will have
        // these extremal values but not the others.
        // for (int b : {0, 255}) {
        // for (int c : {0, 255}) {
        // for (int d : {0, 255}) {
        for (int b = 0; b < 256; b++) {
          for (int c = 0; c < 256; c++) {
            for (int d = 0; d < 256; d++) {      
              const uint64_t pos = ((uint32)a << 24) | ((uint32)b << 16) |
                ((uint32)c << 8) | (uint32)d;
              const auto [x32, y32] = HilbertCurve::To2D(16, pos);
              xmin = std::min((int)x32, xmin);
              xmax = std::max((int)x32, xmax);
              ymin = std::min((int)y32, ymin);
              ymax = std::max((int)y32, ymax);
            }
          }
        }
        double x = xmin / 100.0;
        double y = ymin / 100.0;
        double w = (xmax - xmin) / 100.0;
        double h = (ymax - ymin) / 100.0;
        string ret =
          StringPrintf(
              "<rect fill=\"none\" stroke-width=\"1\" stroke=\"#000\" "
              "x=\"%.2f\" y=\"%.2f\" "
              "width=\"%.2f\" height=\"%.2f\" />\n",
              x, y, w, h);

        constexpr double font_size = 16.0;
        StringAppendF(
            &ret, "%s",
            TextSVG::Text(x + w / 8, y + h / 2 + font_size / 2,
                          "sans-serif", font_size,
                          {{"#000", StringPrintf("%d", a)}}).c_str());
        return ret;
      }, THREADS);

  printf("%s", TextSVG::Header(655.36, 655.36).c_str());
  for (const string &s : cells) printf("%s", s.c_str());
  printf("%s", TextSVG::Footer().c_str());

  return 0;
}
