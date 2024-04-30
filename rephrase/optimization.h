
// Basic block optimization (symbolic bytecode).

#ifndef _REPHRASE_OPTIMIZATION_H
#define _REPHRASE_OPTIMIZATION_H

#include <cstdint>

#include "bc.h"

namespace bc {

struct Optimization {
  Optimization();

  static constexpr uint64_t O_DEAD_INST = 1ULL << 1;
  static constexpr uint64_t O_DEAD_BLOCK = 1ULL << 2;
  static constexpr uint64_t O_INLINE_BLOCK = 1ULL << 3;

  static constexpr uint64_t O_ALL = ~0ULL;

  void SetVerbose(int verbose);

  SymbolicProgram Optimize(const SymbolicProgram &, uint64_t opts = O_ALL);

 private:
  int verbose = 0;
};

}  // bc

#endif
