
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>
#include <string>
#include <unordered_map>
#include <utility>
#include <algorithm>

#include "image.h"
#include "base/logging.h"
#include "base/stringprintf.h"

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

static constexpr uint8_t PIXELS_RGBA = 0;
static constexpr uint8_t PIXELS_RGB = 1;
static constexpr uint8_t PIXELS_PALETTE = 2;

// Two bytes in big-endian order, for use in structs.
struct u16be {
  explicit u16be(uint16_t u) : hi(u >> 8), lo(u & 0xFF) {}
  u16be(uint8_t hi, uint8_t lo) : hi(hi), lo(lo) {}

  uint16_t value() const { return (uint16_t(hi) << 8) | lo; }

  uint8_t hi = 0;
  uint8_t lo = 0;
};

// TODO: Masks

struct IFrameHeader {

  uint8_t pixel_format;

  // If pixel_format is palette, then this is the number of
  // palette entries minus one (since there may be 256 entries and
  // cannot be zero). Otherwise, zero.
  // Palette entries are always RGBA.
  uint8_t palette_size;
};

// A bijection between uint32_t and uint8_t, favoring small indices.
struct Palette {

  // The RGBA colors in the palette. Distinct. At most 256 entries.
  std::vector<uint32_t> entries;
  // The number of times the entry appears.
  std::vector<int> counts;
  // Inverse.
  std::unordered_map<uint32_t, int> indices;

  // Returns pixelformat. Initializes the palette arg if that
  // format is PALETTE.
  static uint8_t MakePalette(const ImageRGBA &img, Palette *p) {
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
//
// TODO: Certainly consider other mask types here (e.g.
// a run-length-encoded bitmask or quadtree) and
// boolean operators on them.
struct Mask {
  enum MaskType : uint8_t {
    ALL = 0,
    RECT = 1,
  };

  MaskType type = ALL;

  // Maybe better to do this as a serialize/deserialize
  // routine?
  union u {
    struct all {
      // Nothing.
    };

    struct rect {
      u16be x, y;
      u16be w, h;
    };
  };

  static Mask All() {
    Mask mask;
    mask.type = ALL;
    return mask;
  }

  int NumPixels() const {

  }

};

// See below, but this requires the background color to be the
// specific color.
static Mask GetForegroundMaskWithColor(const ImageRGBA &frame,
                                       uint32_t bgcolor) {
  int left = 0, top = 0, right = 0, bottom = 0;

  // The order we do this in matters, but a row has to be
  // completely blank in order to crop it out.
  auto SolidRow = [&frame, width](int x) -> std::optional<uint32_t> {
      for (int i = 0; i < width; i++) {
        if
      }
    };

  for (int i = 0; i < width; i++) {

  }

}

// This masks out a region with a solid "background" color.
// The mask will be non-empty.
static Mask GetForegroundMask(const ImageRGBA &frame) {
  // Greedy approach that only generates rectangular masks.
  const int width = frame.Width(), height = frame.Height();

  CHECK(width > 0 && height > 0);
  // Can't make a non-degenerate mask.
  if (width == 1 && height == 1) {
    return Mask::All();
  }

  // The corner-based logic wants nontrivial edges. We could
  // support this with a special case, though.
  if (width == 1 || height == 1) {
    return Mask::All();
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
  if (a == b) num_eq++;
  if (b == c) num_eq++;
  if (c == d) num_eq++;
  if (d == a) num_eq++;

  // No way to make a rectangular mask, then.
  if (num_eq == 0) return Mask::All();

  if (num_eq >= 4) {
    // Only one color will work. We can figure out the color
    // value with some pigeonhole logic:
    const uint32_t color = a == b ? a : c;
    return GetForegroundMaskWithColor(frame, color);
  }

  if (num_eq == 1) {
    // Only one edge can be cropped.
    // We could have a faster version of the cropping logic in this
    // case, but it's not like this is the
    if (a == b) return GetForegroundMaskWithColor(frame, a);
    if (b == c) return GetForegroundMaskWithColor(frame, b);
    if (c == d) return GetForegroundMaskWithColor(frame, c);
    CHECK(d == a);
    return GetForegroundMaskWithColor(frame, d);
  }

  CHECK(num_eq == 2);
  // The only interesting case is that opposing edges would work,
  // but they have different colors, so we can only choose one.
  // An example would be a French flag, where we could choose
  // blue or red to crop out. If the flag is asymmetric, one
  // choice may be better than the other.
  // Get two masks and take the smaller one.
  if (a == b && c == d) {
    // Crop vertically.
    Mask mask1 = GetForegroundMaskWithColor(frame, a);
    Mask mask2 = GetForegroundMaskWithColor(frame, c);
    if (mask ==
  } else {
    CHECK(b == c && d == a);

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

  std::vector<uint8_t> EncodeIFrame(const ImageRGBA &frame) {

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
    Palette p;
    uint8_t pixel_format = Palette::MakePalette(frame, &p);

    Mask m = GetForegroundMask(frame);

    return {};
  }

};

struct Decode {
  Decode(int frame_width, int frame_height) :
    frame_width(frame_width), frame_height(frame_height) {}

  // PERF: Probably these should work on spans, not vectors.
  ImageRGBA DecodeIFrame(const std::vector<uint8_t> &v) {
    ImageRGBA ret(frame_width, frame_height);
    ret.Clear32(0xFF0000FF);
    ret.BlendText2x32(6, 6, 0x000000FF, "UNIMPLEMENTED");
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

  // All I-frame.
  for (const ImageRGBA &frame : frames) {
    std::vector<uint8_t> bytes = encode.EncodeIFrame()
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
  };

  for (const Testcase &t : testcases) {
    RunTestcase(t);
  }
}


int main(int argc, char **argv) {

  RunTests();

  return 0;
}
