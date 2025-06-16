
#include <cstdio>
#include <cstring>
#include <format>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "../fceulib/emulator.h"
#include "ansi.h"
#include "base/stringprintf.h"
#include "image.h"
#include "periodically.h"
#include "threadutil.h"
#include "timer.h"

#include "minus.h"
#include "mario.h"
#include "mario-util.h"
#include "emulator-pool.h"
#include "atomic-util.h"

DECLARE_COUNTERS(images_done, u2, u3, u4, u5, u6, u7, u8);

static constexpr const char *ROMFILE = "mario.nes";

static EmulatorPool *emulator_pool = nullptr;

static ImageRGBA Render(LevelId level) {
  auto emu = emulator_pool->AcquireClean();
  const auto &[major, minor] = UnpackLevel(level);
  MarioUtil::WarpTo(emu.get(), major, minor, 0);

  // Wait until the level actually starts, which takes about
  // 160 frames, and then a few more so that mario is on-screen.

  for (int skip = 0; skip < 175; skip++)
    emu->StepFull(0, 0);

  // Now a screenshot for our collection.
  return MarioUtil::Screenshot(emu.get());
}

int main(int argc, char **argv) {
  ANSI::Init();

  emulator_pool = new EmulatorPool(ROMFILE);
  CHECK(emulator_pool != nullptr);

  bool only_unsolved = false;
  if (argc == 2) {
    CHECK((std::string)argv[1] == "unsolved");
    only_unsolved = true;
  }

  Timer run_timer;
  Periodically status_per(1.0);

  if (only_unsolved) {
    MinusDB db;
    // Has a definitive answer.
    std::unordered_set<LevelId> solved = db.GetSolved();
    std::unordered_set<LevelId> rejected = db.GetRejected();

    std::vector<LevelId> unknown;
    for (int i = 0; i < 65536; i++) {
      if (!solved.contains(i) &&
          !rejected.contains(i)) {
        unknown.push_back(i);
      }
    }

    const int num_images = (int)unknown.size();
    std::string filename = "unsolved-collage.png";

    printf("Rendering %d unsolved levels...\n\n", num_images);
    std::vector<ImageRGBA> images =
      ParallelMap(
          unknown,
          [&status_per, &run_timer, num_images, &filename](LevelId level) {
            ImageRGBA img = Render(level);
            const auto &[major, minor] = UnpackLevel(level);
            img.BlendText32(1, 1, 0x00FFFFFF,
                            std::format("{:02x}-{:02x}", major, minor));

            images_done++;
            if (status_per.ShouldRun()) {
              printf(ANSI_UP "%s\n",
                     ANSI::ProgressBar(images_done.Read(), num_images,
                                       filename,
                                       run_timer.Seconds()).c_str());
            }

            return img;
          },
          16);

    printf("\nWrite huge image...\n");

    // How big does it need to be?
    const int SCREENS_WIDE = 256;
    const int SCREENS_TALL = (images.size() / SCREENS_WIDE) +
      (images.size() % SCREENS_WIDE == 0 ? 0 : 1);

    ImageRGBA out(256 * SCREENS_WIDE, 240 * SCREENS_TALL);
    out.Clear32(0x000000FF);

    for (int i = 0; i < (int)images.size(); i++) {
      int y = i / SCREENS_WIDE;
      int x = i % SCREENS_WIDE;
      out.CopyImageRect(x * 256, y * 240, images[i], 0, 0, 256, 240);
    }

    printf("Save to disk...\n");
    out.Save(filename);
    printf("Done. Wrote to " AGREEN("%s") ".\n", filename.c_str());

  } else {
    // All of them, grouped by world.
    for (int major = 0; major < 256; major++) {
      std::vector<ImageRGBA> images = ParallelTabulate(
          256, [major](int minor) { return Render(PackLevel(major, minor)); },
          16);

      // Create collage of all screens in this world. 16x16.
      ImageRGBA out(256 * 16, 256 * 16);
      for (int i = 0; i < 256; i++) {
        int y = i / 16;
        int x = i % 16;
        out.CopyImageRect(x * 256, y * 256, images[i], 0, 0, 256, 256);
      }

      std::string filename = std::format("world_{}-x.png", major);
      out.Save(filename);

      if (status_per.ShouldRun()) {
        printf(ANSI_UP "%s\n",
               ANSI::ProgressBar(major, 256, filename,
                                 run_timer.Seconds()).c_str());
      }
    }
  }

  return 0;
}
