
#include <cstdint>
#include <cstdio>
#include <format>
#include <mutex>
#include <string>
#include <vector>

#include "ansi.h"
#include "base/print.h"
#include "emulator-pool.h"
#include "image.h"
#include "minus.h"
#include "periodically.h"
#include "status-bar.h"
#include "threadutil.h"
#include "timer.h"

#include "../fceulib/simplefm7.h"
#include "../fceulib/emulator.h"

#include "evaluator.h"
#include "mario-util.h"

#define VALID_IMAGES 0
#define INVALID_IMAGES 1
#define DELETE_INVALID 1

using SolutionRow = MinusDB::SolutionRow;

static constexpr const char *ROMFILE = "mario.nes";

static void Validate() {
  MinusDB db;

  std::vector<SolutionRow> all = db.GetAllSolutions();
  Print("Validating {} solutions.\n", all.size());

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
        Evaluator eval(&emulator_pool, emu.get());

        bool failed = false;
        for (uint8_t b : row.movie) {
          emu->Step(b, 0);
          if (eval.Stuck(emu.get())) {
            failed = true;
            break;
          }
        }

        int solved = eval.HowSolved(emu.get());
        if (!failed && solved != 0) {
          if (VALID_IMAGES) {
            ImageRGBA img = MarioUtil::Screenshot(emu.get());
            std::string flags;
            if (solved & Evaluator::SOLVED_LEVEL) flags += "LEVEL ";
            if (solved & Evaluator::SOLVED_PRINCESS) flags += "PRINCESS ";
            if (solved & Evaluator::SOLVED_FLAGPOLE) flags += "FLAGPOLE ";
            img.BlendText32(1, 1, 0xFF00FFFF, flags);
            img.Save(std::format("valid-{}-{}.png",
                                 major, minor));
          }

          MutexLock ml(&m);
          valid++;
        } else {
          std::string what =
            failed ? AORANGE("failed") : AYELLOW("not solved");
          if (DELETE_INVALID) {
            db.DeleteSolution(row.id);
          }
          status.Print(
              AWHITE("#{}") " Invalid ({}) for {}: " AGREY("{}") ".{}\n",
              row.id,
              what,
              ColorLevel(row.level),
              SimpleFM7::EncodeOneLine(row.movie),
              DELETE_INVALID ? " " ARED("Deleted") "." : "");
          if (INVALID_IMAGES) {
            MarioUtil::Screenshot(emu.get()).Save(
                std::format("invalid-{}-{}.png",
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
              msg = std::format(AGREEN("{}") " + " ARED("{}"),
                                valid, invalid);
            }
            status.Emit(ANSI::ProgressBar(
                            done, all.size(),
                            std::format("Validate: {}", msg),
                            timer.Seconds()));
          });
      },
      12);

  Print("\n\n"
        "Done. " AGREEN("{}") " are valid. "
        ARED("{}") " are invalid.\n",
        valid, invalid);
}

int main(int argc, char **argv) {
  ANSI::Init();

  Validate();

  return 0;
}
