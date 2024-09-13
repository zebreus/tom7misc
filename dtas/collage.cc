
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "mario.h"
#include "mario-util.h"

#include "image.h"
#include "../fceulib/emulator.h"
#include "threadutil.h"
#include "periodically.h"
#include "ansi.h"
#include "timer.h"
#include "base/stringprintf.h"

int main(int argc, char **argv) {
  ANSI::Init();
  Timer run_timer;
  Periodically status_per(1.0);

  for (int major = 0; major < 256; major++) {
    std::vector<ImageRGBA> images =
    ParallelTabulate(
        256,
        [major](int minor) {
          std::unique_ptr<Emulator> emu(Emulator::Create("mario.nes"));

          MarioUtil::WarpTo(emu.get(), major, minor, 0);

          // Wait until the level actually starts, which takes about
          // 160 frames, and then a few more so that mario is on-screen.

          for (int skip = 0; skip < 175; skip++)
            emu->StepFull(0, 0);

          // Now a screenshot for our collection.
          return MarioUtil::Screenshot(emu.get());
        },
        16);

    // Create collage of all screens. 16x16.
    ImageRGBA out(256 * 16, 256 * 16);
    for (int i = 0; i < 256; i++) {
      int y = i / 16;
      int x = i % 16;
      out.CopyImageRect(x * 256, y * 256, images[i], 0, 0, 256, 256);
    }

    std::string filename = StringPrintf("world_%d-x.png", major);
    out.Save(filename);

    if (status_per.ShouldRun()) {
      printf(ANSI_UP "%s\n",
             ANSI::ProgressBar(major, 256,
                               filename,
                               run_timer.Seconds()).c_str());
    }
  }

  return 0;
}
