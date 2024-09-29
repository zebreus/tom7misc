
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

#include "base/stringprintf.h"
#include "image.h"
#include "emulator-pool.h"
#include "minus.h"
#include "ansi.h"
#include "status-bar.h"
#include "threadutil.h"
#include "periodically.h"
#include "timer.h"

#include "../fceulib/simplefm7.h"
#include "../fceulib/emulator.h"

#include "mario-util.h"
#include "mario.h"

#define VALID_IMAGES 0
#define INVALID_IMAGES 1

using SolutionRow = MinusDB::SolutionRow;

static constexpr const char *ROMFILE = "mario.nes";

static constexpr int SOLVED_LEVEL = 1;
static constexpr int SOLVED_PRINCESS = 2;
static constexpr int SOLVED_FLAGPOLE = 4;

static int Solved(const Emulator *emu, LevelId level) {
  const auto &[major, minor] = UnpackLevel(level);

  const bool next_level =
    emu->ReadRAM(WORLD_MAJOR) > major ||
    emu->ReadRAM(WORLD_MINOR) > minor;

  const uint8_t oper_mode = emu->ReadRAM(OPER_MODE);
  const uint8_t oper_task = emu->ReadRAM(OPER_MODE_TASK);

  // We could also count tasks 0, 1, as these are normal
  // speedrun rules?
  const bool princess = oper_mode == 2 && (oper_task == 3 || oper_task == 4);

  const uint8_t subroutine = emu->ReadRAM(GAME_ENGINE_SUBROUTINE);
  const bool flagpole =
      // slide down flagpole
      subroutine == 0x04 ||
      // walk to exit
      subroutine == 0x05;

  int out = 0;
  if (next_level) out |= SOLVED_LEVEL;
  if (princess) out |= SOLVED_PRINCESS;
  if (flagpole) out |= SOLVED_FLAGPOLE;

  return out;
}

static void Validate() {
  MinusDB db;

  std::vector<SolutionRow> all = db.GetSolutions();
  printf("Validating %d solutions.\n", (int)all.size());

  EmulatorPool emulator_pool(ROMFILE);

  std::vector<uint8_t> start_state;
  {
    EmulatorPool::Lease emu = emulator_pool.Acquire();
    start_state = emu->SaveUncompressed();
  }

  StatusBar status(2);

  std::mutex m;
  int64_t valid = 0;
  int64_t invalid = 0;

  Periodically status_per(1.0);
  Timer timer;

  ParallelApp(
      all,
      [&](const SolutionRow &row) {
        const auto &[major, minor] = UnpackLevel(row.level);
        EmulatorPool::Lease emu = emulator_pool.Acquire();
        emu->LoadUncompressed(start_state);
        MarioUtil::WarpTo(emu.get(), major, minor, 0);

        for (uint8_t b : row.movie) {
          emu->Step(b, 0);
        }

        int solved = Solved(emu.get(), row.level);
        if (solved != 0) {
          if (VALID_IMAGES) {
            ImageRGBA img = MarioUtil::Screenshot(emu.get());
            std::string flags;
            if (solved & SOLVED_LEVEL) flags += "LEVEL ";
            if (solved & SOLVED_PRINCESS) flags += "PRINCESS ";
            if (solved & SOLVED_FLAGPOLE) flags += "FLAGPOLE ";
            img.BlendText32(1, 1, 0xFF00FFFF, flags);
            img.Save(StringPrintf("valid-%d-%d.png",
                                  major, minor));
          }

          MutexLock ml(&m);
          valid++;
        } else {
          db.DeleteSolution(row.id);
          status.Printf(
              "Invalid for %s: " AGREY("%s") ". " ARED("Deleted") ".\n",
              ColorLevel(row.level).c_str(),
              SimpleFM7::EncodeOneLine(row.movie).c_str());
          if (INVALID_IMAGES) {
            MarioUtil::Screenshot(emu.get()).Save(
                StringPrintf("invalid-%d-%d.png",
                             major, minor));
          }
          MutexLock ml(&m);
          invalid++;
        }

        status_per.RunIf([&]() {
            int64_t done = 0;
            std::string msg;
            {
              MutexLock ml(&m);
              done += valid;
              done += invalid;
              msg = StringPrintf(AGREEN("%lld") " + " ARED("%lld"),
                                 valid, invalid);
            }
            status.Emit(ANSI::ProgressBar(
                            done, all.size(),
                            StringPrintf("Validate: %s", msg.c_str()),
                            timer.Seconds()));
          });
      },
      8);

  printf("Done. " AGREEN("%lld") " are valid. "
         ARED("%lld") " are invalid.\n",
         valid, invalid);
}

int main(int argc, char **argv) {
  ANSI::Init();

  Validate();

  return 0;
}
