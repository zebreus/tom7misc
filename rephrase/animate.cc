
// Experimental tool to animate "drawing" an input image.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
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

  std::unordered_map<uint32_t, Region> regions;
  Region transparent;
  int frame_num = 0;

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
        const uint32_t c = MapAlpha(in->GetPixel32(x, y));
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
          // Transparent
          transparent.pixels.insert(idx);
        }
      }
    }
  }

  std::pair<int, int> UnPos(int idx) const {
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
    CHECK(layer_colors.size() < 256);
    layers.reset(new ImageA(in->Width(), in->Height()));
    for (int y = 0; y < in->Height(); y++) {
      for (int x = 0; x < in->Width(); x++) {
        uint32_t c = poster->GetPixel32(x, y);
        auto cit = region_to_layer.find(c);
        CHECK(cit != region_to_layer.end());
        layers->SetPixel(x, y, cit->second);
      }
    }

    // TODO: Smooth lower regions (e.g. behind lines), since
    // obviously we wouldn't draw *around* the lines on higher
    // layers.

    ImageRGBA frame(in->Width(), in->Height());

    int layer_num = 0;
    for (uint32_t color : layer_colors) {
      CHECK(regions.contains(color));
      // Blit all at once.

      DrawLayer(file_base_out, &frame, layer_num, regions[color]);
      layer_num++;
    }

    // Draw transparent pixels last.
    // XXX also do this with DrawLayer
    for (int idx : transparent.pixels) {
      const auto &[x, y] = UnPos(idx);
      // Always with the original pixel, not the posterized one.
      frame.SetPixel32(x, y, in->GetPixel32(x, y));
    }
    std::string final_file = StringPrintf("%s-%d.png",
                                          file_base_out.c_str(),
                                          frame_num);
    frame.Save(final_file);
  }

  void DrawLayer(const std::string &file_base_out,
                 ImageRGBA *frame,
                 int layer_num,
                 const Region &region) {

    constexpr float MAX_PEN_VELOCITY = 12.0;
    constexpr float PEN_RADIUS = 6.0f;

    Asynchronously async(16);
    Periodically status_per(1.0);

    // Pick a random corner, maybe?
    float cx = 0.0f, cy = frame->Height() - 1.0f;
    // Pen velocity
    float cdx = 0.0f, cdy = 0.0f;

    Timer timer;
    Tree2D<int, char> remaining;
    using Pos = typename Tree2D<int, char>::Pos;
    for (int pos : region.pixels) {
      const auto &[x, y] = UnPos(pos);
      remaining.Insert(x, y, 0);
    }
    const int start_pixels = remaining.Size();

    double sec = timer.Seconds();
    printf("Built tree in %s\n", ANSI::Time(sec).c_str());

    int layer_frames = 0;
    while (!remaining.Empty()) {
      // Move towards the closest pixel.
      const auto &[pos, c_, dist] = remaining.Closest(std::make_pair(cx, cy));
      const auto &[px, py] = pos;
      float vx = px - cx, vy = py - cy;
      // TODO: We want to ensure we make progress, so we should never
      // orbit a point. One simple way to do this would be to increase the
      // acceleration on each frame where we are not consuming pixels.

      cdx = std::lerp(cdx, vx, 0.3);
      cdy = std::lerp(cdy, vy, 0.3);
      float norm = std::sqrt(cdx * cdx + cdy * cdy);
      if (norm > MAX_PEN_VELOCITY) {
        cdx /= (norm / MAX_PEN_VELOCITY);
        cdy /= (norm / MAX_PEN_VELOCITY);
      }

      cx += cdx;
      cy += cdy;

      // We've moved the pen. Now eat any pixels that are in its
      // radius.
      CHECK(!remaining.Empty());
      // XXX We could get slightly better quality here if
      // we had lookup on floating point positions (or we can just
      // represent the tree that way in the first place).
      std::vector<std::tuple<Pos, char, double>> inside =
        remaining.LookUp(Pos(std::round(cx), std::round(cy)), PEN_RADIUS);

      // Draw those pixels and delete them from the tree.
      std::unordered_map<uint32_t, int> ink_count;
      for (const auto &[pos, c_, dist_] : inside) {
        const auto &[x, y] = pos;
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
                  // For pixels that are STRICTLY deeper.
                  if (layers->GetPixel(x, y) > layer_num) {
                    frame->SetPixel32(x, y, freq_color);
                  }
                }
              }
            }
          }
        }
      }

      // And draw the brush itself, but on a temporary copy of the
      // frame.
      std::shared_ptr<ImageRGBA> frame_copy(frame->Copy());
      frame_copy->BlendCircle32(cx, cy, PEN_RADIUS + 1.0f, 0x00000077);

      #if 0
      for (int idx : region.pixels) {
        const auto &[x, y] = UnPos(idx);
        // Always with the original pixel, not the posterized one.
        frame->SetPixel32(x, y, in->GetPixel32(x, y));
      }
      #endif

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
