
// Converts from simplified (closure-converted) IL to bytecode.

#ifndef _REPHRASE_TO_BYTECODE_H
#define _REPHRASE_TO_BYTECODE_H

#include "il.h"
#include "bc.h"

namespace bc {

struct ToBytecode {
  ToBytecode();

  void SetVerbose(int verbose);

  bc::SymbolicProgram Convert(const il::Program &pgm);

 private:
  int verbose = 0;
};

}  // namespace bc


#endif
