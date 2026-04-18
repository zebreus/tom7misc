
#include "ico.h"

#include <string>
#include <string_view>
#include <vector>

#include "ansi-image.h"
#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "ico.h"
#include "image.h"
#include "util.h"

[[maybe_unused]]
static void ShowIcons() {
  std::string dir = "../../win95-winxp_icons/icons/";
  for (const std::string &filename : Util::ListFiles(dir)) {
    if (filename.ends_with(".ico")) {
      std::vector<ImageRGBA> icons =
        ICO::LoadICO(Util::DirPlus(dir, filename));
      CHECK(!icons.empty()) << filename;

      for (const ImageRGBA &img : icons) {
        Print("\n" AWHITE("{}") " " AGREY("({}×{})") ":\n",
              filename, img.Width(), img.Height());
        if (img.Width() > 64) {
          Print("(big)\n");
        } else {
          Print("{}\n", ANSIImage::HalfChar(img));
        }
      }
    }
  }
}

[[maybe_unused]]
static void ShowCursor() {

  for (std::string_view filename : {"arrow.cur", "hourglass.cur"}) {
    std::vector<ICO::Cursor> cursors = ICO::LoadCUR(filename);
    CHECK(cursors.size() == 1);

    ImageRGBA img = cursors[0].img;
    img.BlendPixel32(cursors[0].x, cursors[0].y, 0xFF0000AA);
    Print("Cursor {} ({}x{}), hot @{},{}\n\n"
          "{}\n",
          filename, img.Width(), img.Height(),
          cursors[0].x, cursors[0].y,
          ANSIImage::HalfChar(img));
  }
}

static void RoundTripICO() {
  std::vector<ImageRGBA> original;

  // 16x16 red image with a white pixel
  original.emplace_back(16, 16);
  original.back().Clear32(0xFF0000FF);
  original.back().SetPixel32(0, 0, 0xFFFFFFFF);

  // 32x32 green image with a semi-transparent blue pixel
  original.emplace_back(32, 32);
  original.back().Clear32(0x00FF00FF);
  original.back().SetPixel32(15, 15, 0x0000FF80);

  // Encode to in-memory ICO file
  std::vector<uint8_t> encoded = ICO::EncodeICO(original);
  CHECK(!encoded.empty()) << "Failed to encode ICO data.";

  // Parse from in-memory ICO file
  std::vector<ImageRGBA> decoded = ICO::ParseICO(encoded);
  CHECK(decoded.size() == original.size())
      << "Parsed ICO image count doesn't match original.";

  // Test for equality
  for (size_t i = 0; i < original.size(); i++) {
    CHECK(original[i].Width() == decoded[i].Width());
    CHECK(original[i].Height() == decoded[i].Height());
    CHECK(original[i] == decoded[i])
        << "Image " << i << " did not round-trip correctly.";
  }
}

static void RoundTripCUR() {
  std::vector<ICO::Cursor> original;

  // 16x16 red image with a white pixel
  original.push_back(ICO::Cursor{.img = ImageRGBA(16, 16), .x = 8, .y = 8});
  original.back().img.Clear32(0xFF0000FF);
  original.back().img.SetPixel32(0, 0, 0xFFFFFFFF);

  // 32x32 green image with a semi-transparent blue pixel
  original.push_back(ICO::Cursor{.img = ImageRGBA(32, 32), .x = 16, .y = 16});
  original.back().img.Clear32(0x00FF00FF);
  original.back().img.SetPixel32(15, 15, 0x0000FF80);

  // Encode to in-memory CUR file
  std::vector<uint8_t> encoded = ICO::EncodeCUR(original);
  CHECK(!encoded.empty()) << "Failed to encode CUR data.";

  // Parse from in-memory CUR file
  std::vector<ICO::Cursor> decoded = ICO::ParseCUR(encoded);
  CHECK(decoded.size() == original.size())
      << "Parsed CUR image count doesn't match original.";

  // Test for equality
  for (size_t i = 0; i < original.size(); i++) {
    CHECK(original[i].img.Width() == decoded[i].img.Width());
    CHECK(original[i].img.Height() == decoded[i].img.Height());
    CHECK(original[i].img == decoded[i].img)
        << "Image " << i << " did not round-trip correctly.";
    CHECK(original[i].x == decoded[i].x)
        << "Hotspot X for image " << i << " did not round-trip correctly.";
    CHECK(original[i].y == decoded[i].y)
        << "Hotspot Y for image " << i << " did not round-trip correctly.";
  }
}

int main(int argc, char **argv) {
  ANSI::Init();

  // Need non-free assets.
  // ShowCursor();
  // ShowIcons();

  RoundTripICO();
  RoundTripCUR();

  Print("OK\n");
  return 0;
}
