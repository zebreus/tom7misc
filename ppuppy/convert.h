
#ifndef _PPUPPY_CONVERT_H
#define _PPUPPY_CONVERT_H

#include "screen.h"
#include <string>
#include <vector>

#include "image.h"

struct ArcFour;

// Convert images to Screen format.

Screen ScreenFromFileDithered(const std::string &filename);

Screen ScreenFromFile(const std::string &filename);

std::vector<Screen> MultiScreenFromFile(const std::string &filename);

enum class PaletteMethod {
  // TODO: fixed, etc.
  GREYSCALE,
  MOST_COMMON,
  MOST_COMMON_SHUFFLED,
  GREEDY_BIGRAMS,
};

void MakePalette(PaletteMethod method, const ImageRGB *img,
                 ArcFour *rc, bool offset,
                 const std::vector<int> &forced,
                 Screen *screen);

// Put the magic bytes in unused palette slots that tell
// ppuppy to turn off debugging.
inline void NoDebugPalette(Screen *screen) {
  screen->palette[8] = 0x2A;
  screen->palette[12] = 0xA7;
}

void FillScreenSelective(ImageRGB *img, bool offset,
                         Screen *screen);

#endif
