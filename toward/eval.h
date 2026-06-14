
#ifndef _TOWARD_EVAL_H
#define _TOWARD_EVAL_H

#include "letters.h"

// All evals should be thought of as a "Penalty", where
// lower is "better" (i.e. more stable).
struct Eval {

  static double Stability(const Letter &letter);

};

#endif
