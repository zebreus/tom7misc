
// Experimental tool to animate "drawing" an input image.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <numbers>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "image.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "color-util.h"
#include "geom/tree-2d.h"
#include "ansi.h"
#include "timer.h"
#include "threadutil.h"
#include "periodically.h"

struct Region {
  std::unordered_set<int> pixels;
  // L*A*B* color for nearest (perceptual) color computation.
  float l = 0.0, a = 0.0, b = 0.0;
};

struct Animation {
  Animation(const std::string &file_in) : file_in(file_in) {}

  std::string file_in;
  std::unique_ptr<ImageRGBA> in;
  std::unique_ptr<ImageRGBA> poster;
  // Gives the assigned layer (as an integer index) at each
  // pixel.
  std::unique_ptr<ImageA> layers;

  // The color (as a region key) of each ordered layer.
  std::vector<uint32_t> layer_colors;

  // These are the blobs to draw. They may only reflect the
  // final image once they are composited, as we smooth
  // deeper regions.
  std::unordered_map<uint32_t, Region> regions;
  Region transparent;
  int frame_num = 0;

  static constexpr uint32_t TRANSPARENT_USED  = 0x00000000;
  static constexpr uint32_t TRANSPARENT_EMPTY = 0xFFFFFF00;

  void Prep() {
    in.reset(ImageRGBA::Load(file_in));
    CHECK(in.get() != nullptr);

    // We expect the image to be a small number of colors,
    // but we can handle "anti-aliased" pixels by snapping
    // them to a nearby color.

    // Pixels with alpha value less than or equal to this amount are
    // treated as fully transparent.
    static constexpr uint8_t MIN_ALPHA = 0x07;

    auto MapAlpha = [](uint32_t c) -> uint32_t {
        const uint8_t a = c & 0xFF;
        if (a <= MIN_ALPHA) {
          // "fully" transparent; ignore colors.
          return 0x00000000;
        } else {
          return c | 0xFF;
        }
      };

    // For alpha, we have only FF and 00 (with r=g=b=0 as well) here.
    std::unordered_map<uint32_t, int64_t> color_counts;
    int64_t opaque_pixels = 0;
    for (int y = 0; y < in->Height(); y++) {
      for (int x = 0; x < in->Width(); x++) {
        const uint32_t c = MapAlpha(in->GetPixel32(x, y));
        color_counts[c]++;

        if (c & 0xFF) {
          opaque_pixels++;
        }
      }
    }

    // The minimum percentage of the opaque pixels that a color must
    // account for in order to be considered distinct.
    static constexpr float MIN_PERCENTAGE = 0.005f;
    const int64_t min_pixels = MIN_PERCENTAGE * opaque_pixels;

    // Create the regions.
    for (int y = 0; y < in->Height(); y++) {
      for (int x = 0; x < in->Width(); x++) {
        const uint32_t c = MapAlpha(in->GetPixel32(x, y));
        if (!regions.contains(c)) {
          if (color_counts[c] >= min_pixels) {
            const auto &[r, g, b, a_] = ColorUtil::U32ToFloats(c);
            Region &region = regions[c];
            std::tie(region.l, region.a, region.b) =
              ColorUtil::RGBToLAB(r, g, b);
          }
        }
      }
    }

    // XXX: Do something non-broken if there are no significant colors
    // in the image.
    CHECK(!regions.empty());

    // Get the region (by color id) of the closest color for this
    // pixel. TODO: Also consider its proximity to neighbors, as
    // we much prefer the input to be spatially smooth!
    auto ClosestColor = [this](int x, int y, uint32_t c) {
        uint32_t best_c = 0x00000000;
        double best_de = 10000.0;
        const auto &[rr, gg, bb, aa_] = ColorUtil::U32ToFloats(c);
        const auto &[l, a, b] = ColorUtil::RGBToLAB(rr, gg, bb);
        for (const auto &[cc, region] : regions) {
          double de = ColorUtil::DeltaE(region.l, region.a, region.b, l, a, b);
          if (de < best_de) {
            best_c = cc;
            best_de = de;
          }
        }

        CHECK(best_c != 0) << "No color was found?";
        return best_c;
      };



    // Now posterize the image. We use this to indicate regions of
    // the original image; it is not output directly.
    poster.reset(new ImageRGBA(in->Width(), in->Height()));
    for (int y = 0; y < in->Height(); y++) {
      for (int x = 0; x < in->Width(); x++) {
        int idx = y * in->Width() + x;
        const uint32_t original_c = in->GetPixel32(x, y);
        const uint32_t c = MapAlpha(original_c);
        if (c & 0xFF) {
          // Opaque
          if (color_counts[c] >= min_pixels) {
            regions[c].pixels.insert(idx);
            poster->SetPixel32(x, y, c);
          } else {
            // Find the closest color.
            uint32_t cc = ClosestColor(x, y, c);
            CHECK(regions.contains(cc));
            regions[cc].pixels.insert(idx);
            poster->SetPixel32(x, y, cc);
          }
        } else {
          // Transparent.
          // There is no need to draw a completely blank pixel, so we
          // just skip those.
          if (original_c & 0xFF) {
            transparent.pixels.insert(idx);
            poster->SetPixel32(x, y, TRANSPARENT_USED);
          } else {
            // We need to set something in the poster for every pixel
            // (even if it is not in a region); we use white transparent
            // pixels.

            poster->SetPixel32(x, y, TRANSPARENT_EMPTY);
          }
        }
      }
    }
  }

