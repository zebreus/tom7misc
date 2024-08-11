
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <optional>
#include <variant>
#include <vector>
#include <string>
#include <unordered_map>
#include <utility>
#include <algorithm>
#include <span>

#include "image.h"
#include "base/logging.h"
#include "base/stringprintf.h"

#include "zip.h"

// Assumptions:
//   All frames are a fixed size.
//   That size is at least 1x1.
//   Pixel indices fit in int.
//   Width and height fit in 16 bits each.
// TODO: Can relax some of these with varint representations.


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
};

static const char *PixelFormatString(PixelFormat p) {
  switch (p) {
  case PIXELS_RGBA: return "RGBA";
  case PIXELS_RGB: return "RGB";
  case PIXELS_PALETTE: return "PALETTE";
  default: return "BAD_PIXELFORMAT";
  }
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

// A bijection between uint32_t and uint8_t, favoring small indices.
struct Palette {

  // The RGBA colors in the palette. Distinct. At most 256 entries.
  std::vector<uint32_t> entries;
  // Inverse.
  std::unordered_map<uint32_t, int> indices;

  // The number of times the entry appears. This is only present
  // during encoding. (Maybe make this a separate PaletteStats?)
  std::vector<int> counts;

  // Returns pixelformat. Initializes the palette arg if that
  // format is PALETTE.
  static PixelFormat MakePalette(const ImageRGBA &img, Palette *p) {
    // RGBA -> count
    std::unordered_map<uint32_t, int> counts;
    uint8_t any_alpha = 0x00;
    for (int y = 0; y < img.Height(); y++) {
      for (int x = 0; x < img.Width(); x++) {
        uint32_t c = img.GetPixel32(x, y);
        counts[c]++;
        any_alpha |= (c & 0xFF);
      }
      // Can return early if we already have too many colors.
      if (counts.size() > 256 && any_alpha) {
        return PIXELS_RGBA;
      }
    }

    if (counts.size() > 256) {
      return any_alpha ? PIXELS_RGBA : PIXELS_RGB;
    }

    // Otherwise, create the palette.
    std::vector<std::pair<uint32_t, int>> cv;
    cv.reserve(counts.size());
    for (const auto &[color, count] : counts)
      cv.emplace_back(color, count);
    // Sort by decreasing frequency.
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
    p->counts.resize(num_entries);
    p->indices.clear();
    p->indices.reserve(num_entries);
    for (size_t i = 0; i < num_entries; i++) {
      const auto &[color, count] = cv[i];
      p->entries[i] = color;
      p->counts[i] = count;
      p->indices[color] = (int)i;
    }

    return PIXELS_PALETTE;
  }
};

// A mask is a set of pixels, typically in some spatially
// dense region.
// TODO: Certainly consider other mask types here (e.g.
// a run-length-encoded bitmask or quadtree) and
// boolean operators on them.
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
  // Runs alternate "off" and "on".
  std::vector<int> runs;
};

using Mask =
  std::variant<AllMask, RectMask, RLEMask>;

static MaskType MaskType(const Mask &mask) {
  if (const AllMask *a = std::get_if<AllMask>(&mask)) {
    return MASK_ALL;
  } else if (const RectMask *r = std::get_if<RectMask>(&mask)) {
    return MASK_RECT;
  } else if (const RLEMask *r = std::get_if<RLEMask>(&mask)) {
    return MASK_RLE;
  } else {
    LOG(FATAL) << "Bad mask";
    return MASK_ALL;
  }
}

static std::string MaskString(const Mask &mask) {
  if (const AllMask *a = std::get_if<AllMask>(&mask)) {
    return "ALL";
  } else if (const RectMask *r = std::get_if<RectMask>(&mask)) {
    return StringPrintf("RECT(@%d,%d %dx%d)",
                        r->x, r->y,
                        r->width, r->height);
  } else if (const RLEMask *r = std::get_if<RLEMask>(&mask)) {
    return StringPrintf("RLE(%d runs)",
                        (int)r->runs.size());
  } else {
    LOG(FATAL) << "Bad mask";
    return 0;
  }
}

static int MaskNumPixels(const Mask &mask) {
  if (const AllMask *a = std::get_if<AllMask>(&mask)) {
    return a->width * a->height;
  } else if (const RectMask *r = std::get_if<RectMask>(&mask)) {
    return a->width * a->height;
  } else if (const RLEMask *r = std::get_if<RLEMask>(&mask)) {
    // Only the "on" runs.
    int total = 0;
    for (int i = 1; i < r->runs.size(); i += 2) {
      total += r->runs[i];
    }
    return total;
  } else {
    LOG(FATAL) << "Bad mask";
    return 0;
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

  } else {
    LOG(FATAL) << "Bad mask";
  }
}

