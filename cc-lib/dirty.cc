
#include "dirty.h"

#include <limits>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

#include "image.h"
// XXX This doesn't work that well and depending on this
// library is messy. Maybe we should simplify it...
#include "integer-voronoi.h"

// Find a place near (x, y) to place a rectangle of size w x h.
// If we can't find one, just return (x, y). Does not claim the space.
std::pair<int, int> Dirty::PlaceNearby(int x, int y, int w, int h,
                                       int max_dist) const {
  if (!IsUsed(x, y, w, h)) {
    // printf("Initial spot is free\n");
    return std::make_pair(x, y);
  }

  // The smallest circle that fits in the thing.
  const float dia = std::min(w, h);
  const float eff_dia = std::max((float)scale, dia);

  // XXX improve this!
  // Try increasing distances from the point, up to the maximum distance.
  // Step by at least 1, but up to eff_dia to take advantage of the
  // coarser raster.
  int r_step = std::max(1, (int)eff_dia);
  for (int r = 1; r < max_dist; r += r_step) {
    // Then try placing at this distance but at various angles.

    // Number of circles we could place at this distance without overlapping.
    // 2π / (2πr / eff_dia) =
    // 1 / (r / eff_dia) =
    // eff_dia / r
    float span = eff_dia / r;
    for (float theta = 0.0f; theta < 2.0 * std::numbers::pi; theta += span) {
      int xpos = std::round(x + std::cos(theta) * r);
      int ypos = std::round(y + std::sin(theta) * r);
      if (!IsUsed(xpos, ypos, w, h)) {
        return std::make_pair(xpos, ypos);
      }
    }
  }

  // printf("Couldn't find anywhere.\n");
  return std::make_pair(x, y);
}

std::pair<int, int> Dirty::FindEmptySpace(int srcx, int srcy,
                                          int width, int height,
                                          double outside_penalty,
                                          double nearness_penalty,
                                          double min_distance) const {
  if (width <= 0 || height <= 0) return std::make_pair(srcx, srcy);

  ImageF df = IntegerVoronoi::DistanceField(raster);
  // TODO: Use euclidean distance, not normalized distance...
  ImageF norm_df = IntegerVoronoi::NormalizeDistanceField(df);

  double best_cost = std::numeric_limits<double>::infinity();
  int best_x = srcx;
  int best_y = srcy;

  auto SquaredDistToSrc = [&](int x, int y) {
      double dx = 0.0;
      if (srcx < x) dx = x - srcx;
      else if (srcx > x + width - 1) dx = srcx - (x + width - 1);

      double dy = 0.0;
      if (srcy < y) dy = y - srcy;
      else if (srcy > y + height - 1) dy = srcy - (y + height - 1);

      return dx * dx + dy * dy;
    };

  auto DistToSrc = [&](int x, int y) {
      return std::sqrt(SquaredDistToSrc(x, y));
    };

  auto OutsideFraction = [&](int x, int y) {
    int raster_w = raster.Width() * scale;
    int raster_h = raster.Height() * scale;

    int ix0 = std::max(x, 0);
    int iy0 = std::max(y, 0);
    int ix1 = std::min(x + width, raster_w);
    int iy1 = std::min(y + height, raster_h);

    if (ix0 < ix1 && iy0 < iy1) {
      double intersect_area = (double)(ix1 - ix0) * (iy1 - iy0);
      return 1.0 - intersect_area / ((double)width * height);
    }
    return 1.0;
  };

  auto GetMinNdf = [&](int x, int y, double lower_bound) {
    int rx = CoordToRaster(x);
    int ry = CoordToRaster(y);
    int rw = CoordToRaster(x + width - 1) - rx + 1;
    int rh = CoordToRaster(y + height - 1) - ry + 1;

    // Branch and bound: If the min_val drops below this target, the final
    // cost will be >= best_cost, so we can abort early.
    // We never want to overlap dirty pixels (ndf == 0), so target is at
    // least 0.0.
    double target_min = 0.0;
    if (nearness_penalty > 0.0 &&
        best_cost < std::numeric_limits<double>::infinity()) {
      target_min = std::max(
          target_min,
          1.0 - (best_cost - lower_bound) / nearness_penalty);
    }

    double min_val = 1.0;
    bool any_inside = false;
    int w = raster.Width();
    int h = raster.Height();

    auto Check = [&](int cx, int cy) {
      if (cx >= 0 && cx < w && cy >= 0 && cy < h) {
        double val = norm_df.GetPixel(cx, cy);
        if (val < min_val) {
          min_val = val;
        }
        any_inside = true;
      }
    };

    // Constant time coarse pass.
    if (rw >= 6 && rh >= 6) {
      int xs[3] = {0, rw / 2, rw - 1};
      int ys[3] = {0, rh / 2, rh - 1};
      for (int yy : ys) {
        for (int xx : xs) {
          Check(rx + xx, ry + yy);
          if (min_val <= target_min) return min_val;
        }
      }
    }

    // Fine pass for the remaining pixels.
    for (int yy = 0; yy < rh; ++yy) {
      for (int xx = 0; xx < rw; ++xx) {
        Check(rx + xx, ry + yy);
        if (min_val <= target_min) return min_val;
      }
    }

    if (!any_inside) return 1.0;
    return min_val;
  };

  auto Evaluate = [&](int x, int y) {
    double d_src = DistToSrc(x, y);
    if (d_src < min_distance) return;

    double out_frac = OutsideFraction(x, y);
    double lower_bound = d_src + out_frac * outside_penalty;
    if (lower_bound >= best_cost) return;

    double ndf = GetMinNdf(x, y, lower_bound);
    // Don't allow overlapping dirty pixels at all.
    if (ndf <= 0.0) return;

    double cost = lower_bound + nearness_penalty * (1.0 - ndf);

    if (cost < best_cost) {
      best_cost = cost;
      best_x = x;
      best_y = y;
    }
  };

  int r = 0;
  for (;;) {
    bool possible_improvement = false;
    for (int dx = -r; dx <= r; dx++) {
      int x = srcx + dx;
      int y = srcy - r;
      if (DistToSrc(x, y) <= best_cost) {
        possible_improvement = true;
        Evaluate(x, y);
        if (best_cost <= 1e-9) return std::make_pair(best_x, best_y);
      }
      if (r > 0) {
        y = srcy + r;
        if (DistToSrc(x, y) <= best_cost) {
          possible_improvement = true;
          Evaluate(x, y);
          if (best_cost <= 1e-9) return std::make_pair(best_x, best_y);
        }
      }
    }

    for (int dy = -r + 1; dy <= r - 1; dy++) {
      int x = srcx - r;
      int y = srcy + dy;
      if (DistToSrc(x, y) <= best_cost) {
        possible_improvement = true;
        Evaluate(x, y);
        if (best_cost <= 1e-9) return std::make_pair(best_x, best_y);
      }
      x = srcx + r;
      if (DistToSrc(x, y) <= best_cost) {
        possible_improvement = true;
        Evaluate(x, y);
        if (best_cost <= 1e-9) return std::make_pair(best_x, best_y);
      }
    }

    if (!possible_improvement &&
        best_cost < std::numeric_limits<double>::infinity()) {
      break;
    }
    r++;
  }

  return std::make_pair(best_x, best_y);
}

