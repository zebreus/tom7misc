
#ifndef _DTAS_EVALUATOR_H
#define _DTAS_EVALUATOR_H

#include <cstdint>

#include "../fceulib/emulator.h"

#include "emulator-pool.h"
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
      // Print("{}: {:02x}.{:02x}\n", i, mode, task);
      if (mode == 1 && task == 3) {
        // Playing.
        // Print("{}: Started! {:02x}.{:02x}\n\n\n\n", i, mode, task);
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

  // Bitmasks.
  static constexpr int SOLVED_LEVEL = 1;
  static constexpr int SOLVED_PRINCESS = 2;
  static constexpr int SOLVED_FLAGPOLE = 4;

  int HowSolved(const Emulator *emu) const {
    const uint8_t now_maj = emu->ReadRAM(WORLD_MAJOR);
    const uint8_t now_min = emu->ReadRAM(WORLD_MINOR);

    // Normally the level increases, but some levels have warp
    // zones that go backwards. So we treat any level change
    // as winning.
    const bool change_level =
      // If we don't have a valid start level, don't allow
      // winning via this condition.
      !never_started &&
      // Don't allow warping to 0-0, since dying and returning
      // and starting again is not "winning". It would be better
      // if the evaluator saw every frame in between, and
      // put us in a dead state if we ever see the main menu.
      !(now_maj == 0 &&
        now_min == 0) &&
      (now_maj != world_major ||
       now_min != world_minor);

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

    int ret = 0;
    if (change_level) ret |= SOLVED_LEVEL;
    if (princess) ret |= SOLVED_PRINCESS;
    if (flagpole) ret |= SOLVED_FLAGPOLE;

    return ret;
  }

  // True if we've beaten the level.
  // This is only accurate if you also check Stuck() on
  // every intermediate frame, because it is usually possible
  // to die and restart and then beat some levels.
  bool Succeeded(const Emulator *emu) const {
    return HowSolved(emu) != 0;
  }

  bool IsDead(const Emulator *emu) const {
    // Two kinds of death. When you run into an enemy or run
    // out of time, the subroutine becomes 11 and mario goes
    // into his death animation.
    //
    // If you fall into a pit, the player's major Y coordinate
    // becomes 2 or greater (and in fact we enter this case
    // from the death animation).
    const uint8_t mode = emu->ReadRAM(OPER_MODE);
    const uint8_t task = emu->ReadRAM(OPER_MODE_TASK);
    const uint8_t subroutine =
      emu->ReadRAM(GAME_ENGINE_SUBROUTINE);
    if (mode == 1 && task == 3) {
      // Death animation.
      if (subroutine == 11)
        return true;

      const uint8_t yscreen = emu->ReadRAM(PLAYER_Y_SCREEN);
      if (yscreen >= 2)
        return true;
    }

    return false;
  }

  bool Stuck(const Emulator *emu) const {
    // This is the main menu.
    if (emu->ReadRAM(OPER_MODE) == 0)
      return true;

    // Note: Occasionally dying can be useful to warp to the halfway
    // point.
    if (IsDead(emu)) return true;

    // Death not detected by the above. Necessary?
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
