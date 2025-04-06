#ifndef _BIT7_FONT_IMAGE_H
#define _BIT7_FONT_IMAGE_H

#include <string>
#include <unordered_map>
#include <cstdint>
#include <memory>
#include <vector>

#include "image.h"

// A page maps glyph indices to unicode codepoints.
enum class Page {
  BIT7_CLASSIC,
  BIT7_LATINAB,
  BIT7_EXTENDED,
  BIT7_CYRILLIC,
  BIT7_MATH,
};

struct Config {
  std::string pngfile;

  std::string name;
  std::string copyright;

  // TODO: Make configurable?
  static constexpr int page_spacing = 8;

  // If true, copy uppercase letters to lowercase (where missing).
  bool no_lowercase = false;
  // If true, the character width is always the charbox width.
  bool fixed_width = false;

  // Size of regular grid in input image (see e.g. makegrid).
  int charbox_width = 0;
  int charbox_height = 0;
  // Pixels at the bottom of the charbox that are beneath the baseline.
  int descent = 0;

  // Pixels on the right side of each charbox that are for
  // inter-character space. This is only used by makegrid
  // to generate darker background pixels in the template image
  // (and these pixels are not treated any differently).
  int spacing = 0;

  // Per page.
  int chars_across = 16;
  int chars_down = 8;

  // Describes how the glyphs on each page are encoded.
  // This also tells us how many pages there are (across).
  std::vector<Page> pages;

  // Additional space between lines, in pixels.
  int extra_linespacing = 0;

  char vendor[4] = {'f', 'o', 'n', 't'};

  static Page ParsePage(const std::string &p);
  static const char *PageString(Page p);
  static Config ParseConfig(const std::string &cfgfile);
};

struct FontImage {
  struct Glyph {
    // Can be negative, allowing for overhang on a character like j,
    // for example. XXX not implemented
    int left_edge = 0;
    // Height will be charbox_height; width of the image may vary from
    // glyph to glyph. This is a 1-bit bitmap; 0 means "off"
    // (transparent) and any other value is "on".
    ImageA pic;
  };

  // As an ASCII drawing.
  static std::string GlyphString(const Glyph &glyph);

  // True if the glyph is totally blank.
  static bool EmptyGlyph(const Glyph &g);

  explicit FontImage(const Config &config);

  // Save the glyphs as a normalized image, using the member config.
  // It's OK for the config to be modified; this just uses the
  // already-loaded glyphs. If there's no glyph in a space implied
  // by the config, it'll just be blank.
  void SaveImage(const std::string &filename);

  // Render just one page. Uses the config for the charbox sizes, etc.
  ImageRGBA ImagePage(Page p);

  std::unordered_map<int, int> GetUnicode() const {
    return unicode_to_glyph;
  }

  bool MappedCodepoint(uint32_t codepoint) const;

  // Maps from codepoint to glyph index in vector below.
  std::unordered_map<int, int> unicode_to_glyph;

  // Map from character index (position in image) to glyph.
  std::vector<Glyph> glyphs;
  Config config;

 private:
  void AddPage(const ImageRGBA &img, Page p);
};

// For drawing to ImageRGBA.
struct BitmapFont {
  static std::unique_ptr<BitmapFont> Load(const std::string &configfile);
  BitmapFont(FontImage font);

  void DrawText(ImageRGBA *img, int x, int y,
                uint32_t color,
                const std::string &s) const;

  int Height() const;
  int Width(int codepoint = 'm') const;

 private:
  FontImage font;
};

#endif
