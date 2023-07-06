
#include <string>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>

#include "textsvg.h"
#include "base/stringprintf.h"
#include "image.h"

#include "encoding.h"
#include "tetris.h"
#include "util.h"

using uint32 = uint32_t;
using int64 = int64_t;

// If true, this generates the inverse colors of play.cc.
static constexpr bool INVERT = true;
static constexpr int SCALE = 6;

int main(int argc, char **argv) {
  static constexpr const char *solfile = "../tetris/best-solutions.txt";
  
  std::map<uint8_t, std::vector<Move>> sols =
    Encoding::ParseSolutions(solfile);

  int best_moves = 99999, worst_moves = 0;
  for (const auto &[b, movie] : sols) {
    best_moves = std::min(best_moves, (int)movie.size());
    worst_moves = std::max(worst_moves, (int)movie.size());
  }

  fprintf(stderr,
          "Best moves: %d\n"
          "Worst: %d\n",
          best_moves, worst_moves);

  const int width = worst_moves - best_moves;
  if (width == 0) {
    printf("Can't generate image because all solutions are same length!\n");
  } else {
    ImageRGBA image(16, 16);
    for (const auto &[b, movie] : sols) {
      int size = (int)movie.size();
      float f = (size - best_moves) / (float)width;
      int x = b & 0xF;
      int y = (b >> 4) & 0xF;
      const uint8 vv = std::clamp((int)roundf(f * 255.0f), 0, 255);
      const uint8 v = INVERT ? 255 - vv : vv;
      image.SetPixel(x, y, v, v, v, 0xFF);
    }
    image.ScaleBy(SCALE).Save("hyrule.png");
    fprintf(stderr, "Wrote hyrule.png\n");
  }
  
  return 0;
}