  int Idx(int x, int y) const {
    return y * in->Width() + x;
  }
  std::pair<int, int> UnIdx(int idx) const {
    return std::make_pair(idx % in->Width(), idx / in->Width());
  }

  void Animate(const std::string &file_base_out) {
    Prep();

    std::string poster_file =
      StringPrintf("%s-poster.png", file_base_out.c_str());
    poster->Save(poster_file);
    printf("Wrote %s\n", poster_file.c_str());

    // First thing we do is order the regions.
    // The simplest heuristic is to order by the number of
    // total pixels; this makes the big chunks of color go
    // first, and the highlights and shadows go after those.

    for (const auto &[c, region] : regions)
      layer_colors.push_back(c);
    std::sort(layer_colors.begin(), layer_colors.end(),
              [this](uint32_t c1, uint32_t c2) {
                return regions[c1].pixels.size() >
                  regions[c2].pixels.size();
              });

    std::unordered_map<uint32_t, int> region_to_layer;
    for (int idx = 0; idx < (int)layer_colors.size(); idx++) {
      region_to_layer[layer_colors[idx]] = idx;
    }

    // Assign layers.
    CHECK(layer_colors.size() < 255);
    layers.reset(new ImageA(in->Width(), in->Height()));
    for (int y = 0; y < in->Height(); y++) {
      for (int x = 0; x < in->Width(); x++) {
        uint32_t c = poster->GetPixel32(x, y);
        if (c == TRANSPARENT_EMPTY) {
          // two past the number of real layers.
          layers->SetPixel(x, y, layer_colors.size() + 1);
        } else if (c == TRANSPARENT_USED) {
          layers->SetPixel(x, y, layer_colors.size());
        } else {
          auto cit = region_to_layer.find(c);
          CHECK(cit != region_to_layer.end());
          layers->SetPixel(x, y, cit->second);
        }
      }
    }

    // Now that we have the ordering, we can smooth each region.
    for (int z = 0; z < (int)layer_colors.size(); z++) {
      SmoothRegion(file_base_out, z, 25);
    }

    ImageRGBA frame(in->Width(), in->Height());

    int layer_num = 0;
    for (uint32_t color : layer_colors) {
      CHECK(regions.contains(color));
      // Blit all at once.

      DrawLayer(file_base_out, &frame, layer_num, regions[color]);
      layer_num++;
    }

    // Draw (non-empty) transparent pixels last.
    DrawLayer(file_base_out, &frame, layer_num, transparent);

    std::string final_file = StringPrintf("%s-%d.png",
                                          file_base_out.c_str(),
                                          frame_num);
    frame.Save(final_file);
  }

  // For pixels that are STRICTLY deeper, but also
  // not one of the transparent layers.
  bool WillOverwrite(int x, int y, int z) {
    int depth = layers->GetPixel(x, y);
    return depth > z && depth < (int)layer_colors.size();
  }

