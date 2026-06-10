
// Experimental "Dirty rectangles"-like data structure.
// It's just a raster bitmap!

#ifndef _CC_LIB_DIRTY_H
#define _CC_LIB_DIRTY_H

#include <utility>

#include "image.h"
#include "base/logging.h"

struct Dirty {
  // Scale is an integer multiplier, which causes the internal
  // raster to be lower resolution; the external interface
  // remains the same.
  Dirty(int width, int height, int scale = 1) : scale(scale) {

    CHECK(scale >= 1);
    int w = width / scale + ((width % scale == 0) ? 0 : 1);
    int h = height / scale + ((height % scale == 0) ? 0 : 1);

    raster = Image1(w, h);
    raster.Clear(false);
  }

  void MarkUsed(int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return;
    int rx = CoordToRaster(x);
    int ry = CoordToRaster(y);
    int rw = CoordToRaster(x + w - 1) - rx + 1;
    int rh = CoordToRaster(y + h - 1) - ry + 1;
    raster.SetRect(rx, ry, rw, rh, true);
  }

  bool IsUsed(int x, int y, int w, int h) const {
    if (w <= 0 || h <= 0) return false;
    int rx = CoordToRaster(x);
    int ry = CoordToRaster(y);
    int rw = CoordToRaster(x + w - 1) - rx + 1;
    int rh = CoordToRaster(y + h - 1) - ry + 1;
    for (int yy = 0; yy < rh; yy++) {
      for (int xx = 0; xx < rw; xx++) {
        if (raster.GetPixel(rx + xx, ry + yy)) return true;
      }
    }
    return false;
  }

  // Find a place near (x, y) to place a rectangle of size w x h.
  // If we can't find one, just return (x, y). Does not claim the space.
  std::pair<int, int> PlaceNearby(int x, int y, int w, int h,
                                  int max_dist) const;

  // Finds a location for the input rectangle (size width * height)
  // near the provided src point. Always succeeds, since it will place
  // the rectangle outside of the raster if necessary. The rectangle
  // is placed to maximize the distance to dirty pixels (from any part
  // of it), but to minimize the distance to the source point (from
  // any point on the rectangle).
  //   The outside_penalty is framed as a distance to add into
  //     the cost. If the entire rectangle is outside the image,
  //     then this entire cost is incurred. If a fraction of the
  //     rectangle is outside, then that fraction of the cost is
  //     incurred.
  //
  //   The nearness_penalty is a multiplier on the penalty for being
  //     near dirty pixels. Higher multiplers mean that the placement
  //     will avoid geometry more (at the expense of being further
  //     from the target point).
  std::pair<int, int> FindEmptySpace(int srcx, int srcy,
                                     int width, int height,
                                     double outside_penalty,
                                     double nearness_penalty,
                                     // No part of the rectangle
                                     // can be closer than this
                                     // distance to the source.
                                     double min_distance = 0.0) const;

 private:
  inline int CoordToRaster(int d) const {
    if (d < 0) return (d - scale + 1) / scale;
    else return d / scale;
  }

  int scale = 1;
  Image1 raster;
};

#endif
