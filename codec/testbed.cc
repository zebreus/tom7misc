
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "color-util.h"
#include "image.h"
#include "timer.h"
#include "util.h"
#include "zip.h"
#include "png.h"

#include "opt/optimizer.h"

// Assumptions:
//   All frames are a fixed size.
//   That size is at least 1x1.
//   Pixel indices fit in int.
//   Width and height fit in 16 bits each.
// TODO: Can relax some of these with varint representations.

// TODO: Quadtree could actually be octree, using time dimension?

// Tuning run crashed (?), but this was good: 35 39 1.04 2.05
// It looks like setting the quadtree cost quite high is the
// cause?

// Rectangles smaller than this must be encoded as solid (fg or bg).
static int QUADTREE_MIN_RECT_PIXELS = 208;

// The minimum (background) run size that we try to encode. We can always
// treat background pixels as foreground in order to make longer runs.
static int RLE_MIN_BACKGROUND_RUN = 42;

// There are only three possibilities for nodes (0, 1, 2), plus
// we get compression. So estimate this as one bit per node.
static double QUADTREE_BYTES_PER_NODE = 0.80;

// When scoring a mask, we give it credit for covering more background
// pixels, since these do not need to be encoded otherwise. This is
// the estimated number of bytes used to encode each background pixel.
static double BYTES_PER_BACKGROUND_PIXEL = 0.02;

// This works with an unsigned byte stream, with a preference towards
// smaller byte values. The idea is that the generic compressor (e.g.
// flate) expects octet-aligned input, and can encode the bytes
// more efficiently (e.g. using Huffman encoding) than we can by
// trying to do some bit packing. This works better when the
// distribution of bytes is not uniform, so when we can, we try to
// make bytes smaller.

enum PixelFormat : uint8_t {
  PIXELS_RGBA = 0,
  PIXELS_RGB = 1,
  PIXELS_PALETTE = 2,
};

enum MaskType : uint8_t {
  MASK_ALL = 0,
  MASK_RECT = 1,
  MASK_RLE = 2,
  MASK_QUADTREE = 3,
};

static const char *PixelFormatString(PixelFormat p) {
  switch (p) {
  case PIXELS_RGBA: return "RGBA";
  case PIXELS_RGB: return "RGB";
  case PIXELS_PALETTE: return "PALETTE";
  default: return "BAD_PIXELFORMAT";
  }
}

static std::string ColorString(uint32_t c) {
  const auto &[r, g, b, a] = ColorUtil::Unpack32(c);
  return std::format("#{:02x}{:02x}{:02x}{:02x}", r, g, b, a);
}

// Output buffer; defaulting to big-endian numbers.
struct Buf {
  void WB(const std::initializer_list<uint8_t> &bs) {
    for (uint8_t b : bs) bytes.push_back(b);
  }

  void W8(uint8_t b) {
    bytes.push_back(b);
  }

  void W16(uint16_t w) {
    W8(0xFF & (w >> 8));
    W8(0xFF & w);
  }

  void W32(uint32_t w) {
    W8(0xFF & (w >> 24));
    W8(0xFF & (w >> 16));
    W8(0xFF & (w >>  8));
    W8(0xFF & (w >>  0));
  }

  // Writes only the rgb components (high 3 bits) of the
  // color.
  void WRGB(uint32_t color) {
    W8(0xFF & (color >> 24));
    W8(0xFF & (color >> 16));
    W8(0xFF & (color >>  8));
  }

  void AddBuf(const Buf &other) {
    for (uint8_t b : other.bytes) {
      bytes.push_back(b);
    }
  }

  size_t Size() const {
    return bytes.size();
  }

  std::vector<uint8_t> bytes;
};

struct ColorStats {
  // The number of times the entry appears. This is only present
  // during encoding. (Maybe make this a separate PaletteStats?)
  std::unordered_map<uint32_t, int> counts;
  // True if any color has alpha other than 0xFF.
  bool any_alpha = false;

  static constexpr int MAX_NUM_FREQ = 4;
  // Only consider colors to be "frequent" if they cover at least
  // this fraction of the image.
  static constexpr float MIN_FRAC_TO_BE_FREQUENT = 0.01f;
  std::vector<std::pair<uint32_t, int>> most_frequent;

  ColorStats(const ImageRGBA &img) {
    for (int y = 0; y < img.Height(); y++) {
      for (int x = 0; x < img.Width(); x++) {
        uint32_t c = img.GetPixel32(x, y);
        counts[c]++;
        if (c != 0xFF) any_alpha = true;
      }
    }

    const int min_c = std::ceil(MIN_FRAC_TO_BE_FREQUENT *
                                img.Width() *
                                img.Height());

    // PERF don't need to sort everything if only
    // getting the top 4!
    most_frequent.reserve(counts.size() / 2);
    for (const auto &[c, count] : counts) {
      if (count >= min_c) {
        most_frequent.emplace_back(c, count);
      }
    }

    // Sort by decreasing frequency.
    std::sort(most_frequent.begin(), most_frequent.end(),
              [](const auto &a, const auto &b) {
                if (a.second == b.second) {
                  // Break ties arbitrarily.
                  return a.first < b.first;
                } else {
                  return a.second > b.second;
                }
              });

    if (most_frequent.size() > MAX_NUM_FREQ) {
      most_frequent.resize(MAX_NUM_FREQ);
    }
  }
};

// A bijection between uint32_t and uint8_t, favoring small indices.
struct Palette {

  // The RGBA colors in the palette. Distinct. At most 256 entries.
  std::vector<uint32_t> entries;
  // Inverse.
  std::unordered_map<uint32_t, int> indices;

  // Returns pixelformat. Initializes the palette arg if that
  // format is PALETTE.
  static PixelFormat MakePalette(const ImageRGBA &img, Palette *p) {
    // RGBA -> count
    // PERF: If we just used this for the palette, we can return
    // early when we find we have too many colors (and have alpha).
    // But we also use this for mask generation.
    ColorStats color_stats(img);

    if (color_stats.counts.size() > 256) {
      return color_stats.any_alpha ? PIXELS_RGBA : PIXELS_RGB;
    }

    // Otherwise, create the palette.
    std::vector<std::pair<uint32_t, int>> cv;
    cv.reserve(color_stats.counts.size());
    for (const auto &[color, count] : color_stats.counts)
      cv.emplace_back(color, count);
    // Sort by decreasing frequency, so that we use lower byte
    // values for more common colors.
    std::sort(cv.begin(), cv.end(),
              [](const auto &a, const auto &b) {
                if (a.second == b.second) {
                  // Break ties arbitrarily.
                  return a.first < b.first;
                } else {
                  return a.second > b.second;
                }
              });
    const size_t num_entries = cv.size();
    p->entries.resize(num_entries);
    p->indices.clear();
    p->indices.reserve(num_entries);
    for (size_t i = 0; i < num_entries; i++) {
      const auto &[color, count] = cv[i];
      p->entries[i] = color;
      p->indices[color] = (int)i;
    }

    return PIXELS_PALETTE;
  }
};

// A mask is a set of pixels, typically in some spatially
// dense region.
// TODO: Consider boolean operators on masks...
struct AllMask {
  int width = 0;
  int height = 0;
};

struct RectMask {
  int x = 0, y = 0;
  int width = 0, height = 0;
};

struct RLEMask {
  int width = 0, height = 0;
  // Runs alternate "off" (background) then "on" (foreground).
  // At most 2^16 entries.
  std::vector<int> runs;
};


struct Rect {
  int x = 0, y = 0, w = 0, h = 0;

  int Size() const {
    return w * h;
  }
};



