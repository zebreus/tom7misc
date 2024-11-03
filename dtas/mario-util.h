

#ifndef _MARIO_UTIL_H
#define _MARIO_UTIL_H

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "../fceulib/emulator.h"
#include "image.h"

struct MarioUtil {

  // Emulator state should be immediately after loading mario.nes (or
  // restoring such a state).
  //
  // Warps to the given level. The major/minor world numbers are
  // zero-based. The halfway page is zero for the beginning of a
  // level, but it can be modified to continue part-way through the
  // level (or cause havoc by being out of bounds).
  static void WarpTo(Emulator *emu,
                     uint8_t major, uint8_t minor, uint8_t halfway);

  // Take a screenshot in the current state. The last step needs to
  // have been StepFull or the image might be stale.
  static ImageRGBA Screenshot(Emulator *emu);

  // Does a full step and then takes a screenshot; then undoes the
  // step so that the emulator state is unchanged.
  static ImageRGBA ScreenshotAny(Emulator *emu);

  // Return an ANSI image, 64 characters wide, 30 lines tall.
  static std::string ScreenshotANSI(Emulator *emu);

  // Play the movie at the current state. Expects that we stay on
  // the same level. Generates an image stitched together of the
  // level as seen during play.
  static ImageRGBA MakeMap(Emulator *emu,
                           const std::vector<uint8_t> &movie);

  // TODO: Include velocity, sub-pixel values, etc?
  struct Pos {
    uint16_t x = 0;
    uint16_t y = 0;
  };

  // Get raw global x,y coordinates of the player.
  // x is as you'd expect. For y, 256-512 is on-screen.
  static Pos GetPos(const Emulator *emu);

  // Get Mario's path through a level, as a series of absolute
  // coordinates. y coordinates of 256-512 are on-screen.
  // Exact consecutive duplicates are removed.
  static std::vector<Pos> GetPath(Emulator *emu,
                                  const std::vector<uint8_t> &movie);

  static void DrawPath(const std::vector<Pos> &path,
                       ImageRGBA *img,
                       uint32_t color);

  static std::string FormatNum(uint64_t n);

  static std::string DescribeAddress(uint16_t addr);
};

inline bool operator== (const MarioUtil::Pos &a, const MarioUtil::Pos &b) {
  return a.x == b.x && a.y == b.y;
}

#endif
