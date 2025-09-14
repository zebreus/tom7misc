
// Simple utility used in simplification and optimization.

#ifndef _REPHRASE_PROGRESS_H
#define _REPHRASE_PROGRESS_H

#include "ansi.h"
#include "base/print.h"

template<bool VERBOSE>
struct Progress {
  // Call this whenever the expression definitely got smaller.
  void Record(const char *msg) {
    if (VERBOSE) {
      Print(AWHITE("S {}") " " AGREEN("{}") "\n",
            simplified, msg);
    }
    simplified++;
  }

  void Reset() {
    simplified = 0;
  }

  bool MadeProgress() const { return simplified > 0; }

private:
  int simplified = 0;
};


#endif
