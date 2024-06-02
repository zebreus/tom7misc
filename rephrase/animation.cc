
#include "animation.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <numbers>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ansi.h"
#include "arcfour.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "bounds.h"
#include "color-util.h"
#include "geom/tree-2d.h"
#include "image.h"
#include "integer-voronoi.h"
#include "periodically.h"
#include "randutil.h"
#include "threadutil.h"
#include "timer.h"

namespace {

static constexpr bool DEBUG_ANIMATION = false;

struct Region {
  std::unordered_set<int> pixels;
  // L*A*B* color for nearest (perceptual) color computation.
  float l = 0.0, a = 0.0, b = 0.0;
};

struct AnimationImpl : public Animation {
  AnimationImpl(const ImageRGBA &image_in,
                const Animation::Options &opt) :
    in(image_in),
    opt(opt) {

    CHECK(opt.timesteps_per_frame > 0);
  }

  // Not owned.
  const ImageRGBA &in;
  const Animation::Options opt;

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


  static inline uint32_t MapAlpha(uint32_t c) {
    // We expect the image to be a small number of colors,
    // but we can handle "anti-aliased" pixels by snapping
    // them to a nearby color.

    // Pixels with alpha value less than or equal to this amount are
    // treated as fully transparent.
    static constexpr uint8_t MIN_ALPHA = 0x07;

    const uint8_t a = c & 0xFF;
    if (a <= MIN_ALPHA) {
      // "fully" transparent; ignore colors.
      return 0x00000000;
    } else {
      return c | 0xFF;
    }
  }

