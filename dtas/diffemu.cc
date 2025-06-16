
#include <format>
#include <memory>
#include <cstdint>

#include "../fceulib/emulator.h"
#include "../fceulib/simplefm2.h"

#include "ansi.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "image.h"
#include "mario-util.h"
#include "mario.h"
#include "status-bar.h"

static constexpr const char *ROMFILE = "mario.nes";

// menu_frames should be at least 33
static void WarpTo(Emulator *emu,
                   uint8_t major, uint8_t minor, uint8_t halfway,
                   int menu_frames) {

  for (int i = 0; i < menu_frames; i++) {
    emu->StepFull(0, 0);
  }

  // After 33 frames we can start the game. We need only press start
  // for one frame to do that. But first modify memory to do the
  // desired warp.
  emu->SetRAM(WORLD_MAJOR, major);
  emu->SetRAM(WORLD_MINOR, minor);
  // XXX This does not match the game, since it does not increment
  // after transition levels like 1-2. What would be the best way
  // to set it outside the main game?
  emu->SetRAM(WORLD_MINOR_DISPLAY, minor);
  emu->SetRAM(HALFWAY_PAGE, halfway);

  emu->StepFull(INPUT_T, 0);
}

static void Diff2() {
  std::unique_ptr<Emulator> emu1(Emulator::Create(ROMFILE));
  std::unique_ptr<Emulator> emu2(Emulator::Create(ROMFILE));

  // Timer expires immediately (?)
  WarpTo(emu1.get(), 0x1F, 0xFA, 0x00, 33);
  // Now make two steps so that they are synchronized in time.
  emu1->StepFull(0, 0);
  emu1->StepFull(0, 0);

  // Mario drops
  WarpTo(emu2.get(), 0x1F, 0xFA, 0x00, 35);


  int frame_num = 35;

  StatusBar status(1);
  static constexpr int MARGIN = 8;
  // Now do the diff.
  for (int i = 0; i < 300; i++) {
    ImageRGBA sxs(256 + MARGIN + 256, 256);
    sxs.Clear32(0x000000FF);

    int TOP = 16;

    sxs.CopyImage(0, TOP, MarioUtil::Screenshot(emu1.get()));
    sxs.CopyImage(256 + MARGIN, TOP, MarioUtil::Screenshot(emu2.get()));

    sxs.BlendText32(1, 1, 0xFFFFFFFF, std::format("Frame {}.", frame_num));

    sxs.Save(std::format("diff/frame{}.png", frame_num));
    frame_num++;
    status.Print("{}/{}", i, 300);

    emu1->StepFull(0, 0);
    emu2->StepFull(0, 0);
  }
  status.Printf("Done.\n");

}



int main(int argc, char **argv) {
  ANSI::Init();

  Diff2();

  return 0;
}