  template<class F>
  void ForEachPixelInCircle(int x, int y, int radius,
                            const F &f) {
    const int xmin = std::max(0, x - radius);
    const int ymin = std::max(0, y - radius);
    const int xmax = std::min(in->Width(), x + radius);
    const int ymax = std::min(in->Height(), y + radius);

    const int sq_radius = radius * radius;

    // Loop over the bounding box and test distance.
    for (int yy = ymin; yy <= ymax; yy++) {
      int dy = y - yy;
      int ddy = dy * dy;
      for (int xx = xmin; xx <= xmax; xx++) {
        int dx = x - xx;
        int ddx = dx * dx;
        if (ddy + ddx <= sq_radius) {
          f(xx, yy);
        }
      }
    }
  };

  // The situation we are trying to fix is where a lower layer
  // is some color blob with lines or highlights above it. When
  // drawing naturally, we don't anticipate lines that are going
  // to come later and then draw *around* them! So when we know
  // that we'll be drawing over pixels later, we prefer the
  // geometry to be simple and contiguous.
  //
  // Lots of ways we could do this, including algorithmically
  // nice stuff. Here I am starting with a really simple approach,
  // which is to convolve a "voting" circle over the whole thing,
  // multiple times. We always do this subject to the mask of
  // layers that are going to overwrite us, which keeps the
  // blob within the hull, for one thing.
  void SmoothRegion(const std::string file_base_out, int z, int passes) {

    // TODO: Can I fiddle with this so that blobs don't always grow??
    static constexpr int KERNEL_RADIUS = 34;
    // We fill the center of the kernel only. Otherwise there is
    // essentially always a way of extending a blob by placing the
    // circle only ever-so-slightly off its edge.
    static constexpr int KERNEL_FILL_RADIUS = KERNEL_RADIUS >> 1;
    static constexpr float VOTE_THRESHOLD = 0.75f;

    CHECK(z >= 0 && z < (int)layer_colors.size());
    const uint32_t color = layer_colors[z];
    CHECK(regions.contains(color));
    Region &region = regions[color];

    // Use rasterized region.
    ImageA raster(in->Width(), in->Height());
    raster.Clear(0);
    for (int idx : region.pixels) {
      const auto &[x, y] = UnIdx(idx);
      raster.SetPixel(x, y, 0xFF);
    }

    for (int i = 0; i < passes; i++) {

      // To avoid weird appearance due to order dependence,
      // we add all the pixels at the end of the pass.
      ImageA new_raster = raster;
      std::unordered_set<int> pixels_to_add;

      for (int cy = 0; cy < in->Height(); cy++) {
        for (int cx = 0; cx < in->Width(); cx++) {

          int count = 0;
          int count_all = 0;
          ForEachPixelInCircle(
              cx, cy, KERNEL_RADIUS,
              [this, &region, &raster, &count, &count_all](int x, int y) {
                count_all++;
                // if (region.pixels.contains(Idx(x, y))) {
                // count++;
                // }
                if (raster.GetPixel(x, y)) {
                  count++;
                }
              });

          if (count / (float)count_all >= VOTE_THRESHOLD) {
            // Then fill it. We only fill inside the hull, though.
            ForEachPixelInCircle(
                cx, cy, KERNEL_FILL_RADIUS,
                [this, z, &pixels_to_add, &new_raster](int x, int y) {
                  if (WillOverwrite(x, y, z)) {
                    new_raster.SetPixel(x, y, 0xFF);
                    // pixels_to_add.insert(Idx(x, y));
                  }
                });

          }
        }
      }

      // Now any new pixels.
      // bool added = false;
      // for (int idx : pixels_to_add) {
      // added = region.pixels.insert(idx).second || added;
      // }
      const bool added = [&raster, &new_raster]() {
          for (int y = 0; y < raster.Height(); y++) {
            for (int x = 0; x < raster.Width(); x++) {
              if (raster.GetPixel(x, y) == 0 &&
                  new_raster.GetPixel(x, y) != 0) {
                return true;
              }
            }
          }
          return false;
        }();

      // No more passes are needed, since there will be no more
      // changes.
      if (!added) {
        printf("Reached a fixed point.\n");
        break;
      }

      // PERF only if debugging is enabled!
      {
        std::string file = StringPrintf("%s-region.%d.%d.png",
                                        file_base_out.c_str(),
                                        z, i);
        const auto &[r, g, b, a_] = ColorUtil::Unpack32(color);
        new_raster.AlphaMaskRGBA(r, g, b).Save(file);
        /*
        ImageRGBA smoothed(in->Width(), in->Height());
        for (int idx : region.pixels) {
          const auto &[x, y] = UnIdx(idx);
          smoothed.SetPixel32(x, y, color);
        }
        smoothed.Save(file);
        */
        printf("Wrote %s\n", file.c_str());
      }

      raster = std::move(new_raster);

    }

    // convert raster back to region. pixels are only added.
    for (int y = 0; y < raster.Height(); y++) {
      for (int x = 0; x < raster.Width(); x++) {
        if (raster.GetPixel(x, y) != 0) {
          region.pixels.insert(Idx(x, y));
        }
      }
    }

  }

