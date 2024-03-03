
#ifndef _REPHRASE_SIMPLIFICATION_H
#define _REPHRASE_SIMPLIFICATION_H

#include <string>
#include <vector>

#include "il.h"

namespace il {

struct Simplification {
  Simplification(AstPool *pool);

  static constexpr uint64_t O_DEAD_VARS = 1ULL << 1;
  static constexpr uint64_t O_REDUCE = 1ULL << 2;
  static constexpr uint64_t O_MAKE_NONRECURSIVE = 1ULL << 3;
  static constexpr uint64_t O_ETA_CONTRACT = 1ULL << 4;
  static constexpr uint64_t O_INLINE_EXP = 1ULL << 5;
  static constexpr uint64_t O_DEAD_CODE = 1ULL << 6;
  static constexpr uint64_t O_FLATTEN = 1ULL << 7;

  static constexpr uint64_t O_GLOBAL_INLINING = 1ULL << 32;
  static constexpr uint64_t O_GLOBAL_DEAD = 1ULL << 33;

  // We require these optimizations to remove some constructs
  // before bytecode generation.
  static constexpr uint64_t O_DECOMPOSE_INTCASE = 1ULL << 50;
  static constexpr uint64_t O_DECOMPOSE_STRINGCASE = 1ULL << 51;

  static constexpr uint64_t O_ALL = ~0ULL;
  static constexpr uint64_t O_CONSERVATIVE =
    ~(O_DECOMPOSE_INTCASE | O_DECOMPOSE_STRINGCASE);

  void SetVerbose(int verbose);

  Program Simplify(const Program &, uint64_t opts = O_CONSERVATIVE);

private:
  int verbose = 0;
  AstPool *pool;
};

}  // il

#endif