// See below, but this requires the background color to be the
// specific color.
static Mask GetForegroundRectMaskWithColor(const ImageRGBA &frame,
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

  if (left == 0 && top == 0 && right == 0 && bottom == 0) {
    return AllMask{.width = width, .height = height};
  } else {
    return RectMask{
      .x = left, .y = top,
      .width = width - left - right,
      .height = mheight,
    };
  }
}

// This masks out a region with a solid "background" color.
// The mask will be non-empty. Sets the background color
// if the mask is not degenerate.
static Mask GetForegroundRectMask(const ImageRGBA &frame, uint32_t *bgcolor) {
  // Greedy approach that only generates rectangular masks.
  const int width = frame.Width(), height = frame.Height();

  CHECK(width > 0 && height > 0);
  // Can't make a non-degenerate mask.
  if (width == 1 && height == 1) {
    return AllMask{.width = width, .height = height};
  }

  // The corner-based logic wants nontrivial edges. We could
  // support this with a special case, though.
  if (width == 1 || height == 1) {
    return AllMask{.width = width, .height = height};
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
    return AllMask{.width = width, .height = height};
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
      GetForegroundRectMaskWithColor(frame, b);
    } else if (c == d) {
      *bgcolor = c;
      return GetForegroundRectMaskWithColor(frame, c);
    } else if (d == a) {
      *bgcolor = d;
      return GetForegroundRectMaskWithColor(frame, d);
    } else {
      // One diagonal is equal. Useless.
      return AllMask{.width = width, .height = height};
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
    StringPrintf("\n"
                 "%08x %08x\n"
                 "%08x %08x\n", a, b, d, c);
    */
    CHECK(a != c);

    // Get two masks and take the smaller one.

    Mask mask1 = GetForegroundRectMaskWithColor(frame, a);
    Mask mask2 = GetForegroundRectMaskWithColor(frame, c);
    if (MaskNumPixels(mask1) < MaskNumPixels(mask2)) {
      *bgcolor = a;
      return mask1;
    } else {
      *bgcolor = c;
      return mask2;
    }
  } else {
    // Both diagonals are equal. Useless.
    return AllMask{.width = width, .height = height};
  }
}

static RLEMask GetForegroundRLEMask(const ImageRGBA &frame, uint32_t *bgcolor) {
  // We can use any color as the background color. Currently we only try the
  // most common color, which would usually be the right choice.

  // HERE: Actually, count longest runs and use that.

}


// Mask out the foreground (against a solid background) if possible.
// Tries to find the best mask (rectangular, rle, ...).
static Mask GetForegroundMask(const ImageRGBA &frame, uint32_t *bgcolor) {
  uint32_t rle_bgcolor = 0;
  RLEMask rle_mask = GetForegroundRLEMask(frame, palette, rle_bgcolor);

  // Don't generate ALL mask in this one.
  uint32_t rect_bgcolor = 0;
  Mask rect_mask = GetForegroundRectMask(frame, rect_bgcolor);

  // The rle mask will always be at least as detailed as the rectangle (if we
  // found the same background color), but it is typically much larger to encode.
  int rect_pixels = MaskNumPixels(rect_mask);
  int rle_pixels = MaskNumPixels(rle_mask);

  if (rect_pixels <= rle_pixels) {
    *bgcolor = rect_bgcolor;
    return rect_mask;
  }

  // Rect is four 16-bit coordinates.
  int rect_size = 4 * 2;
  // Estimate rle as 16 bit run lenghts.
  int rle_size = rle_mask.runs.size() * 2;

  // Choose heuristically. We assume that the difference between the masks is
  // encoding background pixels.
  // PERF: Tune this.
  static constexpr double BYTES_PER_BACKGROUND_PIXEL = 1.5 / 8.0;

  double rect_cost_bytes = rect_size + (rect_pixels - rle_pixels) * BYTES_PER_BACKGROUND_PIXEL;
  double rle_cost_bytes = rle_size;

  if (rle_cost_bytes < rect_cost_bytes) {
    *bgcolor = rle_bgcolor;
    return rle_mask;
  } else {
    *bgcolor = rect_bgcolor;
    return rect_mask;
  }
}

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
  if (v->size() < 4 * num_entries) return false;
  for (int i = 0; i < num_entries; i++) {
    p->entries.push_back(Read32(v));
  }

  // PERF: We probably don't need the inverted map for decoding.
  for (int i = 0; i < num_entries; i++) {
    p->indices[p->entries[i]] = i;
  }

  return true;
}

