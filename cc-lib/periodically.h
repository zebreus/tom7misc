
// For use in polling loops. Keeps track of a "next time to run"
// and tells the caller when it should run. Not thread-safe.

#ifndef _CC_LIB_PERIODICALLY_H
#define _CC_LIB_PERIODICALLY_H

#include <cstdint>
#include <chrono>

struct Periodically {
  Periodically(double wait_period_seconds) {
	using namespace std::chrono_literals;	
	wait_period = std::chrono::duration_cast<dur>(1s * wait_period_seconds);
	// Immediately available.
    next_run = std::chrono::steady_clock::now();
  }

  // Return true if 'seconds' has elapsed since the last run.
  // If this function returns true, we assume the caller does
  // the associated action now (and so move the next run time
  // forward).
  bool ShouldRun() {
    if (paused) return false;
    const tpoint now = std::chrono::steady_clock::now();
    if (now >= next_run) {
      next_run = now + wait_period;
      return true;
    }
    return false;
  }

  void Pause() {
    paused = true;
  }

  void Reset() {
    paused = false;
    next_run = std::chrono::steady_clock::now() + wait_period;
  }

private:
  using dur = std::chrono::steady_clock::duration;
  using tpoint = std::chrono::time_point<std::chrono::steady_clock>;
  tpoint next_run;
  dur wait_period = dur::zero();
  bool paused = false;
};

#endif
