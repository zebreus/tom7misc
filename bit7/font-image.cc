#include "font-image.h"

#include <string>
#include <map>
#include <cstdint>
#include <memory>

#include "util.h"
#include "image.h"
#include "base/logging.h"

using namespace std;

Config Config::ParseConfig(const string &cfgfile) {
  Config config;
  std::map<string, string> m = Util::ReadFileToMap(cfgfile);
  CHECK(!m.empty()) << "Couldn't read config file " << cfgfile;
  config.pngfile = m["pngfile"];
  config.name = m["name"];
  config.copyright = m["copyright"];
  config.charbox_width = atoi(m["charbox-width"].c_str());
  config.charbox_height = atoi(m["charbox-height"].c_str());
  config.descent = atoi(m["descent"].c_str());
  config.spacing = atoi(m["spacing"].c_str());

  if (m.find("chars-across") != m.end())
    config.chars_across = atoi(m["chars-across"].c_str());

  if (m.find("chars-down") != m.end())
    config.chars_down = atoi(m["chars-down"].c_str());

  if (m.find("extra-linespacing") != m.end())
    config.extra_linespacing = atoi(m["extra-linespacing"].c_str());

  if (m.find("no-lowercase") != m.end())
    config.no_lowercase = true;

  if (m.find("fixed-width") != m.end())
    config.fixed_width = true;

  return config;
}

string FontImage::GlyphString(const Glyph &glyph) {
  string out;
  for (int y = 0; y < glyph.pic.Height(); y++) {
    for (int x = 0; x < glyph.pic.Width(); x++) {
      char c = (glyph.pic.GetPixel(x, y) != 0) ? '#' : '.';
      out += c;
    }
    out += '\n';
  }
  return out;
}

bool FontImage::EmptyGlyph(const Glyph &g) {
  for (int y = 0; y < g.pic.Height(); y++)
    for (int x = 0; x < g.pic.Width(); x++)
      if (g.pic.GetPixel(x, y) != 0) return false;
  return true;
}

FontImage::FontImage(const Config &config) {
  const int chars_across = config.chars_across;
  const int chars_down = config.chars_down;

  // XXX: Make it so we can just set a fixed width in the config
  // and ignore black lines?
  // 'spacing' is presentational in makegrid; we derive the width
  // from the black line in each character cell.

  std::unique_ptr<ImageRGBA> input(ImageRGBA::Load(config.pngfile));
  CHECK(input.get() != nullptr) << "Couldn't load: " << config.pngfile;
  CHECK(chars_across * config.charbox_width == input->Width() &&
        chars_down * config.charbox_height == input->Height()) <<
    "Image with configured charboxes " << config.charbox_width << "x"
                                       << config.charbox_height <<
    " should be " << (chars_across * config.charbox_width) << "x"
                  << (chars_down * config.charbox_height) << " but got "
                  << input->Width() << "x" << input->Height();

  for (int cy = 0; cy < chars_down; cy++) {
    for (int cx = 0; cx < chars_across; cx++) {
      const int cidx = chars_across * cy + cx;

      // Get width, by searching for a column of all black.
      auto GetWidth = [&]() {
          if (config.fixed_width)
            return config.charbox_width;
          for (int x = 0; x < config.charbox_width; x++) {
            auto IsBlackColumn = [&]() {
                int sx = cx * config.charbox_width + x;
                for (int y = 0; y < config.charbox_height; y++) {
                  int sy = cy * config.charbox_height + y;
                  uint32_t color = input->GetPixel32(sx, sy);
                  if (color != 0x000000FF) return false;
                }
                return true;
              };
            if (IsBlackColumn()) {
              return x;
            }
          }
          return -1;
        };
      // -1 if not found. This is tolerated for totally empty characters.
      const int width = GetWidth();

      auto IsEmpty = [&]() {
          for (int y = 0; y < config.charbox_height; y++) {
            for (int x = 0; x < config.charbox_width; x++) {
              int sx = cx * config.charbox_width + x;
              int sy = cy * config.charbox_height + y;
              uint32_t color = input->GetPixel32(sx, sy);
              if (color == 0xFFFFFFFF) return false;
            }
          }
          return true;
        };

      if (width < 0) {
        if (!IsEmpty()) {
          printf("%s: "
                 "Character at cx=%d, cy=%d has no width (black column) but "
                 "has a glyph (white pixels).\n",
                 config.pngfile.c_str(), cx, cy);
          CHECK(false);
        }

        continue;
      } else if (width == 0) {
        printf("%s: Character at cx=%d, cy=%d has zero width; "
               "not supported!\n",
               config.pngfile.c_str(), cx, cy);
        CHECK(false);
      } else {
        // Glyph, but possibly an empty one...
        ImageA pic{width, config.charbox_height};
        pic.Clear(0x00);

        for (int y = 0; y < config.charbox_height; y++) {
          for (int x = 0; x < width; x++) {
            int sx = cx * config.charbox_width + x;
            int sy = cy * config.charbox_height + y;
            bool bit = input->GetPixel32(sx, sy) == 0xFFFFFFFF;
            if (bit) pic.SetPixel(x, y, 0xFF);
          }
        }

        Glyph *glyph = &glyphs[cidx];
        // No way to set this from image yet...
        glyph->left_edge = 0;
        glyph->pic = std::move(pic);
      }
    }
  }
}

