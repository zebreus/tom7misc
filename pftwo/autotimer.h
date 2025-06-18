// Tries to automatically detect memory locations that are timers,
// either count-down or count-up. Such locations increment or
// decrement at a nearly-regular frequency (some jitter is allowed)
// during most normal play, not depending on the input.
//
// The reason we do this today is to filter out such locations for
// consideration in autolives; timers look just like "lives" because
// when they go to 0, a traumatic event happens.
//
// There may be other uses for the signal (for example, they should
// probably be filtered out of objective functions, too?).
//
// TODO: Game timers are often represented as multibyte quantities,
// usually using BCD (but usually in adjacent bytes or nybbles).

#ifndef _PFTWO_AUTOTIMER_H
#define _PFTWO_AUTOTIMER_H

#include <cstdint>
#include <string>
#include <vector>

#include "n-markov-controller.h"
#include "random-pool.h"
#include "emulator-pool.h"

// Thread safe.
struct AutoTimer {
  // Creates some private emulator instances that it can reuse.
  // Wants a markov model for generating inputs.
  AutoTimer(const std::string &game,
            NMarkovController nmarkov);
  ~AutoTimer();

  struct TimerLoc {
    int loc = 0;
    float score = 0.0f;
    // Average period, in frames.
    float period = 0;
    // True if it's a count-up timer.
    bool incrementing = false;
    TimerLoc() {}
    TimerLoc(int loc, float score, float period, bool incrementing) :
      loc(loc), score(score), period(period), incrementing(incrementing) {}
  };

  // Find and score memory locations that may be timers. Expensive
  // since it needs to simulate frames.
  std::vector<TimerLoc> FindTimers(const std::vector<uint8_t> &save);

  // Merge and sort by summing scores.
  static std::vector<TimerLoc> MergeTimers(
      const std::vector<std::vector<TimerLoc>> &lv);

 private:
  RandomPool random_pool;
  EmulatorPool emulator_pool;
  const NMarkovController nmarkov;
};

#endif
