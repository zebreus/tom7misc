// Copy characters from a 1x font to 2x font, if they aren't already
// present there.

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <set>
#include <map>
#include <unordered_map>

#include "util.h"
#include "image.h"
#include "bit7chars.h"
#include "bitmap-font.h"
#include "base/stringprintf.h"
#include "base/logging.h"
#include "fonts/island-finder.h"
#include "fonts/ttf.h"
#include "font-image.h"

using namespace std;
using uint8 = uint8_t;
using uint32 = uint32_t;
using uint64 = uint64_t;

using Glyph = FontImage::Glyph;


template<class C, class K>
static bool ContainsKey(const C &c, const K &k) {
  return c.find(k) != c.end();
}

static Config ParseAndCheckConfig(const std::string &cfgfile) {
  Config config = Config::ParseConfig(cfgfile);
  CHECK(!config.pngfile.empty()) << "Required config line: pngfile";
  CHECK(!config.name.empty()) << "Required config line: name";

  CHECK(config.charbox_width > 0) << "Config line charbox-width must be >0";
  CHECK(config.charbox_height > 0) << "Config line charbox-height must be >0";

  CHECK(config.descent >= 0) << "Config line charbox-height must be >= 0";

  CHECK(config.chars_across > 0);
  CHECK(config.chars_down > 0);

  return config;
}

int main(int argc, char **argv) {
  CHECK(argc == 4) <<
    "Usage: ./addrow.exe config1x.cfg 7 merged.png\n"
    "Inserts a blank 7th row in the image.\n";
  const int add_row = atoi(argv[2]);

  const Config config = ParseAndCheckConfig(argv[1]);
  CHECK(add_row <= config.chars_down);

  FontImage font(config);

  // Now just increase the glyph offsets.
  const int min_glyph = config.chars_across * add_row;

  std::map<int, Glyph> glyphs = std::move(font.glyphs);
  font.glyphs.clear();

  for (const auto &[idx, glyph] : glyphs) {
    const int new_idx = idx < min_glyph ? idx : idx + config.chars_across;
    font.glyphs[new_idx] = glyph;
  }

  int chars_across = config.chars_across;
  int chars_down = config.chars_down + 1;
  font.SaveImage(argv[3], chars_across, chars_down);

  return 0;
}