struct QuadNode {
  // Either four children (maybe null), or no children but is_all_on.
  std::vector<std::shared_ptr<QuadNode>> children;
  bool is_all_on = false;
};
struct QuadTreeMask {
  int width = 0, height = 0;
  // Describing the rectangle 0,0,width,height.
  std::shared_ptr<QuadNode> root;

  // Split the rectangle into the four canonical subrectangles
  // (top-left, top-right, bottom-left, bottom-right).
  // Note that rectangles may be empty if the input is small enough.
  static std::tuple<Rect, Rect, Rect, Rect> Split(const Rect &r) {
    const int halfw = r.w >> 1;
    const int halfh = r.h >> 1;
    const int restw = r.w - halfw;
    const int resth = r.h - halfh;
    Rect tl = {.x = r.x, .y = r.y, .w = halfw, .h = halfh };
    Rect tr = {.x = r.x + halfw, .y = r.y, .w = restw, .h = halfh };
    Rect bl = {.x = r.x, .y = r.y + halfh, .w = halfw, .h = resth };
    Rect br = {.x = r.x + halfw, .y = r.y + halfh, .w = restw, .h = resth };
    return std::make_tuple(tl, tr, bl, br);
  }
};

using Mask =
  std::variant<AllMask, RectMask, RLEMask, QuadTreeMask>;

static MaskType MaskType(const Mask &mask) {
  if (const AllMask *a = std::get_if<AllMask>(&mask)) {
    return MASK_ALL;
  } else if (const RectMask *r = std::get_if<RectMask>(&mask)) {
    return MASK_RECT;
  } else if (const RLEMask *r = std::get_if<RLEMask>(&mask)) {
    return MASK_RLE;
  } else if (const QuadTreeMask *q = std::get_if<QuadTreeMask>(&mask)) {
    return MASK_QUADTREE;
  } else {
    LOG(FATAL) << "Bad mask";
    return MASK_ALL;
  }
}

static std::string MaskString(const Mask &mask) {
  if (const AllMask *a = std::get_if<AllMask>(&mask)) {
    return "ALL";
  } else if (const RectMask *r = std::get_if<RectMask>(&mask)) {
    return std::format("RECT(@{},{} {}x{})",
                       r->x, r->y,
                       r->width, r->height);
  } else if (const RLEMask *r = std::get_if<RLEMask>(&mask)) {
    return std::format("RLE({} runs)",
                       r->runs.size());
  } else if (const QuadTreeMask *r = std::get_if<QuadTreeMask>(&mask)) {
    return "QUADTREE";

  } else {
    LOG(FATAL) << "Bad mask";
    return "BAD";
  }
}

// Apply f to the pixel coordinates in the mask, in order.
template<class F>
static void AppMask(const Mask &mask, const F &f) {
  if (const AllMask *a = std::get_if<AllMask>(&mask)) {
    for (int y = 0; y < a->height; y++) {
      for (int x = 0; x < a->width; x++) {
        f(x, y);
      }
    }

  } else if (const RectMask *r = std::get_if<RectMask>(&mask)) {
    for (int y = 0; y < r->height; y++) {
      for (int x = 0; x < r->width; x++) {
        f(r->x + x, r->y + y);
      }
    }

  } else if (const RLEMask *r = std::get_if<RLEMask>(&mask)) {
    bool on = false;
    int idx = 0;
    for (const int run_len : r->runs) {
      if (on) {
        for (int i = 0; i < run_len; i++) {
          // PERF avoid division
          int y = idx / r->width;
          int x = idx % r->width;
          f(x, y);
          idx++;
        }
      } else {
        idx += run_len;
      }
      on = !on;
    }

  } else if (const QuadTreeMask *q = std::get_if<QuadTreeMask>(&mask)) {

    std::function<void (const Rect &, const QuadNode *)> Rec =
      [&Rec, &f](const Rect &r, const QuadNode *node) {
        // Treat null nodes as all empty.
        if (node == nullptr) return;
        if (node->is_all_on) {
          CHECK(node->children.empty());
          for (int y = 0; y < r.h; y++) {
            for (int x = 0; x < r.w; x++) {
              f(r.x + x, r.y + y);
            }
          }
        } else {
          const auto &[tl, tr, bl, br] = QuadTreeMask::Split(r);
          // They can be null, but there must be four of them.
          CHECK(node->children.size() == 4);
          Rec(tl, node->children[0].get());
          Rec(tr, node->children[1].get());
          Rec(bl, node->children[2].get());
          Rec(br, node->children[3].get());
        }
      };

    Rect all{.x = 0, .y = 0, .w = q->width, .h = q->height };
    Rec(all, q->root.get());

  } else {
    LOG(FATAL) << "Bad mask";
  }
}

static int MaskNumPixels(const Mask &mask) {
  if (const AllMask *a = std::get_if<AllMask>(&mask)) {
    return a->width * a->height;

  } else if (const RectMask *r = std::get_if<RectMask>(&mask)) {
    return r->width * r->height;

  } else if (const RLEMask *r = std::get_if<RLEMask>(&mask)) {
    // Only the "on" runs.
    int total = 0;
    for (int i = 1; i < (int)r->runs.size(); i += 2) {
      total += r->runs[i];
    }
    return total;

  } else if (const QuadTreeMask *r = std::get_if<QuadTreeMask>(&mask)) {
    // PERF: This can be faster because we can just add rectangle sizes.
    int total = 0;
    AppMask(mask, [&total](int x, int y) {
        total++;
      });
    return total;

  } else {
    LOG(FATAL) << "Bad mask";
    return 0;
  }
}

// See below, but this requires the background color to be the
// specific color. This function might return the entire frame
// as a rectangle.
static RectMask
GetForegroundRectMaskWithColor(const ImageRGBA &frame,
                               uint32_t bgcolor) {
  const int width = frame.Width(), height = frame.Height();
  int left = 0, top = 0, right = 0, bottom = 0;

  // The order we do this in matters, but a row has to be
  // completely blank in order to crop it out.
  auto SolidRow = [&frame, bgcolor, width](int y) -> bool {
      for (int x = 0; x < width; x++)
        if (frame.GetPixel32(x, y) != bgcolor)
          return false;
      return true;
    };

  while (top < height - 1 && SolidRow(top))
    top++;
  while (bottom < height - top - 1 && SolidRow(height - bottom - 1))
    bottom++;
  const int mheight = height - top - bottom;
  CHECK(mheight >= 1);

  auto SolidCol = [&frame, bgcolor, top, mheight](int x) -> bool {
      for (int y = top; y < top + mheight; y++)
        if (frame.GetPixel32(x, y) != bgcolor)
          return false;
      return true;
    };

  while (left < width - 1 && SolidCol(left))
    left++;
  while (right < width - left - 1 && SolidCol(width - right - 1))
    right++;

  return RectMask{
    .x = left, .y = top,
    .width = width - left - right,
    .height = mheight,
  };
}

