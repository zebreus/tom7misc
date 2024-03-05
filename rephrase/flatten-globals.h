
// Simple pass that flattens globals to satisfy the requirements
// of to-bytecode. Flattening means that a record's fields are
// only GlobalSym (after erasing types).

#ifndef _REPHRASE_FLATTEN_GLOBALS_H
#define _REPHRASE_FLATTEN_GLOBALS_H

#include "il.h"

namespace il {

struct FlattenGlobals {
  explicit FlattenGlobals(AstPool *pool);

  void SetVerbose(int v);

  Program Flatten(const Program &pgm);

 private:
  int verbose = 0;
  AstPool *pool;
};

}

#endif
