
#ifndef _REPHRASE_SIMPLIFICATION_H
#define _REPHRASE_SIMPLIFICATION_H

#include <string>
#include <vector>

#include "il.h"

namespace il {

struct Simplification {
  Simplification(AstPool *pool);

  void SetVerbose(int verbose);

  Program Simplify(const Program &);

private:
  int verbose = 0;
  AstPool *pool;
};

}  // il

#endif