// This masks out a region with a solid "background" color.
// The mask will be non-empty. Sets the background color
// if the mask is not degenerate.
static std::optional<RectMask>
GetForegroundRectMask(const ImageRGBA &frame, uint32_t *bgcolor) {
  // Greedy approach that only generates rectangular masks.
  const int width = frame.Width(), height = frame.Height();

  CHECK(width > 0 && height > 0);
  // Can't make a non-degenerate mask.
  if (width == 1 && height == 1) {
    return std::nullopt;
  }

  // The corner-based logic wants nontrivial edges. We could
  // support this with a special case, though.
  if (width == 1 || height == 1) {
    return std::nullopt;
  }


  // In order to shrink an edge, it must be all the same color.
  // That means that there must be at least two corners that
  // are the same color in order to shrink at all.
  //
  //    A----------B
  //    |          |
  //    D----------C

  const uint32_t a = frame.GetPixel32(0, 0);
  const uint32_t b = frame.GetPixel32(width - 1, 0);
  const uint32_t d = frame.GetPixel32(0, width - 1);
  const uint32_t c = frame.GetPixel32(width - 1, height - 1);

  int num_eq = 0;
  // Count equal edges.
  if (a == b) num_eq++;
  if (b == c) num_eq++;
  if (c == d) num_eq++;
  if (d == a) num_eq++;
  // And diagonals.
  if (a == c) num_eq++;
  if (b == d) num_eq++;

  // No way to make a rectangular mask, then.
  if (num_eq == 0) {
    return std::nullopt;
  }

  if (num_eq >= 3) {
    // Only one color will work. We can figure out the color
    // value with some pigeonhole logic:
    const uint32_t color = a == b ? a : c;
    *bgcolor = color;
    return GetForegroundRectMaskWithColor(frame, color);
  }

  if (num_eq == 1) {
    // Only one edge can be cropped.
    // We could have a faster version of the cropping logic in this
    // case, but it's not like this is a performance bottleneck.
    if (a == b) {
      *bgcolor = a;
      return GetForegroundRectMaskWithColor(frame, a);
    } else if (b == c) {
      *bgcolor = b;
      return GetForegroundRectMaskWithColor(frame, b);
    } else if (c == d) {
      *bgcolor = c;
      return GetForegroundRectMaskWithColor(frame, c);
    } else if (d == a) {
      *bgcolor = d;
      return GetForegroundRectMaskWithColor(frame, d);
    } else {
      // One diagonal is equal. Useless.
      return std::nullopt;
    }
  }

  CHECK(num_eq == 2) << num_eq;
  // The only interesting case is that opposing edges would work,
  // but they have different colors, so we can only choose one.
  // An example would be a French flag, where we could choose
  // blue or red to crop out. If the flag is asymmetric, one
  // choice may be better than the other.

  // Either we have (a == b && c == d) or (a == d && b == c).
  // Regardless we can use opposite corners as the two candidates.
  if ((a == b && c == d) || (a == d && b == c)) {
    /*
    std::format("\n"
                "{:08x} {:08x}\n"
                "{:08x} {:08x}\n", a, b, d, c);
    */
    CHECK(a != c);

    // Get two masks and take the smaller one.

    RectMask mask1 = GetForegroundRectMaskWithColor(frame, a);
    RectMask mask2 = GetForegroundRectMaskWithColor(frame, c);
    if (MaskNumPixels(mask1) < MaskNumPixels(mask2)) {
      *bgcolor = a;
      return mask1;
    } else {
      *bgcolor = c;
      return mask2;
    }
  } else {
    // Both diagonals are equal. Useless.
    return std::nullopt;
  }
}

static std::optional<QuadTreeMask>
GetForegroundQuadTreeMask(const ImageRGBA &frame, uint32_t bgcolor) {

  // static_assert(QUADTREE_MIN_RECT_PIXELS >= 1);
  CHECK(QUADTREE_MIN_RECT_PIXELS >= 1);

  Rect all_rect{.x = 0, .y = 0, .w = frame.Width(), .h = frame.Height()};
  // Share one "on" leaf for the quadtree.
  std::shared_ptr<QuadNode> all_on(
      new QuadNode{
        .children = {},
        .is_all_on = true,
      });

  std::function<std::shared_ptr<QuadNode>(const Rect &r)> Rec =
    [&Rec, &frame, &bgcolor, &all_on](const Rect &r) ->
    std::shared_ptr<QuadNode> {

    // To avoid making linear passes over and over to check for
    // solid regions, we generate the full tree and then merge on
    // the way up. PERF: Perhaps we can avoid allocating in sparse
    // situations though?
    if (r.Size() < QUADTREE_MIN_RECT_PIXELS ||
        // Once the rectangles become degenerate we also stop, as
        // these cannot be subdivided. We do need this condition for
        // correctness (otherwise we get endless recursion). In
        // principle we could support 1xN and Nx1 slivers, though, by
        // just generating two children; the decoder would know that
        // it is reaching this situation as well.
        r.w <= 1 || r.w <= 1) {
      // It must be a leaf then. We can always treat bg as fg, so
      // this leaf is empty iff it is all bgcolor.
      const bool all_background = [&]() {
          for (int yy = 0; yy < r.h; yy++) {
            for (int xx = 0; xx < r.w; xx++) {
              const int x = r.x + xx;
              const int y = r.y + yy;
              uint32_t c = frame.GetPixel32(x, y);
              if (c != bgcolor) return false;
            }
          }

          return true;
        }();

      if (all_background) {
        return std::shared_ptr<QuadNode>(nullptr);
      } else {
        return all_on;
      }
    } else {
      // Internal node.

      const auto &[tl, tr, bl, br] = QuadTreeMask::Split(r);
      auto ntl = Rec(tl);
      auto ntr = Rec(tr);
      auto nbl = Rec(bl);
      auto nbr = Rec(br);

      // If all are solid (and the same) then we merge them into
      // a single node.
      if (ntl.get() == nullptr &&
          ntr.get() == nullptr &&
          nbl.get() == nullptr &&
          nbr.get() == nullptr)
        return nullptr;

      // PERF: In the case of three foreground nodes, we might want to
      // just merge this into a single foreground node when
      // they are small? Tune. (Also we could have a single missing node
      // deep in a very large rectangle. Basically we should be comparing
      // the cost to encode the subtree with the accuracy of the mask.)
      if (ntl.get() != nullptr &&
          ntl->is_all_on &&
          ntr.get() != nullptr &&
          ntr->is_all_on &&
          nbl.get() != nullptr &&
          nbl->is_all_on &&
          nbr.get() != nullptr &&
          nbr->is_all_on) {
        // Children are discarded.
        return all_on;
      }

      // Otherwise, a proper internal node with these children.
      return std::shared_ptr<QuadNode>(
          new QuadNode{
            .children = std::vector<std::shared_ptr<QuadNode>>{
              std::move(ntl),
              std::move(ntr),
              std::move(nbl),
              std::move(nbr),
            },
            .is_all_on = false,
          });
    }

  };

  QuadTreeMask mask;
  mask.width = frame.Width();
  mask.height = frame.Height();
  mask.root = Rec(all_rect);
  return mask;
}

