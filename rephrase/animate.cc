
// Experimental tool to animate "drawing" an input image.

#include <algorithm>
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

  std::unordered_map<uint32_t, Region> regions;
  Region transparent;

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
    //

    std::vector<uint32_t> order;
    for (const auto &[c, region] : regions)
      order.push_back(c);
    std::sort(order.begin(), order.end(),
              [this](uint32_t c1, uint32_t c2) {
                return regions[c1].pixels.size() >
                  regions[c2].pixels.size();
              });

    ImageRGBA frame(in->Width(), in->Height());

    int frame_num = 0;
    for (uint32_t color : order) {
      CHECK(regions.contains(color));
      // Blit all at once.
      // TODO: Use brush strokes!
      const Region &region = regions[color];
      for (int idx : region.pixels) {
        const auto &[x, y] = UnPos(idx);
        // Always with the original pixel, not the posterized one.
        frame.SetPixel32(x, y, in->GetPixel32(x, y));
      }

      std::string frame_file = StringPrintf("%s-%d.png",
                                            file_base_out.c_str(),
                                            frame_num);
      frame.Save(frame_file);
      printf("Wrote %s\n", frame_file.c_str());
      frame_num++;
    }

    // Draw transparent pixels last.
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

  // TODO: Animate!
};

int main(int argc, char **argv) {
  CHECK(argc == 3) << "./animate.exe in.png out-base";

  Animation animation(argv[1]);
  animation.Animate(argv[2]);

  return 0;
}
