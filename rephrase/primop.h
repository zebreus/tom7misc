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

// TODO: Primitive types

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

  INT_TO_FLOAT,
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
  FLOAT_ROUND,

  STRING_EQ,
  STRING_LESS,
  STRING_GREATER,
  STRING_CONCAT,
  STRING_EMPTY,
  // TODO: Other string comparisons

  STRING_SIZE,
  STRING_FIND,
  STRING_SUBSTR,
  STRING_REPLACE,
  STRING_FIRST_CODEPOINT,
  NORMALIZE_WHITESPACE,
  STRING_LOWERCASE,
  STRING_UPPERCASE,

  INT_TO_STRING,

  // I/O
  OUT_STRING,
  OUT_LAYOUT,

  EMIT_BADNESS,
  SET_DOC_INFO,

  STRING_TO_LAYOUT,

  OBJ_EMPTY,
  OBJ_MERGE,

  IS_TEXT,
  GET_TEXT,
  // Operations on non-text layout
  GET_ATTRS,
  SET_ATTRS,
  LAYOUT_VEC_SIZE,
  LAYOUT_VEC_SUB,

  FONT_LOAD_FILE,
  FONT_REGISTER,

  IMAGE_LOAD_FILE,
  IMAGE_PROPS,

  // expensive stuff implemented by harness
  REPHRASE_ONCE,
  REPHRASINGS,
  GET_BOXES,
  PACK_BOXES,

  ACHIEVEMENT,

  OPT,

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