static std::optional<RLEMask>
GetForegroundRLEMask(const ImageRGBA &frame, uint32_t *bgcolor) {
  static constexpr int VERBOSE = 0;

  // XXX Why not equal to 1?
  CHECK(RLE_MIN_BACKGROUND_RUN >= 1);
  // static_assert(MIN_BACKGROUND_RUN > 1);

  const int size = frame.Width() * frame.Height();

  CHECK(frame.Width() > 0 && frame.Height() > 0);

  // Find the best "background color." This is the color with the highest
  // scoring runs.
  std::unordered_map<uint32_t, double> run_scores;

  // PERF: Tune.
  // Longer runs are better, so use a little bit of nonlinearity.
  static constexpr auto ScoreLength = [](int run_length) {
      if (run_length <= RLE_MIN_BACKGROUND_RUN) return 0.0;
      double p = (run_length - RLE_MIN_BACKGROUND_RUN);
      return p * std::log2(p);
    };

  {
    // Color of the current run, and the number of pixels in it.
    // This is always at least 1.
    uint32_t prev = ~frame.GetPixel32(0, 0);
    int run_length = 1;

    auto Emit = [&run_scores, &prev, &run_length]() {
        if (run_length >= RLE_MIN_BACKGROUND_RUN) {
          run_scores[prev] += ScoreLength(run_length);
        }
      };

    for (int idx = 0; idx < size; idx++) {
      // PERF: Avoid division.
      int y = idx / frame.Width();
      int x = idx % frame.Width();

      uint32_t cur = frame.GetPixel32(x, y);
      if (VERBOSE > 2) {
        if (cur != 0) printf("%08x\n", cur);
      }

      if (cur == prev) {
        // Run extended.
        run_length++;
      } else {
        Emit();

        prev = cur;
        run_length = 1;
      }
    }
    Emit();
  }

  if (VERBOSE > 1) {
    for (const auto &[c, s] : run_scores) {
      printf("Color #%08x (%s██" ANSI_RESET "): %.3f\n",
             c, ANSI::ForegroundRGB32(c).c_str(), s);
    }
  }

  // All runs were too short.
  if (run_scores.empty()) return std::nullopt;

  double best_score = 0.0;
  uint32_t best_color = 0xD00D00;
  for (const auto &[c, s] : run_scores) {
    if (s > best_score ||
        (s == best_score && c < best_color)) {
      best_color = c;
      best_score = s;
    }
  }

  const uint32_t bg = best_color;
  if (VERBOSE > 0) printf("RLE best bgcolor: #%08x (%.3f)\n", bg, best_score);

  {
    std::vector<int> runs;
    // Now compute the RLE representation with best_color.
    bool in_background = true;
    int run_length = 0;

    for (int idx = 0; idx < size; idx++) {
      // PERF: Avoid division.
      int y = idx / frame.Width();
      int x = idx % frame.Width();

      uint32_t cur = frame.GetPixel32(x, y);

      if (in_background) {
        if (cur == bg) {
          // Simply extend background run.
          run_length++;
        } else {
          if (VERBOSE > 1) {
            printf("At %d,%d with background run of length %d\n",
                   x, y, run_length);
          }
          // We must end the background run no matter what.
          in_background = false;
          // But if the background run is too short, we do this by
          // resuming the previous foreground run.
          if (run_length < RLE_MIN_BACKGROUND_RUN && !runs.empty()) {
            run_length = runs.back() + run_length + 1;
            runs.pop_back();
          } else {
            runs.push_back(run_length);
            run_length = 1;
          }
        }
      } else {
        // Same idea, but we cannot treat foreground as background.
        // So we need to emit all runs, even if they are shorter than
        // we would want.
        if (cur != bg) {
          // Extend the run of foreground pixels.
          run_length++;
        } else {
          if (VERBOSE > 1) {
            printf("At %d,%d with foreground run of length %d\n",
                   x, y, run_length);
          }

          // End foreground run.
          runs.push_back(run_length);
          run_length = 1;
          in_background = true;
        }
      }
    }

    // The final run is implied by the frame size, so we just
    // leave it off.
    if (VERBOSE > 0) {
      printf("%d runs. Last run was %s, of length %d\n",
             (int)runs.size(),
             in_background ? "bg" : "fg", run_length);
    }

    if ((int)runs.size() < 65536) {
      *bgcolor = bg;
      return std::make_optional(RLEMask{
          .width = frame.Width(),
          .height = frame.Height(),
          .runs = std::move(runs),
        });
    }
  }

  return std::nullopt;
}


// Mask out the foreground (against a solid background) if possible.
// Tries to find the best mask (rectangular, rle, ...).
//
// TODO: Easy to allow variants like transposed masks here.
static Mask GetForegroundMask(const ImageRGBA &frame, uint32_t *bgcolor) {
  static constexpr int VERBOSE = 0;

  // PERF pass this?
  ColorStats color_stats(frame);

  struct ScoredMask {
    double score = 0.0;
    Mask mask;
    uint32_t bgcolor = 0;
  };

  std::vector<ScoredMask> candidates;

  auto Score = [&frame](int mask_bytes, int mask_pixels) {
      const int pixels_outside =
        frame.Width() * frame.Height() - mask_pixels;
      return mask_bytes - pixels_outside * BYTES_PER_BACKGROUND_PIXEL;
    };


  // PERF: Could consider trying more than one top color with the
  // quadtree.
  if (!color_stats.most_frequent.empty()) {
    uint32_t quad_bgcolor = color_stats.most_frequent[0].first;
    if (std::optional<QuadTreeMask> maybe_quad_mask =
        GetForegroundQuadTreeMask(frame, quad_bgcolor)) {
      QuadTreeMask quad_mask = std::move(maybe_quad_mask.value());

      const int quad_pixels = MaskNumPixels(quad_mask);
      // Estimate size as number of nodes? Maybe we should just
      // encode it?
      std::function<int(const QuadNode *)> Rec = [&Rec](const QuadNode *n) {
          if (n == nullptr) {
            return 1;
          } else if (n->is_all_on) {
            return 1;
          } else {
            int sum = 1;
            for (const auto &q : n->children) {
              sum += Rec(q.get());
            }
            return sum;
          }
        };

      const int quad_size =
        Rec(quad_mask.root.get()) * QUADTREE_BYTES_PER_NODE;

      candidates.push_back(
          ScoredMask{
            .score = Score(quad_size, quad_pixels),
            .mask = std::move(quad_mask),
            .bgcolor = quad_bgcolor,
          });
    }
  }

  uint32_t rle_bgcolor = 0xCAFED00D;
  if (std::optional<RLEMask> maybe_rle_mask =
      GetForegroundRLEMask(frame, &rle_bgcolor)) {
    RLEMask rle_mask = std::move(maybe_rle_mask.value());
    // The rle mask will always be at least as detailed as the rectangle
    // (if we found the same background color), but it is typically much
    // larger to encode.
    int rle_pixels = MaskNumPixels(rle_mask);
    // Estimate rle as 16-bit size and then 16 bit run lengths.
    int rle_size = 2 + rle_mask.runs.size() * 2;

    if (VERBOSE > 1) {
      printf(APURPLE("RLE") ":  %d pixels, %d size\n", rle_pixels, rle_size);
    }

    candidates.push_back(
        ScoredMask{
          .score = Score(rle_size, rle_pixels),
          .mask = std::move(rle_mask),
          .bgcolor = rle_bgcolor,
        });
  }

  uint32_t rect_bgcolor = 0;
  if (std::optional<RectMask> maybe_rect_mask =
      GetForegroundRectMask(frame, &rect_bgcolor)) {
    RectMask rect_mask = std::move(maybe_rect_mask.value());
    int rect_pixels = MaskNumPixels(rect_mask);
    int rect_size = 4 * 2;

    if (VERBOSE > 1) {
      printf(APURPLE("RECT") ": %d pixels, %d size\n",
             rect_pixels, rect_size);
    }

    candidates.push_back(
        ScoredMask{
          .score = Score(rect_size, rect_pixels),
          .mask = std::move(rect_mask),
          .bgcolor = rect_bgcolor,
        });
  }


  if (VERBOSE > 1) {
    printf(APURPLE("ALL") ":  %d pixels, %d size\n",
           frame.Width() * frame.Height(),
           1);
  }

  candidates.push_back(
      ScoredMask{
        .score = 1,
        .mask = AllMask{
          .width = frame.Width(),
          .height = frame.Height(),
        },
        .bgcolor = 0x00000000,
      });

  std::sort(
      candidates.begin(), candidates.end(),
      [](const ScoredMask &a, const ScoredMask &b) {
        if (a.score == b.score) {
          // Break ties arbitrarily.
          return MaskType(a.mask) < MaskType(b.mask);
        } else {
          return a.score < b.score;
        }
      });

  CHECK(!candidates.empty()) << "Should always have the ALL mask.";

  *bgcolor = candidates.begin()->bgcolor;
  return std::move(candidates.begin()->mask);
}

// XXX: Not used yet! Transition to this please.
// A frame is represented by the Pixel tree data structure.
// This is the in-memory representation, but it also documents
// the serialized format. Deserialization requires knowing
// the number of pixels.
struct SolidPixels;
struct PalettePixels;
struct RGBPixels;
struct RGBAPixels;
struct MaskedPixels;
using Pixels = std::variant<SolidPixels, PalettePixels,
                            RGBPixels, RGBAPixels, MaskedPixels>;

