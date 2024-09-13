

#ifndef _MARIO_UTIL_H
#define _MARIO_UTIL_H

#include <cstdint>

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

  static ImageRGBA Screenshot(Emulator *emu);

};

#endif
