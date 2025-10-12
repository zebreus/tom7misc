
#ifndef _DTAS_EVALUATOR_H
#define _DTAS_EVALUATOR_H

#include <cstdint>

#include "emulator-pool.h"
#include "mario-util.h"

struct Emulator;
struct Evaluator {
  // Initialize with some emulator state; we use that to read the current
  // level and lives and so on.
  Evaluator(EmulatorPool *emulator_pool, const Emulator *src_emu);

  // True if the level did not start after 200 frames.
  bool NeverStarted() const { return never_started; }

  // Bitmasks.
  static constexpr int SOLVED_LEVEL = 1;
  static constexpr int SOLVED_PRINCESS = 2;
  static constexpr int SOLVED_FLAGPOLE = 4;
  int HowSolved(const Emulator *emu) const;

  // True if we've beaten the level.
  // This is only accurate if you also check Stuck() on
  // every intermediate frame, because it is usually possible
  // to die and restart the entire game, and then beat some levels.
  bool Succeeded(const Emulator *emu) const {
    return HowSolved(emu) != 0;
  }

  // Is Mario dead? Detects both running into an enemy (or
  // out of time), as well as falling off the bottom of the
  // screen.
  bool IsDead(const Emulator *emu) const;

  // As in, is this solution path stuck?
  // Returns true if the game is back on the main menu,
  // or Mario is dead, or has fewer lives than when he
  // started.
  bool Stuck(const Emulator *emu) const;

  // General purpose heuristic evaluation of the current
  // state. Higher scores are better.
  double Eval(const Emulator *emu) const;


  MarioUtil::Pos start_pos{.x = 0, .y = 0};
  uint8_t world_major = 0, world_minor = 0;
  uint8_t start_lives = 0;
  bool never_started = false;
};

#endif
