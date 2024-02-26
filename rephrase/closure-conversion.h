
// Performs typed closure conversion.

#ifndef _REPHRASE_CLOSURE_CONVERSION_H
#define _REPHRASE_CLOSURE_CONVERSION_H

#include "il.h"

namespace il {

struct ClosureConversion {
  ClosureConversion(AstPool *pool);

  void SetVerbose(int verbose);

  Program Convert(const Program &);

private:
  int verbose = 0;
  AstPool *pool;
};

}  // il


#endif

