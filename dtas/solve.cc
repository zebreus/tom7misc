
// Solves Mario Bros.; kinda like playfun but with as much
// game-specific logic as desired.

#include <functional>
#include <memory>
#include <cstdio>
#include <cstdint>
#include <mutex>
#include <vector>

#include "../fceulib/emulator.h"
#include "../fceulib/simplefm2.h"
#include "../fceulib/simplefm7.h"

#include "image.h"
#include "timer.h"
#include "periodically.h"
#include "threadutil.h"
#include "arcfour.h"
#include "randutil.h"
#include "ansi.h"
#include "timer.h"

#include "base/stringprintf.h"
#include "base/logging.h"

#include "mario.h"
#include "mario-util.h"

using uint8 = uint8_t;

static constexpr const char *ROMFILE = "mario.nes";

struct Database {
  std::vector

};

// We're not trying to find a particularly short solution, just a solution.

struct Evaluator {
  // Initialize with some emulator state; we use that to read the current
  // level and lives and so on.
  Evaluator(const Emulator *emu) {
    world_major = emu->ReadRAM(WORLD_MAJOR);
    world_minor = emu->ReadRAM(WORLD_MINOR);

    start_lives = emu->ReadRAM(NUMBER_OF_LIVES);
  }

  // True if we've beaten the level.
  bool Succeeded(const Emulator *emu) const {
    // XXX I think there is also something like "win flag" for when
    // you finish the game. We need to detect this since many levels
    // are won by defeating Bowser, king of the shell people.
    return emu->ReadRAM(WORLD_MAJOR) > world_major ||
      emu->ReadRAM(WORLD_MINOR) > world_minor;
  }

  bool Stuck(const Emulator *emu) const {
    // XXX Detect main menu.

    // Note: Occasionally dying can be useful to warp to the halfway
    // point.
    if (emu->ReadRAM(start_lives) < start_lives) {
      return true;
    }

    return false;
  }

  double Eval(const Emulator *emu) const {
    uint8_t xhi = emu->ReadRAM(PLAYER_X_HI);
    uint8_t xlo = emu->ReadRAM(PLAYER_X_LO);

    uint16_t x = (uint16_t(xhi) << 8) | xlo;
    // double xfrac = x / (double)0xFFFF;

    // Decimal part of score is x position (unless penalized);
    // fractional part is heuristics.
    double score = x;

    // Heuristics:

    // More time is better.
    const int timer =
      emu->ReadRAM(TIMER1) * 100 +
      emu->ReadRAM(TIMER2) * 10 +
      emu->ReadRAM(TIMER3);

    const double tfrac = (timer / (double)400);

    score += tfrac;

    // TODO: Better to have high x velocity.

    // Heuristics: Better to be high on the screen.

    // 0 if above screen, 1 if on screen, 2+ if below
    uint8_t yscreen = emu->ReadRAM(PLAYER_Y_SCREEN);
    uint8_t ypos = emu->ReadRAM(PLAYER_Y);

    // 256-512 is on-screen.
    uint16_t y = (uint16_t(yscreen) << 8) | ypos;
    if (y > 511) {
      // Very bad to be off screen downward, as we will
      // definitely die.
      return -200000.0 + score;
    } else if (y > 256 + 176) {
      // When on screen, we're uncomfortable if we're below 176,
      // which is below the bottom two rows of bricks.

      int depth = y - (256 + 176);
      return -100000.0 - depth + score;
    } else {
      // Otherwise we're not falling off the screen. But give
      // some bonus when we're higher up (lower y coordinate).
      int height = -(y - (256 + 176));

      return score + (height * 0.05);
    }
  }

  uint8_t world_major = 0, world_minor = 0;
  uint8_t start_lives = 0;
};

struct Solution {
  // Inputs starting after the WarpTo.
  std::vector<uint8_t> movie;
  double eval = 0.0;
};

// First a quick-and-dirty solver. This is more or less the playfun
// algorithm, but special-cased to a mario-specific objective.
struct Solver {

  std::mutex should_die_m;
  bool should_die = false;

  std::mutex popm;
  std::vector<Solution> population;

  bool ShouldDie() {
    std::unique_lock<std::mutex> ml(should_die_m);
    return should_die;
  }

  void WorkThread(uint64_t seed) {
    ArcFour rc(seed);
    std::unique_ptr<Emulator> emu(Emulator::Create(ROMFILE));
    emu->LoadUncompressed(start_state);

    for (;;) {
      if (ShouldDie()) return;

      // Select a movie from the population (weighted random?)
      // Try n random futures.
      // If any of these end up successful, we are done.
      // Otherwise pick one (weighted random?) and add it to the population.
      // Reduce population if it's big enough.
    }
  }

  Solver(uint8_t major, uint8_t minor) {
    std::unique_ptr<Emulator> emu(Emulator::Create(ROMFILE));
    CHECK(emu.get() != nullptr) << ROMFILE;
    MarioUtil::WarpTo(emu.get(), major, minor, 0);
    start_state = emu->SaveUncompressed();

    evaluator.reset(new Evaluator(emu.get()));

    CHECK(!evaluator->Stuck(emu.get()));

    Solution empty;
    empty.movie = {};
    empty.eval = evaluator->Eval(emu.get());

    // XXX start the appropriate number of threads here.
  }

  std::vector<uint8_t> start_state;
  std::unique_ptr<Evaluator> evaluator;
};


static void Solve(Emulator *emu) {



}


int main(int argc, char **argv) {
  ANSI::Init();

  return 0;
}
