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

static bool NoGlyph(const FontImage &font, int idx) {
  auto it = font.glyphs.find(idx);
  if (it == font.glyphs.end()) return true;
  return FontImage::EmptyGlyph(it->second);
}

int main(int argc, char **argv) {
  CHECK(argc == 4) <<
    "Usage: ./merge2x.exe config1x.cfg config2x.cfg merged.png";

  const Config config1x = ParseAndCheckConfig(argv[1]);
  const Config config2x = ParseAndCheckConfig(argv[2]);

  FontImage font1x(config1x);
  FontImage font2x(config2x);

  CHECK(config1x.charbox_width * 2 == config2x.charbox_width);
  CHECK(config1x.charbox_height * 2 == config2x.charbox_height);
  // maybe not necessary to check this, but avoid surprises
  CHECK(config1x.spacing * 2 == config2x.spacing);
  CHECK(config1x.descent * 2 == config2x.descent);

  for (const auto &[idx, glyph] : font1x.glyphs) {
    if (!FontImage::EmptyGlyph(glyph)) {
      if (NoGlyph(font2x, idx)) {
        Glyph glyph2x;
        glyph2x.left_edge = glyph.left_edge * 2;
        glyph2x.pic = glyph.pic.ScaleBy(2);
        font2x.glyphs[idx] = glyph2x;
      }
    }
  }

  // Allow expanding down, as it's obvious what we would want.
  int chars_across = config2x.chars_across;
  int chars_down = std::max(config1x.chars_down, config2x.chars_down);
  font2x.SaveImage(argv[3], chars_across, chars_down);

  return 0;
}