struct SolidPixels {
  // A single repeated color of the given size.
  static constexpr uint8_t TAG = 0x00;
  uint32_t rgba = 0;
};

struct PalettePixels {
  static constexpr uint8_t TAG = 0x01;
  Palette palette;
  std::vector<int> indices;
};

struct RGBPixels {
  static constexpr uint8_t TAG = 0x02;
  // as 0x00RRGGBB.
  std::vector<uint32_t> rgb;
};

struct RGBAPixels {
  static constexpr uint8_t TAG = 0x03;
  // as 0xRRGGBBAA.
  std::vector<uint32_t> rgba;
};

struct MaskedPixels {
  // Could have the tag encode the mask type too?
  static constexpr uint8_t TAG = 0x04;
  Mask mask;
  std::shared_ptr<Pixels> background;
  std::shared_ptr<Pixels> foreground;
};

// This is the parsed in-memory representation, but it also contains
// the reference for the serialized form.
struct IFrameHeader {
  // First byte gives the pixel format.
  PixelFormat pixel_format;

  // If pixel format is PALETTE, then we have a palette. The palette
  // is represented as one size byte, which is the number of palette
  // entries minus one (since there may be 256 entries and cannot be
  // zero). Palette entries are always RGBA.
  std::optional<Palette> palette;

  // The foreground mask.
  Mask mask;
  // If the foreground mask is not ALL, then the background color
  // (in the pixel format).
  uint32_t bgcolor = 0;
};

void RemovePrefix(std::span<uint8_t> *v, size_t n) {
  CHECK(v->size() >= n);
  *v = v->subspan(n);
}

uint8_t Read8(std::span<uint8_t> *v) {
  CHECK(!v->empty());
  uint8_t b = v->front();
  RemovePrefix(v, 1);
  return b;
}

uint16_t Read16(std::span<uint8_t> *v) {
  CHECK(v->size() >= 2);
  uint32_t a = (*v)[0];
  uint32_t b = (*v)[1];
  RemovePrefix(v, 2);
  return (a << 8) | b;
}

uint32_t Read32(std::span<uint8_t> *v) {
  CHECK(v->size() >= 4);
  uint32_t a = (*v)[0];
  uint32_t b = (*v)[1];
  uint32_t c = (*v)[2];
  uint32_t d = (*v)[3];
  RemovePrefix(v, 4);
  return (a << 24) | (b << 16) | (c << 8) | d;
}

// Read a three-byte RGBA color with alpha=0xFF implied.
uint32_t ReadRGB(std::span<uint8_t> *v) {
  CHECK(v->size() >= 3);
  uint32_t a = (*v)[0];
  uint32_t b = (*v)[1];
  uint32_t c = (*v)[2];
  RemovePrefix(v, 3);
  return (a << 24) | (b << 16) | (c << 8) | 0xFF;
}

static bool ReadPalette(Palette *p,
                        std::span<uint8_t> *v) {
  if (v->empty()) return false;
  int num_entries = Read8(v) + 1;
  p->entries.reserve(num_entries);
  if ((int)v->size() < 4 * num_entries) return false;
  for (int i = 0; i < num_entries; i++) {
    p->entries.push_back(Read32(v));
  }

  // PERF: We probably don't need the inverted map for decoding.
  for (int i = 0; i < num_entries; i++) {
    p->indices[p->entries[i]] = i;
  }

  return true;
}

static void DebugParse(const char *what,
                       std::span<uint8_t> *v) {
  printf(AWHITE("%s") ":", what);
  for (int i = 0; i < 16; i++) {
    if (i >= (int)v->size()) {
      printf(ARED(" EOF"));
      break;
    } else {
      printf(" %02x", (*v)[i]);
    }
  }
  printf("\n");
}

static bool ReadMask(int frame_width,
                     int frame_height,
                     Mask *m,
                     std::span<uint8_t> *v) {
  static constexpr int VERBOSE = 0;
  if (VERBOSE > 1) {
    DebugParse("read mask", v);
  }

  if (v->empty()) return false;

  const uint8_t mask_type = Read8(v);
  if (VERBOSE > 0) {
    printf("Got mask type %d\n", mask_type);
  }
  switch (mask_type) {
  case MASK_ALL:
    *m = AllMask{.width = frame_width, .height = frame_height};
    return true;

  case MASK_RECT: {
    if (v->size() < 8) return false;
    uint16_t x = Read16(v);
    uint16_t y = Read16(v);
    uint16_t w = Read16(v);
    uint16_t h = Read16(v);
    *m = RectMask{.x = x, .y = y, .width = w, .height = h};
    return true;
  }

  case MASK_RLE: {
    if (v->size() < 2) return false;
    int num = Read16(v);
    std::vector<int> runs;
    runs.reserve(num);

    if ((int)v->size() < num * 2) return false;
    for (int i = 0; i < num; i++) {
      runs.push_back(Read16(v));
    }
    *m = RLEMask{
      .width = frame_width,
      .height = frame_height,
      .runs = std::move(runs),
    };
    if (VERBOSE > 1) {
      printf("Read RLE mask with %d runs.\n", num);
    }
    return true;
  }

  case MASK_QUADTREE: {

    // Parse tree recursively.
    bool failed = false;
    std::function<std::shared_ptr<QuadNode>()> ParseRec =
      [&v, &failed, &ParseRec]() -> std::shared_ptr<QuadNode> {
        if (failed) return nullptr;

        // Always need one byte to represent a node.
        if (v->empty()) {
          failed = true;
          return nullptr;
        }

        const uint8_t b = Read8(v);
        switch (b) {
        case 0x00:
          // Not an error; an empty subtree is represented
          // as nullptr.
          return nullptr;

        case 0x01:
          return std::shared_ptr<QuadNode>(new QuadNode{
              .children = {},
              .is_all_on = true,
           });

        case 0x02: {
          std::vector<std::shared_ptr<QuadNode>> children;
          children.reserve(4);
          for (int i = 0; i < 4; i++) {
            children.push_back(ParseRec());
            if (failed) return nullptr;
          }
          return std::shared_ptr<QuadNode>(new QuadNode{
              .children = std::move(children),
              .is_all_on = false,
            });
        }

        default:
          failed = true;
          return nullptr;
        }
      };

    std::shared_ptr<QuadNode> root = ParseRec();

    if (failed) return false;
    *m = QuadTreeMask{
      .width = frame_width,
      .height = frame_height,
      .root = std::move(root),
    };
    return true;
  }

  default:
    return false;
  }
}

static inline bool ReadPixel(const IFrameHeader &head,
                             std::span<uint8_t> *v,
                             uint32_t *c) {
  switch (head.pixel_format) {
  case PIXELS_RGBA: {
    if (v->size() < 4) return false;
    *c = Read32(v);
    return true;
  }
  case PIXELS_RGB: {
    if (v->size() < 3) return false;
    *c = ReadRGB(v);
    return true;
  }

  case PIXELS_PALETTE: {
    if (v->empty()) return false;
    uint8_t b = Read8(v);
    CHECK(head.palette.has_value());
    if (b >= head.palette.value().entries.size()) return false;
    *c = head.palette.value().entries[b];
    return true;
  }

  default:
    LOG(FATAL) << "Bad pixelformat";
    return false;
  }
}

