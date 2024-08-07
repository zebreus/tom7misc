
#include <cstdint>
#include <vector>
#include <string>
#include <unordered_map>
#include <utility>
#include <algorithm>

#include "image.h"

// Assumptions:
//   All frames are a fixed size.
//   That size is at least 1x1.
//   Pixel indices fit in int.

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

struct Encode {
  std::vector<std::vector<uint8_t>>
  EncodeGroup(const std::vector<ImageRGBA> &group) {
    return {};
  }

  std::vector<uint8_t> EncodeIFrame(const ImageRGBA &frame) {
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

static void RunTests() {
  std::vector<Testcase> testcases = {
    Testcase{
      .file_prefix = "../rephrase/ee/ee-",
      .file_suffix = ".png",
      .start = 970,
      .num = 10,
    },
  };

  // XXX HERE.
}


int main(int argc, char **argv) {

  RunTests();

  return 0;
}
