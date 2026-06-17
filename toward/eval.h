
#ifndef _TOWARD_EVAL_H
#define _TOWARD_EVAL_H

#include "letters.h"
#include "image.h"

// All evals should be thought of as a "Penalty", where
// lower is "better" (i.e. more stable).
struct Eval {

  static double Stability(const Letter &letter);

  static ImageRGBA DebugStability(const Letter &letter);

};

#endif