static std::optional<IFrameHeader>
ParseIFrameHeader(int frame_width, int frame_height,
                  std::span<uint8_t> *v) {
  if (v->empty()) return std::nullopt;

  IFrameHeader head;
  uint8_t pf = Read8(v);
  switch (pf) {
  case PIXELS_RGBA:
    head.pixel_format = PIXELS_RGBA;
    break;

  case PIXELS_RGB:
    head.pixel_format = PIXELS_RGB;
    break;

  case PIXELS_PALETTE: {
    head.pixel_format = PIXELS_PALETTE;

    // Parse palette in this case.
    head.palette = {Palette()};
    if (!ReadPalette(&head.palette.value(), v)) return std::nullopt;
    break;
  }

  default:
    return std::nullopt;
    break;
  }

  if (!ReadMask(frame_width, frame_height, &head.mask, v)) {
    printf("Failed to read mask\n");
    return std::nullopt;
  }

  switch (MaskType(head.mask)) {
  case MASK_QUADTREE:
  case MASK_RLE:
  case MASK_RECT: {
    uint32_t c = 0;
    if (!ReadPixel(head, v, &c))
      return std::nullopt;
    head.bgcolor = c;
    break;
  }

  case MASK_ALL:
    // Nothing.
    break;

  default:
    return std::nullopt;
  }

  return {std::move(head)};
}

static void WritePalette(const Palette &p, Buf *buf) {
  // Palette entries are always RGBA, although we could evolve this.
  // A full palette is 1 kb + 1 byte.

  // Size.
  CHECK(p.entries.size() > 0 && p.entries.size() <= 256) << "Invalid "
    "palette! " << p.entries.size();
  buf->W8(p.entries.size() - 1);

  for (uint32_t c : p.entries) {
    buf->W32(c);
  }
}

static void WriteMask(const Mask &mask, Buf *buf) {
  static constexpr int VERBOSE = 0;

  if (const AllMask *a = std::get_if<AllMask>(&mask)) {
    buf->W8(MASK_ALL);

  } else if (const RectMask *r = std::get_if<RectMask>(&mask)) {
    buf->W8(MASK_RECT);
    buf->W16(r->x);
    buf->W16(r->y);
    buf->W16(r->width);
    buf->W16(r->height);

  } else if (const RLEMask *r = std::get_if<RLEMask>(&mask)) {
    buf->W8(MASK_RLE);

    // Compute the size (see below).
    int r_size = 0;
    for (int r : r->runs) {
      while (r > 65535) {
        r_size++;
        r_size++;
        r -= 65535;
      }

      r_size++;
    }

    if (VERBOSE > 0) {
      printf("Write RLE: %d runs, but write %d\n",
             (int)r->runs.size(), r_size);
    }

    // Also,
    CHECK(r_size < 65536) << "RLE runs are supposed to have no "
      "more than 65536 entries. There is possibly a bug here where "
      "we had just shy of this many, but had to break some super-long "
      "runs up, and so exceeded the size. Could detect this by breaking "
      "them earlier, or use varint here.";

    buf->W16(r_size);

    for (int r : r->runs) {
      // TODO PERF: Varint would probably be more compact here.

      // We can output a run of N+M equivalently as N,0,M. So we use
      // this whenever the run is more than 16 bits.
      while (r > 65535) {
        buf->W16(0xFFFF);
        // degenerate 0-length run of the opposite polarity
        buf->W16(0x0000);
        r -= 65535;
      }

      buf->W16(r);
    }

  } else if (const QuadTreeMask *r = std::get_if<QuadTreeMask>(&mask)) {
    buf->W8(MASK_QUADTREE);

    // PERF: We should consider a DAG-style representation, since
    // there may be subtrees that are equal. On the other hand,
    // repeated subtrees should compress well.

    // Tree in prefix form. 0 represents an all-off leaf, and 1 an all-on leaf.
    // 2 is an internal node, followed by four nodes.
    std::function<void(const QuadNode *)> WriteRec =
      [buf, &WriteRec](const QuadNode *node) {
        if (node == nullptr) {
          buf->W8(0);
        } else {
          if (node->is_all_on) {
            buf->W8(1);
          } else {
            // An alternative encoding here would give the bitmask
            // of nodes that are non-zero?
            buf->W8(2);
            CHECK(node->children.size() == 4);
            WriteRec(node->children[0].get());
            WriteRec(node->children[1].get());
            WriteRec(node->children[2].get());
            WriteRec(node->children[3].get());
          }
        }
      };

    WriteRec(r->root.get());

  } else {
    LOG(FATAL) << "Bad mask";

  }
}

static std::string PaletteString(const Palette &p) {
  std::string ret;
  for (int i = 0; i < p.entries.size(); i++) {
    uint32_t c = p.entries[i];
    auto it = p.indices.find(c);
    CHECK(it != p.indices.end()) << "palette index is wrong";
    CHECK(it->second == i) << "palette index is wrong";
    StringAppendF(&ret, "%08x=%s██" ANSI_RESET, c,
                  ANSI::ForegroundRGB32(c).c_str());
  }
  return ret;
}

struct Encode {
  int frame_width = 0;
  int frame_height = 0;

  Encode(int frame_width, int frame_height) :
    frame_width(frame_width), frame_height(frame_height) {}

  std::vector<std::vector<uint8_t>>
  EncodeGroup(const std::vector<ImageRGBA> &group) {
    return {};
  }

  Buf EncodeIFrame(const ImageRGBA &frame) {
    static constexpr int VERBOSE = 0;

    // For iframe encoding, we are just going to
    // encode the pixels of the image (using the smallest
    // pixelformat). But as one simple improvement, we
    // allow masking the region that contains the pixels,
    // and giving a single solid color for everything else.
    //
    // This helps for the case that the frame has a "background
    // color", typically 0,0,0,0.
    //
    // TODO: Probably the output should be a series of masks
    // and contents. The content can be repeated (so that we
    // can easily get a fill with a single pixel) or something
    // like that.
    Palette palette;
    PixelFormat pixel_format = Palette::MakePalette(frame, &palette);
    CHECK(palette.entries.size() == palette.indices.size());

    uint32_t bgcolor = 0;
    Mask mask = GetForegroundMask(frame, &bgcolor);

    if (VERBOSE > 1) {
      printf("Mask %s with bgcolor: %08x\n",
             MaskString(mask).c_str(), bgcolor);
    }

    // Write.
    Buf buf;

    auto WritePixel = [pixel_format, &palette, &buf](uint32_t c) {
        switch (pixel_format) {
        case PIXELS_PALETTE: {
          auto it = palette.indices.find(c);
          CHECK(it != palette.indices.end()) << "Bug: Missing color in "
            "palette: " << ColorString(c) << "\n"
            "Palette has " << palette.indices.size() << " entries\n" <<
            PaletteString(palette);
          buf.W8(it->second);
          break;
        }

        case PIXELS_RGBA:
          buf.W32(c);
          break;

        case PIXELS_RGB:
          buf.WRGB(c);
          break;

        default:
          LOG(FATAL) << "Bad pixelformat";
        }
      };

    buf.W8(pixel_format);
    if (pixel_format == PIXELS_PALETTE) {
      WritePalette(palette, &buf);
    }

    const size_t size_before_mask = buf.Size();
    WriteMask(mask, &buf);
    const size_t mask_bytes = buf.Size() - size_before_mask;

    switch (MaskType(mask)) {
    case MASK_ALL:
      // Nothing.
      break;

    case MASK_RECT:
    case MASK_RLE:
    case MASK_QUADTREE:
      WritePixel(bgcolor);
      break;

    default:
      LOG(FATAL) << "Bad mask";
      return {};
    }

    if (VERBOSE > 0) {
      printf("Pixelformat %s. Palette size %d. Mask=%s (%zu bytes)\n",
             PixelFormatString(pixel_format),
             (int)palette.entries.size(),
             MaskString(mask).c_str(),
             mask_bytes);
    }

    // Now the image pixels, but only from the mask.
    AppMask(mask, [&frame, &WritePixel](int x, int y) {
        WritePixel(frame.GetPixel32(x, y));
      });

    return buf;
  }

};

