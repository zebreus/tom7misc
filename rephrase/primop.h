#ifndef _REPHRASE_PRIMOP_H
#define _REPHRASE_PRIMOP_H

#include <tuple>
#include <utility>
#include <string>
#include <vector>

namespace il {
struct Type;
struct AstPool;
};

enum class Primop {
  REF,
  GET,
  SET,

  // TODO: Math, comparisons, etc.
  INT_EQ,
  INT_NEQ,
  INT_LESS,
  INT_LESSEQ,
  INT_GREATER,
  INT_GREATEREQ,

  INT_TIMES,
  INT_PLUS,
  INT_MINUS,
  INT_DIV,
  INT_MOD,

  INT_NEG,

  INT_DIV_TO_FLOAT,
  FLOAT_TIMES,
  FLOAT_PLUS,
  FLOAT_MINUS,
  FLOAT_DIV,

  FLOAT_NEG,

  STRING_EQ,
  // TODO: Other string comparisons

  INT_TO_STRING,

  // I/O
  OUT_STRING,

  INVALID,
};

// Total is the most stringent category: It can be freely reordered
// with any other code, discarded, duplicated; it never fails and always
// has the same result.
bool IsPrimopTotal(Primop p);
// A weaker condition: Application of the primop can be discarded
// if its result is unused. For example, GET.
bool IsPrimopDiscardable(Primop p);

// Number of type args, number of value args.
std::tuple<int, int> PrimopArity(Primop p);

const char *PrimopString(Primop p);

// Get the (poly)type of a primop, allocated in the given pool.
std::pair<std::vector<std::string>, const il::Type *>
PrimopType(il::AstPool *pool, Primop p);

#endif