static bool ReadMask(int frame_width,
                     int frame_height,
                     Mask *m,
                     std::span<uint8_t> *v) {
  if (v->empty()) return false;
  switch (Read8(v)) {
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

  if (!ReadMask(frame_width, frame_height, &head.mask, v))
    return std::nullopt;

  if (const RectMask *r = std::get_if<RectMask>(&head.mask)) {
    uint32_t c = 0;
    if (!ReadPixel(head, v, &c))
      return std::nullopt;
    head.bgcolor = c;
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
  if (const AllMask *a = std::get_if<AllMask>(&mask)) {
    buf->W8(MASK_ALL);

  } else if (const RectMask *r = std::get_if<RectMask>(&mask)) {
    buf->W8(MASK_RECT);
    buf->W16(r->x);
    buf->W16(r->y);
    buf->W16(r->width);
    buf->W16(r->height);

  } else {
    LOG(FATAL) << "Bad mask";
  }
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

    uint32_t bgcolor = 0;
    Mask mask = GetForegroundMask(frame, &bgcolor);

    printf("Pixelformat %s. Palette size %d. Mask=%s\n",
           PixelFormatString(pixel_format),
           (int)palette.entries.size(),
           MaskString(mask).c_str());


    // Write.
    Buf buf;

    auto WritePixel = [pixel_format, &palette, &buf](uint32_t c) {
        switch (pixel_format) {
        case PIXELS_PALETTE: {
          auto it = palette.indices.find(c);
          CHECK(it != palette.indices.end()) << "Bug: Missing color in "
            "palette?";
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

    WriteMask(mask, &buf);

    if (const AllMask *a = std::get_if<AllMask>(&mask)) {
      // Nothing.
    } else if (const RectMask *r = std::get_if<RectMask>(&mask)) {
      WritePixel(bgcolor);
    } else {
      LOG(FATAL) << "Bad mask";
      return {};
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

  // PERF: Probably these should work on spans, not vectors.
  ImageRGBA DecodeIFrame(std::span<uint8_t> *v) {
    ImageRGBA ret(frame_width, frame_height);

    std::optional<IFrameHeader> head =
      ParseIFrameHeader(frame_width, frame_height, v);

    if (head.has_value()) {
      if (MaskType(head->mask) == MASK_RECT) {
        ret.Clear32(head->bgcolor);
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
  // Path is prefix + n + suffix.
  std::string file_prefix;
  std::string file_suffix;
  int start = 0;
  int num = 0;
};

// XXX return stats
static void RunTestcase(const Testcase &t) {
  std::vector<ImageRGBA> frames;
  frames.reserve(t.num);
  for (int i = 0; i < t.num; i++) {
    int n = t.start + i;
    std::string filename =
      StringPrintf("%s%d%s",
                   t.file_prefix.c_str(),
                   n,
                   t.file_suffix.c_str());
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
    Buf buf = encode.EncodeIFrame(frame);

    size_t orig = frame.Width() * frame.Height() * 4;

    // Size comparison.
    std::vector<uint8_t> miniz_png =
      ZIP::EncodeAsPNG(frame.Width(), frame.Height(),
                       frame.ToBuffer8());
    [[maybe_unused]] double miniz_png_ratio = orig / (double)miniz_png.size();

    [[maybe_unused]] double enc_ratio = orig / (double)buf.Size();

    std::vector<uint8_t> h777 = ZIP::ZipVector(buf.bytes, 9);
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

    {
      // (Assumes zip is correct.)
      std::span v(buf.bytes);
      ImageRGBA f = decode.DecodeIFrame(&v);
      if (frame != f) {
        frame.Save("test-expected.png");
        f.Save("test-actual.png");
        LOG(FATAL) << "encode/decode round trip not equal.";
      }
    }
  }
}

static void RunTests() {
  std::vector<Testcase> testcases = {
    Testcase{
      .file_prefix = "../rephrase/ee/ee-",
      .file_suffix = ".png",
      .start = 970,
      .num = 10,
    },

    Testcase{
      .file_prefix = "../rephrase/tt-square/tt-",
      .file_suffix = ".png",
      .start = 0,
      .num = 10,
    },
  };

  for (const Testcase &t : testcases) {
    RunTestcase(t);
  }
}


int main(int argc, char **argv) {

  RunTests();

  return 0;
}