  void Prep() {

    Timer posterize_timer;

    // For alpha, we have only FF and 00 (with r=g=b=0 as well) here.
    std::unordered_map<uint32_t, int64_t> color_counts;
    int64_t opaque_pixels = 0;
    for (int y = 0; y < in.Height(); y++) {
      for (int x = 0; x < in.Width(); x++) {
        const uint32_t c = MapAlpha(in.GetPixel32(x, y));
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

    // Very small components should be treated as ambiguous, as it
    // is usually better to snap them to adjacent components.
    std::unordered_set<int> fragile_idx;
    Timer fragile_timer;
    if (opt.max_fragile_piece_size > 0) {
      // already-computed connected components. Not color values: Each
      // pixel has the size of its connected component (or a lower
      // bound, if it reaches max_fragile_piece_size), or 0 if it has
      // not yet been computed. (Every actual component size is at
      // least 1, the pixel itself).
      ImageRGBA connected(in.Width(), in.Height());
      connected.Clear32(0);

      auto GetConnectedSize =
        [this, &connected](int xx, int yy, uint32_t c) -> int {

          // Memoized.
          uint32_t already = connected.GetPixel32(xx, yy);
          if (already > 0) return already;

          // A considered pixel has the correct color value.
          // We don't actually use the memoized values here because
          // it's hard to avoid double counting. But we could at
          // least use them if we see a value >= max_fragile_piece_size.
          const int start_idx = Idx(xx, yy);
          std::unordered_set<int> considered = {start_idx};
          std::vector<int> todo = {start_idx};
          auto Consider = [this, &considered, &todo, c](int x, int y) {
              const int idx = Idx(x, y);
              // Already counted.
              if (considered.contains(idx))
                return 0;

              // The border doesn't match anything.
              if (x < 0 || y < 0 || x >= in.Width() || y >= in.Height())
                return 0;

              // Must match the color exactly.
              uint32_t cc = MapAlpha(in.GetPixel32(x, y));
              if (c == cc) {
                considered.insert(idx);
                todo.push_back(idx);
                return 1;
              }
              return 0;
            };

          int count = 1;
          while (!todo.empty()) {
            const auto &[x, y] = UnIdx(todo.back());
            todo.pop_back();

            // Try all its neighbors.
            count += Consider(x - 1, y);
            count += Consider(x + 1, y);
            count += Consider(x, y - 1);
            count += Consider(x, y + 1);

            if (count > opt.max_fragile_piece_size) {
              break;
            }
          }

          // Now we have the answer. Memoize it in all the
          // pixels we considered.
          for (int idx : considered) {
            const auto &[x, y] = UnIdx(idx);
            connected.SetPixel32(x, y, count);
          }

          return count;
        };

      ImageRGBA fragile_viz(connected.Width(), connected.Height());
      fragile_viz.Clear32(0);
      for (int y = 0; y < in.Height(); y++) {
        for (int x = 0; x < in.Width(); x++) {
          const uint32_t c = MapAlpha(in.GetPixel32(x, y));

          // See if the component is fragile.
          int size = GetConnectedSize(x, y, c);
          if (size <= opt.max_fragile_piece_size) {
            fragile_idx.insert(Idx(x, y));
            fragile_viz.SetPixel32(x, y, 0xFFFFFFFF);

            // Since the pixel was removed from all potential regions,
            // decrease its count. This can sometimes bring a color
            // below the threshold because it was all just fringe.
            color_counts[c]--;
          }
        }
      }

      // XXX!
      if (DEBUG_ANIMATION) {
        ImageRGBA connected_viz(connected.Width(), connected.Height());
        for (int y = 0; y < connected.Height(); y++) {
          for (int x = 0; x < connected.Width(); x++) {
            uint8_t v = std::min(connected.GetPixel32(x, y) * 8,
                                 (uint32_t)0xFF);
            connected_viz.SetPixel(x, y, v, v, v, 0xFF);
          }
        }
        connected_viz.Save("connected.png");
        fragile_viz.Save("fragile.png");
        printf("Wrote conneted.png and fragile.png\n");
      }
    }
    const double fragile_seconds = fragile_timer.Seconds();
    if (opt.verbosity > 0) {
      printf("There are %d/%d fragile pixels.\n",
             (int)fragile_idx.size(),
             (int)(in.Width() * in.Height()));
    }

    // Create the regions.
    for (int y = 0; y < in.Height(); y++) {
      for (int x = 0; x < in.Width(); x++) {
        const uint32_t c = MapAlpha(in.GetPixel32(x, y));
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

    // Now posterize the image. We use this to indicate regions of
    // the original image; it is not output directly.
    poster.reset(new ImageRGBA(in.Width(), in.Height()));

    // First pass populates all the pixels that are unambiguous,
    // like those that match a region exactly.
    std::vector<std::pair<int, int>> ambiguous_xy;
    for (int y = 0; y < in.Height(); y++) {
      for (int x = 0; x < in.Width(); x++) {
        const int idx = y * in.Width() + x;
        const uint32_t original_c = in.GetPixel32(x, y);
        const uint32_t c = MapAlpha(original_c);
        if (c & 0xFF) {
          // Opaque
          if (color_counts[c] >= min_pixels &&
              !fragile_idx.contains(idx)) {
            regions[c].pixels.insert(idx);
            poster->SetPixel32(x, y, c);
          } else {
            // Handle these on the second pass.
            ambiguous_xy.emplace_back(x, y);
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

    if (opt.verbosity > 0) {
      printf("There are %d/%d ambiguous pixels.\n",
             (int)ambiguous_xy.size(),
             (int)(in.Width() * in.Height()));
    }


    if (DEBUG_ANIMATION) {
      ImageRGBA ambiguous_viz(in.Width(), in.Height());
      ambiguous_viz.Clear32(0x00000000);
      for (const auto &[x, y] : ambiguous_xy) {
        ambiguous_viz.SetPixel32(x, y, 0xFF0000FF);
      }
      ambiguous_viz.Save("ambiguous.png");
      printf("Wrote ambiguous.png\n");
    }

    // Now, a second pass for the ambiguous colors.

    // Get the region (by color id) of the closest color for this
    // pixel.
    auto ClosestColor = [this, &fragile_idx](int x, int y, uint32_t c) {
        // If the pixel is part of a fragile region, we don't
        // care about the delta-e threshold; we always just snap it
        // to the best adjacent region if we can.
        const bool is_fragile = fragile_idx.contains(Idx(x, y));

        // Compute Delta-E for each region.

        // Of all regions.
        uint32_t best_c = 0x00000000;
        double best_de = 10000.0;

        // Of adjacent regions.
        uint32_t best_adjacent_c = 0x00000000;
        double best_adjacent_de = 10001.0;

        // Is the region adjacent to the pixel, only considering
        // non-fragile pixels?
        auto IsAdjacent = [this, &fragile_idx, x, y](const Region &region) {
            for (int dy : {-1, 0, 1}) {
              int yy = y + dy;
              for (int dx : {-1, 0, 1}) {
                int xx = x + dx;
                const int idx = Idx(xx, yy);
                if (!fragile_idx.contains(idx) &&
                    region.pixels.contains(idx)) {
                  return true;
                }
              }
            }
            return false;
          };

        const auto &[rr, gg, bb, aa_] = ColorUtil::U32ToFloats(c);
        const auto &[l, a, b] = ColorUtil::RGBToLAB(rr, gg, bb);
        for (const auto &[cc, region] : regions) {
          // Don't allow selecting transparent.
          if (cc & 0xFF) {
            double de =
              ColorUtil::DeltaE(region.l, region.a, region.b, l, a, b);
            if (de < best_de) {
              best_c = cc;
              best_de = de;
            }

            if ((de <= opt.adjacent_deltae_threshold || is_fragile) &&
                de < best_adjacent_de && IsAdjacent(region)) {
              best_adjacent_c = cc;
              best_adjacent_de = de;
            }

          }
        }

        if (best_c == 0) {
          for (const auto &[cc, region] : regions) {
            double de = ColorUtil::DeltaE(region.l, region.a, region.b,
                                          l, a, b);
            printf("#%08x (%s): %.6f\n", cc, ColorSwatch(cc).c_str(),
                   de);
          }
          LOG(FATAL) << "No color was found for " <<
            StringPrintf("#%08x", c) << " at " <<
            x << "," << y;
        }

        // If any adjacent pixel was close enough, use the best one.
        if (best_adjacent_c) {
          return best_adjacent_c;
        } else {
          return best_c;
        }
      };


    for (const auto &[x, y] : ambiguous_xy) {
      int idx = y * in.Width() + x;
      const uint32_t original_c = in.GetPixel32(x, y);
      const uint32_t c = MapAlpha(original_c);
      // Find the closest color.
      uint32_t cc = ClosestColor(x, y, c);
      CHECK(regions.contains(cc));
      regions[cc].pixels.insert(idx);
      poster->SetPixel32(x, y, cc);
    }

    if (opt.verbosity > 0) {
      printf("Posterized in %s (%s fragile)\n",
             ANSI::Time(posterize_timer.Seconds()).c_str(),
             ANSI::Time(fragile_seconds).c_str());
    }

  }

  int Idx(int x, int y) const {
    return y * in.Width() + x;
  }
  std::pair<int, int> UnIdx(int idx) const {
    return std::make_pair(idx % in.Width(), idx / in.Width());
  }

  const ImageRGBA &GetPoster() const override {
    CHECK(poster.get() != nullptr) << "Need to call Animate first.";
    return *poster;
  }

  std::vector<ImageRGBA> Animate() override {
    Prep();

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
    layers.reset(new ImageA(in.Width(), in.Height()));
    for (int y = 0; y < in.Height(); y++) {
      for (int x = 0; x < in.Width(); x++) {
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
    // PERF: Can run these in parallel.
    for (int z = 0; z < (int)layer_colors.size(); z++) {
      SmoothRegion("animation-debug", z, opt.smooth_passes);
    }

    ImageRGBA frame(in.Width(), in.Height());

    std::vector<ImageRGBA> all_frames;

    int layer_num = 0;
    for (uint32_t color : layer_colors) {
      CHECK(regions.contains(color));
      // Blit all at once.

      DrawLayer(&frame, layer_num, regions[color], &all_frames);
      layer_num++;
    }

    // Draw (non-empty) transparent pixels last.
    DrawLayer(&frame, layer_num, transparent, &all_frames);

    all_frames.push_back(frame);
    return all_frames;
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
    // Clip to image.
    const int xmin = std::max(0, x - radius);
    const int ymin = std::max(0, y - radius);
    const int xmax = std::min(in.Width() - 1, x + radius);
    const int ymax = std::min(in.Height() - 1, y + radius);

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
  void SmoothRegion(const std::string file_base_out, int z,
                    int max_passes) {
    Timer smooth_timer;
    CHECK(z >= 0 && z < (int)layer_colors.size());
    const uint32_t color = layer_colors[z];
    CHECK(regions.contains(color));
    Region &region = regions[color];

    // Use rasterized region.
    Image1 raster(in.Width(), in.Height());
    raster.Clear();
    for (int idx : region.pixels) {
      const auto &[x, y] = UnIdx(idx);
      raster.SetPixel(x, y, true);
    }

    auto SaveRaster = [&file_base_out, z, color](const Image1 &rast, int p) {
        std::string file = StringPrintf("%s-region.%d.%d.png",
                                        file_base_out.c_str(),
                                        z, p);
        // const auto &[r, g, b, a_] = ColorUtil::Unpack32(color);
        rast.MonoRGBA(color | 0xFF).Save(file);
        printf("Wrote %s\n", file.c_str());
      };

    if (DEBUG_ANIMATION) {
      SaveRaster(raster, 0);
    }

    if (opt.verbosity > 0) {
      printf("Smooth layer %d (%s):\n",
             z, ColorSwatch(color).c_str());
    }
    Periodically status_per(1.0, false);
    Timer timer;
    int kernel_radius = 0;
    for (int pass = 0; pass < max_passes; pass++) {
      if (kernel_radius == 0) kernel_radius = 1;
      else if (kernel_radius == 1) kernel_radius = 2;
      else kernel_radius *= 1.5;
      // TODO: Can I fiddle with this so that blobs don't always grow??
      // static constexpr int KERNEL_RADIUS = 18;
      // We fill the center of the kernel only. Otherwise there is
      // essentially always a way of extending a blob by placing the
      // circle only ever-so-slightly off its edge.
      const int kernel_fill_radius = kernel_radius >> 1;

      // To avoid weird appearance due to order dependence,
      // we add all the pixels at the end of the pass.
      // ImageA new_raster = raster;

      // We'll write to the pixels from separate threads.
      std::vector<std::atomic<uint8_t>> new_raster(
          raster.Width() * raster.Height());
      for (int m = 0; m < raster.Width() * raster.Height(); m++)
        new_raster[m].store(0);

      auto SetNewPixel = [&](int x, int y, uint8_t v) {
          new_raster[Idx(x, y)].store(v);
        };
      auto GetNewPixel = [&](int x, int y) -> uint8_t {
          return new_raster[Idx(x, y)].load();
        };

      for (int cy = 0; cy < in.Height(); cy++) {
        // In parallel. This only writes to new_raster, which
        // supports parallel access (to different pixels).
        ParallelComp(
            in.Width(),
            [&](int cx) {
              int count = 0;
              int count_all = 0;
              ForEachPixelInCircle(
                  cx, cy, kernel_radius,
                  [&raster, &count, &count_all](int x, int y) {
                    count_all++;
                    if (raster.GetPixel(x, y)) {
                      count++;
                    }
                  });

              if (count_all > 0) {
                float frac = count / (float)count_all;
                if (frac >= opt.smooth_vote_threshold) {
                  // Then fill it. We only fill inside the hull, though.
                  ForEachPixelInCircle(
                      cx, cy, kernel_fill_radius,
                      [this, z, &SetNewPixel](int x, int y) {
                        if (WillOverwrite(x, y, z)) {
                          SetNewPixel(x, y, 0xFF);
                        }
                      });
                }
              }
            }, 12);

        if (opt.verbosity > 0 && status_per.ShouldRun()) {
          std::string prog =
            ANSI::ProgressBar(cy, in.Height(),
                              StringPrintf("Pass %d/%d",
                                           pass, max_passes),
                              timer.Seconds());
          if (status_per.TimesRun() == 0) {
            // Make room for progress bar.
            printf("\n");
          }
          printf(ANSI_UP "%s\n", prog.c_str());
        }

      }

      // Now any new pixels.
      bool added = false;
      for (int y = 0; y < raster.Height(); y++) {
        for (int x = 0; x < raster.Width(); x++) {
          if (!raster.GetPixel(x, y) &&
              GetNewPixel(x, y) != 0) {
            added = true;
            raster.SetPixel(x, y, true);
          }
        }
      }

      // No more passes are needed, since there will be no more
      // changes. (Just kidding: The kernel grows with each step!)
      if (false && !added) {
        if (opt.verbosity > 0) {
          printf("Reached a fixed point in %d pass(es).\n", pass + 1);
        }
        break;
      }

      // PERF only if debugging is enabled!
      // We use i + 1 so as not to interfere with the original at 0.
      if (DEBUG_ANIMATION) {
        SaveRaster(raster, pass + 1);
      }
    }

    // convert raster back to region. pixels are only added.
    for (int y = 0; y < raster.Height(); y++) {
      for (int x = 0; x < raster.Width(); x++) {
        if (raster.GetPixel(x, y)) {
          region.pixels.insert(Idx(x, y));
        }
      }
    }

    if (opt.verbosity > 0) {
      printf("Smoothed in %s\n", ANSI::Time(smooth_timer.Seconds()).c_str());
    }
  }

  enum class PenShape {
    SQUARE,
    CIRCLE,
  };

  float ComputePenRadius(int z) const {
    Timer pen_size_timer;
    // To figure out what pen size to use, we compute the average
    // distance to a pixel that's not in the region. We do this with a
    // linear time Voronoi rasterization, which gives us the distance
    // from each pixel (in the region) to the closest pixel that's not
    // in the region.
    CHECK(z >= 0 && z < (int)layer_colors.size());
    const uint32_t color = layer_colors[z];
    auto rit = regions.find(color);
    CHECK(rit != regions.end());
    const Region &region = rit->second;

    // As indices into the original image.
    std::vector<int> sample;

    // But since the distance field calculation is somewhat expensive,
    // we only compute it for the bounding box containing the region.
    // This is often much smaller than the whole image.
    IntBounds bbox;
    for (const int idx : region.pixels) {
      const auto &[x, y] = UnIdx(idx);
      bbox.Bound(x, y);
    }

    if (bbox.Empty()) return opt.min_pen_radius;

    // Include a single-pixel border around the region.
    bbox.AddMargin(1);

    // Now a one-pixel bitmap for the bounding box.
    Image1 bitmap(bbox.Width(), bbox.Height());

    bitmap.Clear(true);

    // The bounding box is generally not located at 0,0.
    const int xoff = bbox.MinX();
    const int yoff = bbox.MinY();
    for (const int idx : region.pixels) {
      const auto &[x, y] = UnIdx(idx);
      bitmap.SetPixel(x - xoff, y - yoff, false);
      sample.push_back(idx);
    }

    CHECK(!sample.empty()) << "Otherwise, the bounding box would "
      "also have been empty.";

    Timer field_timer;
    ImageF distance_field = IntegerVoronoi::DistanceField(bitmap);
    const double field_seconds = field_timer.Seconds();

    double total_dist = 0.0;
    int num_samples = 0;

    // (XXX Seems like this could just be a sum over everything at this
    // point!)
    // Now do some samples.
    ArcFour rc(StringPrintf("layer%d", z));
    Shuffle(&rc, &sample);


    static constexpr int MAX_SAMPLES = 2000;
    for (int i = 0; i < (int)sample.size() && i < MAX_SAMPLES; i++) {
      const auto &[x, y] = UnIdx(sample[i]);

      const float dist = distance_field.GetPixel(x - xoff, y - yoff);
      total_dist += dist;
      num_samples++;
    }

    float pen_size =
      std::clamp((float)(total_dist / num_samples),
                 opt.min_pen_radius, opt.max_pen_radius);

    if (opt.verbosity > 0) {
      printf("Pen size for layer %d (%s) is %.3f (%s field; %s all)\n",
             z,
             ColorSwatch(color).c_str(),
             pen_size,
             ANSI::Time(field_seconds).c_str(),
             ANSI::Time(pen_size_timer.Seconds()).c_str());
    }

    return pen_size;
  }

  static std::string ColorSwatch(uint32_t color) {
      const auto &[r, g, b, a_] = ColorUtil::Unpack32(color);
    return StringPrintf("%s██" ANSI_RESET,
                        ANSI::ForegroundRGB(r, g, b).c_str());
  }

  void DrawLayer(ImageRGBA *frame,
                 int z,
                 const Region &region,
                 std::vector<ImageRGBA> *frames_out) {

    // The radius is half the side length for square pens.
    const float PEN_RADIUS =
      z < (int)layer_colors.size() ? ComputePenRadius(z) : 6.0f;

    // Make this configurable somehow.
    const PenShape PEN_SHAPE =
      PEN_RADIUS > 10.0f ? PenShape::CIRCLE :
      PenShape::SQUARE;

    std::shared_ptr<std::vector<ImageRGBA>> frames_to_blend;

    const float PEN_SEARCH_RADIUS = 1.0f +
      (PEN_SHAPE == PenShape::CIRCLE ? PEN_RADIUS :
       (float) PEN_RADIUS * std::numbers::sqrt2);

    // This used to write asynchronously to disk, but now we
    // return the frames in a vector. Since the frames may
    // finish out-of-order, we write them to this vector
    // with their indices.
    std::mutex m;
    std::vector<std::pair<int, ImageRGBA>> numbered_frames;

    Asynchronously async(16);
    Periodically status_per(1.0);

    int layer_frames = 0;
    auto EmitFrames = [this,
                       &m, &numbered_frames,
                       &frames_to_blend, &async,
                       &layer_frames]() {
        if (frames_to_blend.get() == nullptr) return;

        async.Run(
            [this,
             &m, &numbered_frames,
             fnum = frame_num,
             fr = frames_to_blend]() {
              // Blend!

              CHECK(!fr->empty());

              std::vector<uint32_t> colors(fr->size());
              auto AllSame = [&colors]() {
                  uint32_t c = colors[0];
                  for (int i = 1; i < (int)colors.size(); i++) {
                    if (colors[i] != c) return false;
                  }
                  return true;
                };
              auto BlendColor = [&colors, &AllSame]() -> uint32_t {
                  // Due to the nature of these animations, the
                  // vast majority of the time, all the blended
                  // frames will have the same exact color value
                  // at a pixel.
                  if (AllSame()) return colors[0];
                  // Otherwise, compute the blended color, favoring
                  // quality!

                  // total values in L*A*B* color space, where linear
                  // interpolation (i.e. average) is perceptual. The
                  // totals are alpha weighted.
                  double ll = 0.0, aa = 0.0, bb = 0.0;

                  double total_alpha = 0.0;
                  for (uint32_t c : colors) {
                    const auto &[r, g, b, a] = ColorUtil::U32ToFloats(c);
                    const auto &[L, A, B] = ColorUtil::RGBToLAB(r, g, b);
                    total_alpha += a;
                    ll += L * a;
                    aa += A * a;
                    bb += B * a;
                  }

                  if (total_alpha == 0.0) {
                    // Every pixel was totally transparent. In this case
                    // we output a black transparent pixel for uniformity.
                    return 0x00000000;
                  } else {
                    ll /= total_alpha;
                    aa /= total_alpha;
                    bb /= total_alpha;
                    // Now back to RGB.
                    const auto &[newr, newg, newb] =
                      ColorUtil::LABToRGB(ll, aa, bb);
                    double newalpha = total_alpha / colors.size();
                    return ColorUtil::FloatsTo32(newr, newg, newb, newalpha);
                  }

                };

              ImageRGBA frame(in.Width(), in.Height());
              for (int y = 0; y < in.Height(); y++) {
                for (int x = 0; x < in.Width(); x++) {
                  for (int i = 0; i < (int)fr->size(); i++) {
                    colors[i] = (*fr)[i].GetPixel32(x, y);
                  }
                  frame.SetPixel32(x, y, BlendColor());
                }
              }

              {
                std::unique_lock ml(m);
                numbered_frames.emplace_back(fnum, std::move(frame));
              }
            });

        frames_to_blend.reset();
        frame_num++;
        layer_frames++;
      };

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
    if (opt.verbosity > 0) {
      printf("Built tree in %s\n", ANSI::Time(sec).c_str());
    }

    while (!remaining.Empty()) {

      for (int t = 0; t < opt.timesteps_per_frame; t++) {
        if (remaining.Empty()) break;

        // Move towards the closest pixel.
        const auto &[pos, c_, dist] = remaining.Closest(std::make_pair(cx, cy));
        const auto &[px, py] = pos;
        float vx = px - cx, vy = py - cy;
        // TODO: We want to ensure we make progress, so we should never
        // orbit a point. One simple way to do this would be to increase the
        // acceleration on each frame where we are not consuming pixels.

        cdx = std::lerp(cdx, vx,
                        opt.pen_acceleration / opt.timesteps_per_frame);
        cdy = std::lerp(cdy, vy,
                        opt.pen_acceleration / opt.timesteps_per_frame);
        float norm = std::sqrt(cdx * cdx + cdy * cdy);
        if (norm > opt.max_pen_velocity) {
          cdx /= (norm / opt.max_pen_velocity);
          cdy /= (norm / opt.max_pen_velocity);
        }

        cx += (cdx / opt.timesteps_per_frame);
        cy += (cdy / opt.timesteps_per_frame);

        auto InsidePen = [PEN_RADIUS, PEN_SHAPE, cx, cy](float x, float y) {
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

        // TODO:

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
        std::vector<std::pair<int, int>> stray_ink;
        constexpr bool stray_ink_posterized = true;
        for (const auto &[x, y] : inside) {
          if (WillOverwrite(x, y, z)) {
            // We can't use the actual color, since this pixel is not
            // in the original region. So we use the posterized color.
            uint32_t c = layer_colors[z];
            if (stray_ink_posterized) {
              frame->SetPixel32(x, y, c);
            } else {
              // Count this, but don't overweight it.
              ink_count[c] = 1;
              stray_ink.emplace_back(x, y);
            }
          } else {
            // If this is a final pixel (nothing will overwrite it)
            // then use its final actual color.
            uint32_t c = in.GetPixel32(x, y);
            frame->SetPixel32(x, y, c);
            ink_count[c]++;
          }
          CHECK(remaining.Remove(x, y));
        }

        if (!stray_ink.empty()) {
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

          for (const auto &[x, y] : stray_ink) {
            frame->SetPixel32(x, y, freq_color);
          }
        }

      }

      // And draw the brush itself, but on a temporary copy of the
      // frame. We need the copy to write asynchronously anyway.
      // TODO: Draw different pen shapes?
      // TODO: Thicken the cursor?
      {
        if (frames_to_blend.get() == nullptr)
          frames_to_blend = std::make_shared<std::vector<ImageRGBA>>();

        ImageRGBA frame_copy = *frame;
        frame_copy.BlendThickCircle32(cx, cy,
                                      PEN_RADIUS + 1.0f,
                                      4.0f,
                                      0xFFFFFFAA);
        frame_copy.BlendThickCircle32(cx, cy,
                                      PEN_RADIUS + 1.0f,
                                      2.0f,
                                      0x000000AA);
        frames_to_blend->emplace_back(std::move(frame_copy));
      }

      CHECK(frames_to_blend.get() != nullptr);
      if ((int)frames_to_blend->size() == opt.blend_frames) {
        EmitFrames();

        if (opt.verbosity > 0 && status_per.ShouldRun()) {
          const int pixels_done = start_pixels - remaining.Size();
          std::string prog =
            ANSI::ProgressBar(pixels_done, start_pixels,
                              StringPrintf("Layer %d/%d",
                                           z + 1,
                                           (int)layer_colors.size()),
                              timer.Seconds());
          printf(ANSI_UP "%s\n", prog.c_str());
        }
      }
    }

    // If we have an incomplete blending batch, emit what we have.
    EmitFrames();

    std::sort(numbered_frames.begin(),
              numbered_frames.end(),
              [](const auto &a, const auto &b) {
                return a.first < b.first;
              });

    for (auto &[num_, f] : numbered_frames)
      frames_out->push_back(std::move(f));

    if (opt.verbosity > 0) {
      printf("Finished layer in %d frames, %s.\n", layer_frames,
             ANSI::Time(timer.Seconds()).c_str());
    }
  }

};

}  // namespace

Animation::Animation() {}
Animation::~Animation() {}

Animation *Animation::Create(const ImageRGBA &image_in,
                             const Animation::Options &opt) {
  return new AnimationImpl(image_in, opt);
}
