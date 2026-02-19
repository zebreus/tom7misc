
// Simple utility used in simplification and optimization.
// Thread safe.

#ifndef _REPHRASE_PROGRESS_H
#define _REPHRASE_PROGRESS_H

#include <mutex>

#include "ansi.h"
#include "base/print.h"
#include "threadutil.h"

template<bool VERBOSE>
struct Progress {
  // Call this whenever the expression definitely got smaller.
  void Record(const char *msg) {
    {
      MutexLock ml(&mu);
      if constexpr (VERBOSE) {
        Print(AWHITE("S {}") " " AGREEN("{}") "\n",
              simplifications, msg);
      }

      simplifications++;
    }
  }

  void Reset() {
    MutexLock ml(&mu);
    simplifications = 0;
  }

  bool MadeProgress() {
    MutexLock ml(&mu);
    return simplifications > 0;
  }

 private:
  // PERF: Could maybe make this faster in the non-verbose
  // case by using std::atomic_flag?
  std::mutex mu;
  int simplifications = 0;
};


#endif
