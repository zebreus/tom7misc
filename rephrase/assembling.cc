
#include "assembling.h"

#include <string>
#include <vector>
#include <utility>

#include "bc.h"
#include "base/logging.h"

namespace bc {

Assembling::Assembling() {}
Assembling::~Assembling() {}

void Assembling::SetVerbose(int verbose_in) {
  verbose = verbose_in;
}

bc::Program Assembling::Assemble(const SymbolicProgram &sympgm) {
  bc::Program pgm;
  for (const auto &[name, fn] : sympgm.code) {
    CHECK(fn.blocks.size() == 1);
    pgm.code[name] = std::make_pair(fn.arg, fn.blocks.begin()->second.insts);
  }
  pgm.data = sympgm.data;
  return pgm;
}

}  // namespace bc