struct Decode {
  Decode(int frame_width, int frame_height) :
    frame_width(frame_width), frame_height(frame_height) {}

  ImageRGBA DecodeIFrame(std::span<uint8_t> *v) {
    ImageRGBA ret(frame_width, frame_height);

    std::optional<IFrameHeader> head =
      ParseIFrameHeader(frame_width, frame_height, v);

    if (head.has_value()) {
      printf("Decode: %s\n", MaskString(head->mask).c_str());

      switch (MaskType(head->mask)) {
      case MASK_RECT:
      case MASK_RLE:
      case MASK_QUADTREE:
        ret.Clear32(head->bgcolor);
        break;

      case MASK_ALL:
        break;

      default:
        LOG(FATAL) << "Bad mask";
        break;
      }

      // PERF: We can check that we have enough space easily
      // with MaskNumPixels(head->mask) >= v->size(), and
      // avoid doing the checks in the loop. This also makes
      // it easier to exit early.
      bool failed = false;
      AppMask(head->mask, [&ret, &head, &v, &failed](int x, int y) {
          uint32_t color = 0;
          if (!failed) {
            if (ReadPixel(head.value(), v, &color)) {
              ret.SetPixel32(x, y, color);
            } else {
              failed = true;
            }
          }
        });

      if (!failed) {
        return ret;
      }
    }

    // Error path.
    ret.Clear32(0xFF0000FF);
    ret.BlendText2x32(5, 5, 0x000000FF, "DecodeIFrame error");
    return ret;
  }

  int frame_width = 0, frame_height = 0;
};


struct Testcase {
  std::string name;
  // Path is prefix + n + suffix.
  std::string file_prefix;
  std::string file_suffix;
  int start = 0;
  int num = 0;
};

// XXX return stats
struct TestStats {
  int64_t raw_bytes = 0;
  int64_t png_bytes = 0;
  int64_t cmp_bytes = 0;

  double filter_sec = 0.0;
  double zip_sec = 0.0;

  int used_rle = 0;
  int used_all = 0;
  int used_rect = 0;

  void Merge(const TestStats &other) {
    raw_bytes += other.raw_bytes;
    png_bytes += other.png_bytes;
    cmp_bytes += other.cmp_bytes;

    filter_sec += other.filter_sec;
    zip_sec += other.zip_sec;

    used_rle += other.used_rle;
    used_all += other.used_all;
    used_rect += other.used_rect;
  }
};

static TestStats RunTestcase(const Testcase &t) {
  TestStats stats;

  std::vector<ImageRGBA> frames;
  frames.reserve(t.num);
  for (int i = 0; i < t.num; i++) {
    int n = t.start + i;
    std::string filename =
      std::format("{}{}{}",
                   t.file_prefix,
                   n,
                   t.file_suffix);
    std::unique_ptr<ImageRGBA> frame(ImageRGBA::Load(filename));
    CHECK(frame.get() != nullptr) << filename;
    frames.emplace_back(std::move(*frame));
  }

  CHECK(!frames.empty());
  const int width = frames[0].Width();
  const int height = frames[0].Height();
  for (const ImageRGBA &frame : frames) {
    CHECK(frame.Width() == width &&
          frame.Height() == height) << "Frames must all be the "
      "same size.";
  }

  Encode encode(width, height);
  Decode decode(width, height);

  // All I-frame.
  printf("original\tpng\tenc\tzip\n");
  for (const ImageRGBA &frame : frames) {
    Timer filter_timer;
    Buf buf = encode.EncodeIFrame(frame);
    stats.filter_sec += filter_timer.Seconds();

    size_t orig = frame.Width() * frame.Height() * 4;

    stats.raw_bytes += orig;

    // Size comparison.
    std::vector<uint8_t> miniz_png =
      /*
      ZIP::EncodeAsPNG(frame.Width(), frame.Height(),
                       frame.ToBuffer8());
      */
      PNG::EncodeInMemory(frame, 9);
    [[maybe_unused]] double miniz_png_ratio = orig / (double)miniz_png.size();

    stats.png_bytes += miniz_png.size();

    [[maybe_unused]] double enc_ratio = orig / (double)buf.Size();

    Timer zip_timer;
    std::vector<uint8_t> h777 = ZIP::ZipVector(buf.bytes, 9);
    stats.zip_sec += zip_timer.Seconds();
    stats.cmp_bytes = h777.size();
    [[maybe_unused]] double h777_ratio = orig / (double)h777.size();

    /*
    printf("%d\t%.1fx\t%.1fx\t%.1fx\n",
           (int)orig,
           miniz_png_ratio,
           enc_ratio,
           h777_ratio);
    */

    printf("%d\t%d\t%d\t%d\n",
           (int)orig,
           (int)miniz_png.size(),
           (int)buf.Size(),
           (int)h777.size());

    bool failed = false;

    {
      // (Assumes zip is correct.)
      std::span v(buf.bytes);
      ImageRGBA f = decode.DecodeIFrame(&v);
      if (!v.empty()) {
        printf(ARED("%d") " extra byte(s) at end\n",
               (int)v.size());
        failed = true;
      }

      if (frame != f) {
        frame.Save("test-expected.png");
        f.Save("test-actual.png");
        printf("encode/decode " ARED("round trip") " not equal.\n");
        failed = true;
      }

      CHECK(!failed);
    }
  }

  return stats;
}

[[maybe_unused]]
static void RunTests() {
  std::vector<Testcase> testcases = {

    Testcase{
      .name = "ee",
      .file_prefix = "../rephrase/ee/ee-",
      .file_suffix = ".png",
      .start = 970,
      .num = 10,
    },

    Testcase{
      .name = "tt",
      .file_prefix = "../rephrase/tt-square/tt-",
      .file_suffix = ".png",
      .start = 0,
      .num = 10,
    },

    Testcase{
      .name = "rle",
      .file_prefix = "rle",
      .file_suffix = ".png",
      .start = 123,
      .num = 1,
    },
  };

  TestStats all_stats;

  for (const Testcase &t : testcases) {
    TestStats stats = RunTestcase(t);
    all_stats.Merge(stats);
  }

  double png_ratio = all_stats.raw_bytes / (double)all_stats.png_bytes;
  double h777_ratio = all_stats.raw_bytes / (double)all_stats.cmp_bytes;
  double h777_pct = 100.0 *
    (all_stats.cmp_bytes / (double)all_stats.png_bytes);
  printf("\n" "== " AWHITE("TOTALS") " ==\n"
         "Orig bytes: " AORANGE("%lld") "\n"
         "PNG bytes: " AYELLOW("%lld") " (%.1f:1)\n"
         "H777 bytes: " AGREEN("%lld")
         " (%.1f:1; " AYELLOW("%.3f%%") " of PNG)\n",
         all_stats.raw_bytes,
         all_stats.png_bytes, png_ratio,
         all_stats.cmp_bytes, h777_ratio, h777_pct);
}

