
// Converts from symbolic bytecode to numeric bytecode,
// like a macro assembler would.

#ifndef _REPHRASE_ASSEMBLING_H
#define _REPHRASE_ASSEMBLING_H

#include "bc.h"

#include <string>
#include <utility>
#include <vector>

namespace bc {

struct Assembling {
  Assembling();
  ~Assembling();

  void SetVerbose(int verbose);

  bc::Program Assemble(const bc::SymbolicProgram &pgm);

 private:

  std::pair<std::string, std::vector<Inst>>
  AssembleFn(const SymbolicFn &fn);

  int verbose = 0;
};

}  // namespace bc


#endif
