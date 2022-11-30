#ifndef _BIT7_CONFIG_H
#define _BIT7_CONFIG_H

#include <string>

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

#endif
