
#ifndef _PACTOM_UTIL_H
#define _PACTOM_UTIL_H


#include <memory>
#include <cstdint>

#include "pactom.h"
#include "arcfour.h"
#include "lines.h"
#include "image.h"

struct PacTomUtil {
  static std::unique_ptr<PacTom> Load(bool merge_dates);

  static void SetDatesFrom(PacTom *dest, const PacTom &other,
                           int max_threads = 1);

  static void SortByDate(PacTom *dest);

  template<int RADIUS>
  static void DrawThickLine(ImageRGBA *image,
                            int x0, int y0, int x1, int y1,
                            uint32_t color);

  static uint32_t RandomBrightColor(ArcFour *rc);
};


// Implementations follow.

template<int RADIUS>
void PacTomUtil::DrawThickLine(ImageRGBA *image,
                               int x0, int y0, int x1, int y1,
                               uint32_t color) {
  image->BlendPixel32(x0, y0, color);
  for (const auto [x, y] : Line<int>{(int)x0, (int)y0, (int)x1, (int)y1}) {
    for (int dy = -RADIUS; dy <= RADIUS; dy++) {
      const int ddy = dy * dy;
      for (int dx = -RADIUS; dx <= RADIUS; dx++) {
        const int ddx = dx * dx;
        if (ddy + ddx <= RADIUS * RADIUS) {
          image->BlendPixel32(x + dx, y + dy, color);
        }
      }
    }
  }
}


#endif
