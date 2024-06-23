
#ifndef _REPHRASE_MONOMORPHIZATION_H
#define _REPHRASE_MONOMORPHIZATION_H

#include "il.h"

namespace il {

// Experimental monomorphization phase. It's not hooked up or even
// implemented yet; there's just some notes in the .cc file.
struct Monomorphization {
  Monomorphization(AstPool *pool);

  void SetVerbose(int verbose);

  Program Monomorphize(const Program &);

 private:
  int verbose = 0;
  AstPool *pool;
};

}  // il

#endif
