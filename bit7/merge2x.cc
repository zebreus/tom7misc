// Copy characters from a 1x font to 2x font, if they aren't already
// present there.

#include <cstdio>
#include <string>
#include <vector>

#include "base/logging.h"
#include "font-image.h"
#include "util.h"

using namespace std;

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

static bool NoGlyph(const FontImage &font, int codepoint) {
  auto it = font.unicode_to_glyph.find(codepoint);
  if (it == font.unicode_to_glyph.end()) return true;
  return FontImage::EmptyGlyph(font.glyphs[it->second]);
}

int main(int argc, char **argv) {
  CHECK(argc == 3 || argc == 4) <<
    "Usage: ./merge2x.exe config1x.cfg config2x.cfg [merged.png]\n"
    "\n"
    "Adds missing characters in the second font by upscaling\n"
    "characters from the first. The second font must be exactly\n"
    "twice the dimensions. If the merged.png is not provided,\n"
    "back up the second file's font image and overwrite it.\n";

  const Config config1x = ParseAndCheckConfig(argv[1]);
  const Config config2x = ParseAndCheckConfig(argv[2]);

  FontImage font1x(config1x);
  FontImage font2x(config2x);

  std::string dest = config2x.pngfile;
  if (argc == 4) dest = argv[3];

  if (Util::ExistsFile(dest)) {
    std::string back = Util::BackupFile(dest);
    printf("Moved old %s to %s\n", dest.c_str(),
           back.c_str());
  }

  CHECK(config1x.charbox_width * 2 == config2x.charbox_width);
  CHECK(config1x.charbox_height * 2 == config2x.charbox_height);
  // maybe not necessary to check this, but avoid surprises
  CHECK(config1x.spacing * 2 == config2x.spacing);
  CHECK(config1x.descent * 2 == config2x.descent);

  for (const auto &[codepoint, gidx] : font1x.unicode_to_glyph) {
    const FontImage::Glyph &glyph = font1x.glyphs[gidx];
    if (!FontImage::EmptyGlyph(glyph)) {
      if (NoGlyph(font2x, codepoint)) {
        Glyph glyph2x;
        glyph2x.left_edge = glyph.left_edge * 2;
        glyph2x.pic = glyph.pic.ScaleBy(2);
        int new_gidx = (int)font2x.glyphs.size();
        font2x.glyphs.push_back(glyph2x);
        font2x.unicode_to_glyph[codepoint] = new_gidx;
      }
    }
  }

  font2x.SaveImage(dest);

  return 0;
}
