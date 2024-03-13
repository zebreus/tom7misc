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
  REF_GET,
  REF_SET,

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

  // IEEE operation: not an equivalence
  // relation!
  FLOAT_EQ,
  FLOAT_NEQ,
  FLOAT_LESS,
  FLOAT_LESSEQ,
  FLOAT_GREATER,
  FLOAT_GREATEREQ,

  STRING_EQ,
  STRING_LESS,
  STRING_GREATER,
  STRING_CONCAT,
  // TODO: Other string comparisons

  INT_TO_STRING,

  // I/O
  OUT_STRING,
  OUT_LAYOUT,

  STRING_TO_LAYOUT,

  OBJ_EMPTY,

  IS_TEXT,
  GET_TEXT,
  // Operations on non-text layout
  GET_ATTRS,
  LAYOUT_VEC_SIZE,
  LAYOUT_VEC_SUB,

  // expensive stuff implemented by harness
  REPHRASE,
  GET_BOXES,
  PACK_BOXES,

  DEBUG_PRINT_DOC,

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
