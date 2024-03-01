
// Performs typed closure conversion.

#ifndef _REPHRASE_CLOSURE_CONVERSION_H
#define _REPHRASE_CLOSURE_CONVERSION_H

#include "il.h"

namespace il {

struct ClosureConversion {
  ClosureConversion(AstPool *pool);

  void SetVerbose(int verbose);

  Program Convert(const Program &pgm);

  // Options that can be used for simplification
  // after closure conversion.
  static uint64_t SimplificationOpts();

private:
  int verbose = 0;
  AstPool *pool;
};

}  // il


#endif

