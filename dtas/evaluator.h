
#ifndef _DTAS_EVALUATOR_H
#define _DTAS_EVALUATOR_H

#include <cstdint>

#include "emulator-pool.h"
#include "../fceulib/emulator.h"
#include "mario.h"
#include "mario-util.h"

struct Evaluator {
  // Initialize with some emulator state; we use that to read the current
  // level and lives and so on.
  Evaluator(EmulatorPool *emulator_pool, const Emulator *src_emu) {
    // Clone the source emulator temporarily so that we can
    // mess around with it.
    CHECK(emulator_pool != nullptr);
    EmulatorPool::Lease emu = emulator_pool->Acquire();
    emu->LoadUncompressed(src_emu->SaveUncompressed());

    bool started = false;

    for (int i = 0; i < 200; i++) {
      const uint8_t mode = emu->ReadRAM(OPER_MODE);
      const uint8_t task = emu->ReadRAM(OPER_MODE_TASK);
      // printf("%d: %02x.%02x\n", i, mode, task);
      if (mode == 1 && task == 3) {
        // Playing.
        // printf("%d: Started! %02x.%02x\n\n\n\n", i, mode, task);
        started = true;
        break;
      }
      emu->Step(0, 0);
    }

    // Probably don't use these values if never_started is true.
    world_major = emu->ReadRAM(WORLD_MAJOR);
    world_minor = emu->ReadRAM(WORLD_MINOR);
    start_lives = emu->ReadRAM(NUMBER_OF_LIVES);

    start_pos = MarioUtil::GetPos(emu.get());

    never_started = !started;
  }

  bool NeverStarted() const { return never_started; }

  // True if we've beaten the level.
  bool Succeeded(const Emulator *emu) const {
    // XXX I think there is also something like "win flag" for when
    // you finish the game. We need to detect this since many levels
    // are won by defeating Bowser, king of the shell people.

    const bool next_level =
      emu->ReadRAM(WORLD_MAJOR) > world_major ||
      emu->ReadRAM(WORLD_MINOR) > world_minor;

    const uint8_t oper_mode = emu->ReadRAM(OPER_MODE);
    const uint8_t oper_task = emu->ReadRAM(OPER_MODE_TASK);

    // We could also count tasks 0, 1, as these are normal
    // speedrun rules?
    const bool princess =
      oper_mode == 2 &&
      (oper_task == 3 || oper_task == 4);

    const uint8_t subroutine =
      emu->ReadRAM(GAME_ENGINE_SUBROUTINE);
    const bool flagpole =
      // slide down flagpole
      subroutine == 0x04 ||
      // walk to exit
      subroutine == 0x05;

    return next_level || princess || flagpole;
  }

  bool Stuck(const Emulator *emu) const {
    // XXX Detect main menu.

    // Note: Occasionally dying can be useful to warp to the halfway
    // point.
    if (emu->ReadRAM(NUMBER_OF_LIVES) < start_lives) {
      return true;
    }

    return false;
  }

  double Eval(const Emulator *emu) const {
    const MarioUtil::Pos pos = MarioUtil::GetPos(emu);
    // double xfrac = x / (double)0xFFFF;

    // Decimal part of score is x position (unless penalized);
    // fractional part is heuristics.
    double score = pos.x;

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
    // 256-512 is on-screen.
    if (pos.y > 511) {
      // Very bad to be off screen downward, as we will
      // definitely die.
      return -200000.0 + score;
    } else if (pos.y > 256 + 176) {
      // When on screen, we're uncomfortable if we're below 176,
      // which is below the bottom two rows of bricks.

      int depth = pos.y - (256 + 176);
      return -100000.0 - depth + score;
    } else {
      // Otherwise we're not falling off the screen. But give
      // some bonus when we're higher up (lower y coordinate).
      int height = -(pos.y - (256 + 176));

      return score + (height * 0.05);
    }
  }

  MarioUtil::Pos start_pos{.x = 0, .y = 0};
  uint8_t world_major = 0, world_minor = 0;
  uint8_t start_lives = 0;
  bool never_started = false;
};

#endif