  enum class PenShape {
    SQUARE,
    CIRCLE,
  };

  void DrawLayer(const std::string &file_base_out,
                 ImageRGBA *frame,
                 int layer_num,
                 const Region &region) {

    constexpr PenShape PEN_SHAPE = PenShape::SQUARE;

    // This essentially governs how many frames it takes to
    // generate the image.
    constexpr float MAX_PEN_VELOCITY = 24.0f;
    // The radius is half the side length for square pens.
    constexpr float PEN_RADIUS = 6.0f;

    // In [0, 1]. How much of the target velocity gets sent
    // to the current velocity per frame (this is not how
    // physics works).
    static constexpr float PEN_ACCELERATION = 0.5f;

    // This just increases the smoothness of strokes by
    // subdividing each frame into smaller timesteps (but
    // only applying a fraction of the velocity).
    constexpr int TIMESTEPS_PER_FRAME = 8;
    static_assert(TIMESTEPS_PER_FRAME > 0);

    // TODO: Blend frames

    constexpr float PEN_SEARCH_RADIUS =
      PEN_SHAPE == PenShape::CIRCLE ? PEN_RADIUS :
      (float) PEN_RADIUS * std::numbers::sqrt2;

    Asynchronously async(16);
    Periodically status_per(1.0);

    // Pick a random corner, maybe?
    float cx = 0.0f, cy = frame->Height() - 1.0f;
    // Pen velocity
    float cdx = 0.0f, cdy = 0.0f;

    Timer timer;

    Tree2D<int, char> remaining;
    using Pos = typename Tree2D<int, char>::Pos;
    for (int idx : region.pixels) {
      const auto &[x, y] = UnIdx(idx);
      remaining.Insert(x, y, 0);
    }
    const int start_pixels = remaining.Size();

    double sec = timer.Seconds();
    printf("Built tree in %s\n", ANSI::Time(sec).c_str());

    int layer_frames = 0;
    while (!remaining.Empty()) {

      for (int t = 0; t < TIMESTEPS_PER_FRAME; t++) {
        if (remaining.Empty()) break;

        // Move towards the closest pixel.
        const auto &[pos, c_, dist] = remaining.Closest(std::make_pair(cx, cy));
        const auto &[px, py] = pos;
        float vx = px - cx, vy = py - cy;
        // TODO: We want to ensure we make progress, so we should never
        // orbit a point. One simple way to do this would be to increase the
        // acceleration on each frame where we are not consuming pixels.

        cdx = std::lerp(cdx, vx, PEN_ACCELERATION / TIMESTEPS_PER_FRAME);
        cdy = std::lerp(cdy, vy, PEN_ACCELERATION / TIMESTEPS_PER_FRAME);
        float norm = std::sqrt(cdx * cdx + cdy * cdy);
        if (norm > MAX_PEN_VELOCITY) {
          cdx /= (norm / MAX_PEN_VELOCITY);
          cdy /= (norm / MAX_PEN_VELOCITY);
        }

        cx += (cdx / TIMESTEPS_PER_FRAME);
        cy += (cdy / TIMESTEPS_PER_FRAME);

        auto InsidePen = [cx, cy](float x, float y) {
            float dy = y - cy;
            float dx = x - cx;
            switch (PEN_SHAPE) {
            case PenShape::CIRCLE: {
              float ddy = dy * dy;
              float ddx = dx * dx;
              return sqrtf(ddx + ddy) <= PEN_RADIUS;
            }
            case PenShape::SQUARE: {
              return fabs(dx) <= PEN_RADIUS &&
                fabs(dy) <= PEN_RADIUS;
            }
            }
            LOG(FATAL) << "unhandled pen type";
            return false;
          };

        // We've moved the pen. Now eat any pixels that are in its
        // radius.
        CHECK(!remaining.Empty());
        // XXX We could get slightly better quality here if
        // we had lookup on floating point positions (or we can just
        // represent the tree that way in the first place).
        std::vector<std::tuple<Pos, char, double>> inside_coarse =
          remaining.LookUp(Pos(std::round(cx), std::round(cy)),
                           PEN_SEARCH_RADIUS);

        // Since the pen may not be a circle, we look up by the
        // containing radius but then filter.
        std::vector<Pos> inside;
        inside.reserve(inside_coarse.size());
        for (const auto &[pos, c_, dist_] : inside_coarse) {
          if (InsidePen(pos.first, pos.second)) {
            inside.push_back(pos);
          }
        }

        // Draw those pixels and delete them from the tree.
        std::unordered_map<uint32_t, int> ink_count;
        for (const auto &[x, y] : inside) {
          // Always with the original pixel, not the posterized one.
          // We save this color for ink spill. It might be better
          // to use the average color, or the most common one?
          uint32_t c = in->GetPixel32(x, y);
          frame->SetPixel32(x, y, c);
          ink_count[c]++;

          CHECK(remaining.Remove(x, y));
        }

        if (!inside.empty()) {
          // The pen is "down", so we also leave ink wherever it will
          // be overwritten on a deeper layer.

          uint32_t freq_color = 0;
          int freq_count = 0;
          for (const auto &[color, count] : ink_count) {
            if (count > freq_count) {
              freq_count = count;
              freq_color = color;
            }
          }

          // Rasterize the circle by looping over the bounding box.
          int xmin = std::floor(cx - PEN_RADIUS);
          int xmax = std::ceil(cx + PEN_RADIUS);
          int ymin = std::floor(cy - PEN_RADIUS);
          int ymax = std::ceil(cy + PEN_RADIUS);
          for (int y = ymin; y <= ymax; y++) {
            if (y >= 0 && y < frame->Height()) {
              float dy = y - cy;
              float ddy = dy * dy;
              for (int x = xmin; x <= xmax; x++) {
                if (x >= 0 && x < frame->Width()) {
                  float dx = x - cx;
                  float ddx = dx * dx;
                  const bool in_circle = sqrtf(ddx + ddy) < PEN_RADIUS;
                  if (in_circle) {
                    if (WillOverwrite(x, y, layer_num)) {
                      frame->SetPixel32(x, y, freq_color);
                    }
                  }
                }
              }
            }
          }
        }

      }

      // And draw the brush itself, but on a temporary copy of the
      // frame. We need the copy to write asynchronously anyway.
      // TODO: Draw different pen shapes?
      // TODO: Thicken the cursor?
      std::shared_ptr<ImageRGBA> frame_copy(frame->Copy());
      frame_copy->BlendCircle32(cx, cy, PEN_RADIUS + 1.0f, 0x00000077);

      async.Run(
          [&file_base_out,
           fnum = frame_num,
           fr = frame_copy]() {
          std::string frame_file = StringPrintf("%s-%d.png",
                                                file_base_out.c_str(),
                                                fnum);
          fr->Save(frame_file);
          });
      frame_num++;
      layer_frames++;


      if (status_per.ShouldRun()) {
        const int pixels_done = start_pixels - remaining.Size();
        std::string prog =
          ANSI::ProgressBar(pixels_done, start_pixels,
                            StringPrintf("Layer %d/%d",
                                         layer_num + 1,
                                         (int)layer_colors.size()),
                            timer.Seconds());
        printf(ANSI_UP "%s\n", prog.c_str());
      }
    }

    printf("Finished layer in %d frames, %s.\n", layer_frames,
           ANSI::Time(timer.Seconds()).c_str());
  }

};

int main(int argc, char **argv) {
  ANSI::Init();
  CHECK(argc == 3) << "./animate.exe in.png out-base";

  Animation animation(argv[1]);
  animation.Animate(argv[2]);

  return 0;
}
