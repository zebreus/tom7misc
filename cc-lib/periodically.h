
// For use in polling loops. Keeps track of a "next time to run"
// and tells the caller when it should run. Thread safe.

#ifndef _CC_LIB_PERIODICALLY_H
#define _CC_LIB_PERIODICALLY_H

#include <atomic>
#include <cstdint>
#include <chrono>
#include <mutex>

struct Periodically {
  // If start_ready is true, then the next call to ShouldRun will return
  // true. Otherwise, we wait for the period to elapse first.
  explicit Periodically(double wait_period_seconds, bool start_ready = true) {
    using namespace std::chrono_literals;
    wait_period = std::chrono::duration_cast<dur>(1s * wait_period_seconds);
    // Immediately available.
    if (start_ready) {
      next_run.store(std::chrono::steady_clock::now());
    } else {
      next_run.store(std::chrono::steady_clock::now() + wait_period);
    }
  }

  // Return true if 'seconds' has elapsed since the last run.
  // If this function returns true, we assume the caller does
  // the associated action now (and so move the next run time
  // forward).
  bool ShouldRun() {
    const tpoint now = std::chrono::steady_clock::now();
    if (now >= next_run.load()) {
      std::unique_lock ml(m);
      // double-checked lock so that the previous can be cheap
      if (now >= next_run.load()) {
        if (paused) return false;
        next_run.store(now + wait_period);
        return true;
      }
    }
    return false;
  }

  void Pause() {
    std::unique_lock ml(m);
    paused = true;
  }

  void Reset() {
    std::unique_lock ml(m);
    paused = false;
    next_run = std::chrono::steady_clock::now() + wait_period;
  }

  // Sets the wait period, and resets the timer, so the next run
  // will be in this many seconds.
  void SetPeriod(double seconds) {
    std::unique_lock ml(m);
    using namespace std::chrono_literals;
    wait_period = std::chrono::duration_cast<dur>(1s * seconds);
    Reset();
  }

private:
  using dur = std::chrono::steady_clock::duration;
  using tpoint = std::chrono::time_point<std::chrono::steady_clock>;
  std::mutex m;
  std::atomic<tpoint> next_run;
  dur wait_period = dur::zero();
  bool paused = false;
};

#endif
