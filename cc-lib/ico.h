
#ifndef _CC_LIB_ICO_H
#define _CC_LIB_ICO_H

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "image.h"

// Read and write ICO (and CUR) files.
// These are almost the same format; CUR just has a pair of 16-bit
// coordinates that indicate where in the image the mouse cursor's "tip"
// is.
struct ICO {

  // Returns the empty vector on failure.
  // This works on both ICO and CUR files, but ignores the hotspot
  // for CUR files.
  static std::vector<ImageRGBA> ParseICO(std::span<const uint8_t> bytes);
  static std::vector<ImageRGBA> LoadICO(std::string_view filename);

  // Cursor image and its "hotspot" offset.
  struct Cursor {
    ImageRGBA img;
    int x = 0, y = 0;
  };

  // This will ignore ICO-type images in the data (they have no
  // hotspot).
  static std::vector<Cursor> ParseCUR(std::span<const uint8_t> bytes);
  static std::vector<Cursor> LoadCUR(std::string_view filename);

  // Saves with embedded PNG files.
  static std::vector<uint8_t> EncodeICO(
      std::span<const ImageRGBA> images);

  // Returns true on success.
  static bool SaveICO(std::string_view filename,
                      std::span<const ImageRGBA> images);

  static std::vector<uint8_t> EncodeCUR(
      std::span<const Cursor> images);

  static bool SaveCUR(std::string_view filename,
                      std::span<const Cursor> images);

};


#endif
