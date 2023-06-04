#ifndef _BIT7_FONT_IMAGE_H
#define _BIT7_FONT_IMAGE_H

#include <string>
#include <map>
#include "image.h"

struct Config {
  std::string pngfile;

  std::string name;
  std::string copyright;

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

  // Arrangement of characters in PNG file.
  int chars_across = 16;
  int chars_down = 8;

  // Additional space between lines, in pixels.
  int extra_linespacing = 0;

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

  static std::string GlyphString(const Glyph &glyph);
  static bool EmptyGlyph(const Glyph &g);

  explicit FontImage(const Config &config);

  // Save the glyphs as a normalized image. Any characters with indices
  // outside chars_across * chars_down is discarded. Any character with
  // no glyph will just be blank.
  void SaveImage(const std::string &filename,
                 int chars_across, int chars_down);

  // Map from character index (position in image) to glyph.
  std::map<int, Glyph> glyphs;
  Config config;
};


#endif
