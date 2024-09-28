
#include "mario-util.h"

#include <cstdint>
#include <utility>
#include <vector>

#include "../fceulib/emulator.h"
#include "../fceulib/simplefm2.h"
#include "image.h"

#include "mario.h"

// Place the emulator state at the standard beginning of world-level.
//
// There's a way to warp to world-1 which is semi-legitimate, using the
// "continue" feature (hold A on menu). You put the target world in RAM
// and then do a "warm boot" of the game. You have to meet some conditions
// for the warm start to be accepted:
//   - WARM_BOOT_VALIDATION needs to contain 0xA5.
//   - The "top score" display digits all need to be 0-9.
// This video explains the conditions:
// https://youtu.be/hrFHNgJlJSg?si=RkI_4qYsp-PDcXL7
//
// The problem with this is that it only lets you warp to the first level
// of the world. Instead we just modify the next major/minor world number
// so that we can access all 256^2 levels.

void MarioUtil::WarpTo(Emulator *emu,
                       uint8_t major, uint8_t minor, uint8_t halfway) {
  for (int i = 0; i < 33; i++) {
    // Wait 33 frames for the menu.
    emu->StepFull(0, 0);
  }

  // This is the first frame that we can start the game. We need only
  // press start for one frame to do that. But first modify memory
  // to do the desired warp.
  emu->SetRAM(WORLD_MAJOR, major);
  emu->SetRAM(WORLD_MINOR, minor);
  // XXX This does not match the game, since it does not increment
  // after transition levels like 1-2. What would be the best way
  // to set it outside the main game?
  emu->SetRAM(WORLD_MINOR_DISPLAY, minor);
  emu->SetRAM(HALFWAY_PAGE, halfway);

  emu->StepFull(INPUT_T, 0);

}


ImageRGBA MarioUtil::Screenshot(Emulator *emu) {
  std::vector<uint8_t> rgba8 = emu->GetImage();
  return ImageRGBA(std::move(rgba8), 256, 256);
}