[[maybe_unused]]
static void Tune() {
  using IFrameOpt = Optimizer<2, 2, char>;

  // Load all images.
  std::vector<std::pair<std::string, ImageRGBA>> corpus;
  std::filesystem::directory_iterator dir("corpus/");

  for (const auto& entry : dir) {
    if (entry.is_regular_file()) {
      std::string filename = entry.path().string();
      if (Util::EndsWith(filename, ".png")) {
        std::unique_ptr<ImageRGBA> img(ImageRGBA::Load(filename));
        CHECK(img.get() != nullptr);
        corpus.emplace_back(filename, std::move(*img));
      }
    }
  }

  printf("Corpus size: %zu\n", corpus.size());

  auto ArgString = [](const IFrameOpt::arg_type &args) {
      const auto &[qtmr, rmbr] = args.first;
      const auto &[qtbpn, bgbpp] = args.second;
      return std::format("{} {} {:.2f} {:.2f}",
                         qtmr, rmbr,
                         qtbpn, bgbpp);
    };

  int runs = 0;
  IFrameOpt opt([&corpus, &ArgString, &runs](
      const IFrameOpt::arg_type &args) -> IFrameOpt::return_type {
      std::tie(QUADTREE_MIN_RECT_PIXELS,
               RLE_MIN_BACKGROUND_RUN) = args.first;
      std::tie(QUADTREE_BYTES_PER_NODE,
               BYTES_PER_BACKGROUND_PIXEL) = args.second;
      runs++;
      printf("#%d Run %s:\n", runs, ArgString(args).c_str());

      int64_t total = 0;

      for (const auto &[name, img] : corpus) {
        Encode encode(img.Width(), img.Height());

        Buf buf = encode.EncodeIFrame(img);
        std::vector<uint8_t> h777 = ZIP::ZipVector(buf.bytes, 9);
        total += h777.size();
      }

      printf("[%s] Corpus compressed: " AWHITE("%lld") "\n",
             ArgString(args).c_str(),
             (int64_t)total);

      return std::make_pair((double)total, std::make_optional('o'));
    },
  0xCAFE
  );

  // opt.Sample({{1, 1}, {1.04, 1.04}});
  // Previous good tuning.
  // opt.Sample({{35, 39}, {1.04, 2.05}});
  // opt.Sample({{854, 41}, {0.82, 0.03}});
  opt.Sample({{208, 42}, {0.80, 0.02}});

  const std::array<std::pair<int32_t, int32_t>, IFrameOpt::num_ints>
    int_bounds = {
    // quadtree min rect
    std::make_pair(1, 65536),
    // rle min background
    std::make_pair(1, 1024),
  };

  const std::array<std::pair<double, double>, IFrameOpt::num_doubles>
    double_bounds = {
    // quadtree bytes per node.
    // 1+e is the nominal upper bound, since they are actually
    // represented with one byte uncompressed.
    std::make_pair(0.001, 1.1),
    // bytes per background pixel
    // 4+e, since RGBA would be four channels
    std::make_pair(0.001, 4.1),
  };

  static constexpr double RUN_FOR = 1.0 * 3600.0;

  opt.Run(int_bounds, double_bounds,
          // max calls
          std::nullopt,
          // max feasible calls
          std::nullopt,
          std::make_optional(RUN_FOR));

  auto Integral = [](std::string s) {
      return IFrameOpt::IntFeature{
        .name = s,
        .categorical = false,
      };
    };

  std::array<IFrameOpt::IntFeature, IFrameOpt::num_ints> int_features = {
    Integral("qt-min-rect"),
    Integral("rle-min-run"),
  };
  CHECK(int_features.size() == int_bounds.size());

  std::array<IFrameOpt::DoubleFeature, IFrameOpt::num_ints> double_features = {
    IFrameOpt::DoubleFeature{.name = "qt-bpn"},
    IFrameOpt::DoubleFeature{.name = "bg-bpp"},
  };
  CHECK(double_features.size() == double_bounds.size());

  auto FeatureMinMax = [&int_bounds, &double_bounds](
      const IFrameOpt::Feature &f) ->
    std::pair<double, double> {
    switch (f.type) {
    case IFrameOpt::FeatureType::NUMERIC_INT:
    case IFrameOpt::FeatureType::CATEGORICAL_INT:
      CHECK(f.index >= 0 && f.index < int_bounds.size());
      return int_bounds[f.index];

      case IFrameOpt::FeatureType::NUMERIC_DOUBLE:
      CHECK(f.index >= 0 && f.index < double_bounds.size());
      return double_bounds[f.index];

      case IFrameOpt::FeatureType::BIAS:
        return std::make_pair(0.0, 1.0);
    }
  };

  auto ExplainWith = [&](const std::string &name,
                         const std::vector<
                           std::tuple<IFrameOpt::arg_type,
                           double,
                         std::optional<char>>> &datapoints,
                         bool images) {

      printf("Explain using " AYELLOW("%s") " (%d pts)...\n",
             name.c_str(), (int)datapoints.size());
      const auto &[features, loss] =
        opt.Explain(
            datapoints,
            int_features,
            double_features,
            {"bias"});

      printf("Loss: " AWHITE("%.11g") "\n", loss);
      for (const IFrameOpt::Feature &f : features) {
        if (f.type == IFrameOpt::FeatureType::CATEGORICAL_INT) {
          printf(ABLUE("%s") "=" APURPLE("%d") ": ",
                 f.name.c_str(), f.categorical_value);
        } else {
          printf(ABLUE("%s") ": ", f.name.c_str());
        }
        if (f.coefficient < 0.0) {
          printf(AGREEN("%.11g") "\n", f.coefficient);
        } else {
          printf(ARED("+%.11g") "\n", f.coefficient);
        }
      }

      printf("\n");

      // For every pair of features, plot image.
      if (false)
      if (images && !features.empty()) {
        static constexpr int SQUARE = 256;
        static constexpr int MARGIN = 12;

        const int num_total =
          (int)features.size() * ((int)features.size() - 1) / 2;

        int across = std::max((int)sqrt(num_total), 1);
        int down = across;
        while (across * down < num_total) across++;

        int out_width = (SQUARE + MARGIN) * across;
        int out_height = (SQUARE + MARGIN) * down;
        ImageRGBA out(out_width, out_height);
        out.Clear32(0x000000FF);

        // Consider plotting a feature against itself too? The 1D
        // data are interesting and we would at least want *some*
        // output when there is a single feature.
        //
        // (Actually we plot against the constant bias term.)
        int out_idx = 0;
        for (int fx = 0; fx < (int)features.size(); fx++) {
          for (int fy = fx + 1; fy < (int)features.size(); fy++) {

            ImageRGBA plot(SQUARE + MARGIN, SQUARE + MARGIN);
            plot.Clear32(0x00000000);

            // In the plot we just consider the two features,
            // ignoring the fact that other parameters are
            // changing (we "project" to this plane). The
            // values are the output of the evaluation calls.

            // In the same order as the datapoints.
            std::vector<std::pair<int, int>> points;
            points.reserve(datapoints.size());

            const auto &[min1, max1] = FeatureMinMax(features[fx]);
            const auto &[min2, max2] = FeatureMinMax(features[fy]);
            for (const auto &[arg, score, o_] : datapoints) {
              // HERE
            }

            int outx = out_idx % across;
            int outy = out_idx / across;
            out.BlendImage(outx * (SQUARE + MARGIN),
                           outy * (SQUARE + MARGIN), plot);
            out_idx++;
          }
        }

      }
    };

  const auto expt =
    opt.ExploreLocally(
        // No categorical variables
        std::array<bool, IFrameOpt::num_ints>{
          false, false,
        },
        int_bounds,
        double_bounds,
        0.1);

  ExplainWith("expt", expt, false);

  ExplainWith("all", opt.GetAll(), true);

  auto besto = opt.GetBest();
  CHECK(besto.has_value()) << "Every arg is feasible??";
  const auto &[args, lowest_score, ot_] = besto.value();
  printf("Best (score " AGREEN("%.1f") " bytes): %s\n",
         lowest_score,
         ArgString(args).c_str());

  // Make images.
  const auto all = opt.GetAll();

  double highest_score = lowest_score;
  for (const auto &[arg_, score, ot_] : all) {
    highest_score = std::max(score, highest_score);
  }

  // Generate 2D slices by just ignoring all but two
  // dimensions.

  // (XXX)

}


int main(int argc, char **argv) {

  // Tune();

  RunTests();

  return 0;
}
