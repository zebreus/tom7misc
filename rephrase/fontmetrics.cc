
// Throw-away test

#include "fonts/ttf.h"
#include "base/logging.h"

#include "ansi.h"

static void ShowChar() {
  TTF ttf("Georgia.ttf");

  ImageA a = ttf.GetChar('a', 50);
  for (int y = 0; y < a.Height(); y++) {
    for (int x = 0; x < a.Width(); x++) {
      const uint8_t c = a.GetPixel(x, y);
      /*
      uint8_t ch = ' ';
      if (c > 200) ch = '@';
      else if (c > 100) ch = ':';
      else if (c > 50) ch = '.';
      printf("%c", ch);
      */
      printf("%s@" ANSI_RESET,
             ANSI::ForegroundRGB(c, c, c).c_str());
    }
    printf("\n");
  }
}

// TODO: Also compute ideal intra-word spacing this way!

// For intra-letter spacing, we can use the built-in kerning
// table if there is one; this gives us the value for ideal
// glue. But when we stretch or squish, we can give better-tuned
// values by doing a kind of auto-kerning. The idea is that
// pushing together something like MM should be more costly
// than pushing together ><, since MM causes a more significant
// collision than >< (which only collides in the center).
// Similarly, stretching >< may be worse than stretching
// MM, since the discomfort at the top and bottom is higher.
//
// I think this can actually be simplified to the following. Take the
// integral over the distance between the two letters for each y
// coordinate where they both have curves, after applying some
// nonlinear function (d^2? I think TeX uses cube?). If we know
// the parameterization of this function, we can probably solve
// for the parameters, because the kerning table should be the
// distance that minimizes it.
//
// Integrating is probably doable for Beziers. But a quick-and-dirty
// way is to rasterize pairs and then just work at the pixel row
// level.

// Data for a kerning pair.
struct Kerning {
  // The additional spacing (often negative) as designed in
  // the font.
  std::optional<double> designed;

  // For each rasterized line, the original (prior to any kerning)
  // distance between the closest points on the two characters.
  // If neither character has a point on this raster line, the
  // value is NaN. If only the left character has a point, the
  // distance is +inf, and -inf if only the right character
  // has a point.
  // In principle this can also be a negative finite value, if
  // the characters overlap.
  std::vector<double> raster_distance;
};

struct Metrics {
  // The minimal y value (uppermost point) in any character in the font.
  double min_y = 0.0;
  // The maximal y value (lowermost point) in any character in the font.
  double max_y = 0.0;

  double min_x = 0.0;
  double max_x = 0.0;
};

Metrics GetTTFMetrics(const TTF &ttf) {
  Metrics metrics;
  const auto &[min_x, min_y, max_x, max_y] = ttf.BoundingBox();
  metrics.min_y = min_y;
  metrics.max_y = max_y;

  metrics.min_x = min_x;
  metrics.max_x = max_x;

  return metrics;
}


int main(int argc, char **argv) {
  ShowChar();

  TTF ttf("Georgia.ttf");
  GetTTFMetrics(ttf);

  return 0;
}
