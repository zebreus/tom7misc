
#include "primop.h"

#include <cstdio>
#include <tuple>
#include <utility>
#include <string>
#include <vector>

#include "il.h"
#include "base/logging.h"

const char *PrimopString(Primop po) {
  switch (po) {
  case Primop::REF: return "REF";
  case Primop::REF_GET: return "REF_GET";
  case Primop::REF_SET: return "REF_SET";
  case Primop::INT_EQ: return "INT_EQ";
  case Primop::INT_NEQ: return "INT_NEQ";
  case Primop::INT_LESS: return "INT_LESS";
  case Primop::INT_LESSEQ: return "INT_LESSEQ";
  case Primop::INT_GREATER: return "INT_GREATER";
  case Primop::INT_GREATEREQ: return "INT_GREATEREQ";
  case Primop::INT_TIMES: return "INT_TIMES";
  case Primop::INT_PLUS: return "INT_PLUS";
  case Primop::INT_MINUS: return "INT_MINUS";
  case Primop::INT_DIV: return "INT_DIV";
  case Primop::INT_MOD: return "INT_MOD";
  case Primop::INT_NEG: return "INT_NEG";
  case Primop::STRING_EQ: return "STRING_EQ";
  case Primop::STRING_LESS: return "STRING_LESS";
  case Primop::STRING_GREATER: return "STRING_GREATER";
  case Primop::INT_TO_FLOAT: return "INT_TO_FLOAT";
  case Primop::INT_DIV_TO_FLOAT: return "INT_DIV_TO_FLOAT";
  case Primop::FLOAT_TIMES: return "FLOAT_TIMES";
  case Primop::FLOAT_NEG: return "FLOAT_NEG";
  case Primop::FLOAT_PLUS: return "FLOAT_PLUS";
  case Primop::FLOAT_MINUS: return "FLOAT_MINUS";
  case Primop::FLOAT_DIV: return "FLOAT_DIV";
  case Primop::FLOAT_EQ: return "FLOAT_EQ";
  case Primop::FLOAT_NEQ: return "FLOAT_NEQ";
  case Primop::FLOAT_LESS: return "FLOAT_LESS";
  case Primop::FLOAT_LESSEQ: return "FLOAT_LESSEQ";
  case Primop::FLOAT_GREATER: return "FLOAT_GREATER";
  case Primop::FLOAT_GREATEREQ: return "FLOAT_GREATEREQ";

  case Primop::OUT_STRING: return "OUT_STRING";
  case Primop::OUT_LAYOUT: return "OUT_LAYOUT";
  case Primop::EMIT_BADNESS: return "EMIT_BADNESS";
  case Primop::SET_DOC_INFO: return "SET_DOC_INFO";

  case Primop::STRING_CONCAT: return "STRING_CONCAT";
  case Primop::STRING_EMPTY: return "STRING_EMPTY";
  case Primop::STRING_SIZE: return "STRING_SIZE";
  case Primop::STRING_FIND: return "STRING_FIND";
  case Primop::STRING_SUBSTR: return "STRING_SUBSTR";
  case Primop::STRING_REPLACE: return "STRING_REPLACE";
  case Primop::STRING_FIRST_CODEPOINT: return "STRING_FIRST_CODEPOINT";
  case Primop::NORMALIZE_WHITESPACE: return "NORMALIZE_WHITESPACE";

  case Primop::INT_TO_STRING: return "INT_TO_STRING";
  case Primop::STRING_TO_LAYOUT: return "STRING_TO_LAYOUT";
  case Primop::OBJ_EMPTY: return "OBJ_EMPTY";

  case Primop::FONT_LOAD_FILE: return "FONT_LOAD_FILE";
  case Primop::FONT_REGISTER: return "FONT_REGISTER";

  case Primop::IMAGE_LOAD_FILE: return "IMAGE_LOAD_FILE";
  case Primop::IMAGE_PROPS: return "IMAGE_PROPS";

  case Primop::REPHRASE: return "REPHRASE";
  case Primop::GET_BOXES: return "GET_BOXES";
  case Primop::PACK_BOXES: return "PACK_BOXES";
  case Primop::IS_TEXT: return "IS_TEXT";
  case Primop::GET_TEXT: return "GET_TEXT";
  case Primop::GET_ATTRS: return "GET_ATTRS";
  case Primop::SET_ATTRS: return "SET_ATTRS";
  case Primop::LAYOUT_VEC_SIZE: return "LAYOUT_VEC_SIZE";
  case Primop::LAYOUT_VEC_SUB: return "LAYOUT_VEC_SUB";

  case Primop::DEBUG_PRINT_DOC: return "DEBUG_PRINT_DOC";

  case Primop::INVALID: return "INVALID";
  }
  return "?? UNKNOWN PRIMOP ??";
}

std::tuple<int, int> PrimopArity(Primop po) {
  switch (po) {
  case Primop::REF: return std::make_tuple(1, 1);
  case Primop::REF_GET: return std::make_tuple(1, 1);
  case Primop::REF_SET: return std::make_tuple(1, 2);
  case Primop::INT_EQ: return std::make_tuple(0, 2);
  case Primop::INT_NEQ: return std::make_tuple(0, 2);
  case Primop::INT_LESS: return std::make_tuple(0, 2);
  case Primop::INT_LESSEQ: return std::make_tuple(0, 2);
  case Primop::INT_GREATER: return std::make_tuple(0, 2);
  case Primop::INT_GREATEREQ: return std::make_tuple(0, 2);
  case Primop::INT_TIMES: return std::make_tuple(0, 2);
  case Primop::INT_PLUS: return std::make_tuple(0, 2);
  case Primop::INT_MINUS: return std::make_tuple(0, 2);
  case Primop::INT_DIV: return std::make_tuple(0, 2);
  case Primop::INT_MOD: return std::make_tuple(0, 2);
  case Primop::INT_NEG: return std::make_tuple(0, 1);
  case Primop::STRING_EQ: return std::make_tuple(0, 2);
  case Primop::STRING_LESS: return std::make_tuple(0, 2);
  case Primop::STRING_GREATER: return std::make_tuple(0, 2);
  case Primop::INT_TO_FLOAT: return std::make_tuple(0, 1);
  case Primop::INT_DIV_TO_FLOAT: return std::make_tuple(0, 2);
  case Primop::FLOAT_NEG: return std::make_tuple(0, 1);
  case Primop::FLOAT_TIMES: return std::make_tuple(0, 2);
  case Primop::FLOAT_PLUS: return std::make_tuple(0, 2);
  case Primop::FLOAT_MINUS: return std::make_tuple(0, 2);
  case Primop::FLOAT_DIV: return std::make_tuple(0, 2);
  case Primop::FLOAT_EQ: return std::make_tuple(0, 2);
  case Primop::FLOAT_NEQ: return std::make_tuple(0, 2);
  case Primop::FLOAT_LESS: return std::make_tuple(0, 2);
  case Primop::FLOAT_LESSEQ: return std::make_tuple(0, 2);
  case Primop::FLOAT_GREATER: return std::make_tuple(0, 2);
  case Primop::FLOAT_GREATEREQ: return std::make_tuple(0, 2);

  case Primop::OUT_STRING: return std::make_tuple(0, 1);
  case Primop::OUT_LAYOUT: return std::make_tuple(0, 2);
  case Primop::EMIT_BADNESS: return std::make_tuple(0, 1);
  case Primop::SET_DOC_INFO: return std::make_tuple(0, 1);

  case Primop::INT_TO_STRING: return std::make_tuple(0, 1);
  case Primop::STRING_TO_LAYOUT: return std::make_tuple(0, 1);
  case Primop::STRING_CONCAT: return std::make_tuple(0, 2);
  case Primop::STRING_EMPTY: return std::make_tuple(0, 1);
  case Primop::STRING_SIZE: return std::make_tuple(0, 1);
  case Primop::STRING_FIND: return std::make_tuple(0, 2);
  case Primop::STRING_SUBSTR: return std::make_tuple(0, 3);
  case Primop::STRING_REPLACE: return std::make_tuple(0, 3);
  case Primop::STRING_FIRST_CODEPOINT: return std::make_tuple(0, 1);
  case Primop::NORMALIZE_WHITESPACE: return std::make_tuple(0, 1);

  case Primop::OBJ_EMPTY: return std::make_tuple(0, 1);

  case Primop::FONT_LOAD_FILE: return std::make_tuple(0, 1);
  case Primop::FONT_REGISTER: return std::make_tuple(0, 3);

  case Primop::IMAGE_LOAD_FILE: return std::make_tuple(0, 1);
  case Primop::IMAGE_PROPS: return std::make_tuple(0, 1);

  case Primop::REPHRASE: return std::make_tuple(0, 1);
  case Primop::GET_BOXES: return std::make_tuple(0, 1);
  case Primop::PACK_BOXES: return std::make_tuple(0, 2);
  case Primop::IS_TEXT: return std::make_tuple(0, 1);
  case Primop::GET_TEXT: return std::make_tuple(0, 1);
  case Primop::GET_ATTRS: return std::make_tuple(0, 1);
  case Primop::SET_ATTRS: return std::make_tuple(0, 2);
  case Primop::LAYOUT_VEC_SIZE: return std::make_tuple(0, 1);
  case Primop::LAYOUT_VEC_SUB: return std::make_tuple(0, 2);

  case Primop::DEBUG_PRINT_DOC: return std::make_tuple(0, 1);

  case Primop::INVALID:
    LOG(FATAL) << "INVALID primop";
  }
  LOG(FATAL) << "Unknown primop: " << PrimopString(po);
  return std::make_tuple(0, 0);
};

bool IsPrimopTotal(Primop p) {
  switch (p) {
  case Primop::REF: return false;
  case Primop::REF_GET: return false;
  case Primop::REF_SET: return false;
  // Since we use BigInt, integer arithmetic cannot overflow.
  case Primop::INT_EQ: return true;
  case Primop::INT_NEQ: return true;
  case Primop::INT_LESS: return true;
  case Primop::INT_LESSEQ: return true;
  case Primop::INT_GREATER: return true;
  case Primop::INT_GREATEREQ: return true;
  case Primop::INT_TIMES: return true;
  case Primop::INT_PLUS: return true;
  case Primop::INT_MINUS: return true;
  case Primop::INT_DIV:
    // Because of divide by zero.
    return false;
  case Primop::INT_MOD:
    // Because of divide by zero.
    return false;
  case Primop::INT_NEG:
    return true;
  case Primop::STRING_EQ:
  case Primop::STRING_LESS:
  case Primop::STRING_GREATER:
    return true;
  case Primop::INT_TO_FLOAT:
    return true;
  case Primop::INT_DIV_TO_FLOAT:
    // Here, division by zero produces nan or +/- inf.
    return true;
  case Primop::FLOAT_NEG: return true;
  case Primop::FLOAT_TIMES: return true;
  case Primop::FLOAT_PLUS: return true;
  case Primop::FLOAT_MINUS: return true;
  case Primop::FLOAT_DIV: return true;
  case Primop::FLOAT_EQ: return true;
  case Primop::FLOAT_NEQ: return true;
  case Primop::FLOAT_LESS: return true;
  case Primop::FLOAT_LESSEQ: return true;
  case Primop::FLOAT_GREATER: return true;
  case Primop::FLOAT_GREATEREQ: return true;

  case Primop::INT_TO_STRING: return true;
  case Primop::STRING_TO_LAYOUT: return true;
  case Primop::STRING_CONCAT: return true;
  case Primop::STRING_EMPTY: return true;
  case Primop::STRING_SIZE: return true;
  case Primop::STRING_FIND: return true;
  case Primop::STRING_SUBSTR:
    // It can fail.
    return false;
  case Primop::STRING_REPLACE: return true;
  case Primop::STRING_FIRST_CODEPOINT:
    // Returns empty string if string is empty, so this always succeeds.
    return true;
  case Primop::NORMALIZE_WHITESPACE: return true;

  case Primop::OUT_STRING: return false;
  case Primop::OUT_LAYOUT: return false;
  case Primop::EMIT_BADNESS: return false;
  case Primop::SET_DOC_INFO: return false;

  case Primop::IS_TEXT: return true;

  case Primop::OBJ_EMPTY: return true;

  case Primop::FONT_LOAD_FILE:
    // Possibly we could consider this lazy with a little more
    // care, which could be good so that there can be a library
    // of fonts without cost. But currently it does affect the
    // document if it's loaded.
    return false;

  case Primop::FONT_REGISTER:
    return false;

  case Primop::IMAGE_LOAD_FILE:
    return false;

  case Primop::IMAGE_PROPS:
    // Can fail if image handle is bad
    return false;

  case Primop::REPHRASE:
  case Primop::GET_BOXES:
  case Primop::PACK_BOXES:
  case Primop::GET_TEXT:
  case Primop::GET_ATTRS:
  case Primop::SET_ATTRS:
  case Primop::LAYOUT_VEC_SIZE:
  case Primop::LAYOUT_VEC_SUB:
  case Primop::DEBUG_PRINT_DOC:
    // minimally, internal layout stuff errors out on invalid layout
    return false;

  case Primop::INVALID:
    LOG(FATAL) << "INVALID primop.";
  }

  printf("Uknown primop in IsPrimopTotal");
  return false;
}

bool IsPrimopDiscardable(Primop p) {
  switch (p) {
  case Primop::REF:
    // Creating a reference is not itself observable.
    return true;
  case Primop::REF_GET: return true;
  case Primop::REF_SET: return false;

  default:
    return IsPrimopTotal(p);
  }
}

std::pair<std::vector<std::string>, const il::Type *>
PrimopType(il::AstPool *pool, Primop p) {
  using Type = il::Type;

  const auto Unit = [pool]() { return pool->RecordType({}); };
  // These are singletons so we can just grab the pointers eagerly.
  const il::Type *Int = pool->IntType();
  const il::Type *Float = pool->FloatType();
  const il::Type *Bool = pool->BoolType();
  const il::Type *String = pool->StringType();
  const il::Type *Layout = pool->LayoutType();
  const il::Type *Obj = pool->ObjType();
  const auto Alpha = [pool]() { return pool->VarType("a"); };

  auto PairType = [&](const Type *a, const Type *b) {
      return pool->Product({a, b});
    };

  auto Ref = [pool](const Type *a) { return pool->RefType(a); };

  auto BinOp = [pool, &PairType](
      const Type *a, const Type *b, const Type *ret) {
      return pool->Arrow(PairType(a, b), ret);
    };

  switch (p) {
  case Primop::REF:
    return {{"a"}, pool->Arrow(Alpha(), Ref(Alpha()))};
  case Primop::REF_GET:
    return {{"a"}, pool->Arrow(Ref(Alpha()), Alpha())};
  case Primop::REF_SET:
    return {{"a"}, BinOp(Ref(Alpha()), Alpha(), Unit())};

  // Perhaps these should just be overloaded α * α -> bool,
  // with some hack to resolve them? (But not here. Elaboration
  // should do it.)
  case Primop::INT_EQ: return {{}, BinOp(Int, Int, Bool)};
  case Primop::INT_NEQ: return {{}, BinOp(Int, Int, Bool)};
  case Primop::INT_LESS: return {{}, BinOp(Int, Int, Bool)};
  case Primop::INT_LESSEQ: return {{}, BinOp(Int, Int, Bool)};
  case Primop::INT_GREATER: return {{}, BinOp(Int, Int, Bool)};
  case Primop::INT_GREATEREQ: return {{}, BinOp(Int, Int, Bool)};

  case Primop::INT_TIMES: return {{}, BinOp(Int, Int, Int)};
  case Primop::INT_PLUS: return {{}, BinOp(Int, Int, Int)};
  case Primop::INT_MINUS: return {{}, BinOp(Int, Int, Int)};
  case Primop::INT_DIV: return {{}, BinOp(Int, Int, Int)};
  case Primop::INT_MOD: return {{}, BinOp(Int, Int, Int)};

  case Primop::INT_NEG: return {{}, pool->Arrow(Int, Int)};

  case Primop::INT_TO_FLOAT: return {{}, pool->Arrow(Int, Float)};
  case Primop::INT_DIV_TO_FLOAT: return {{}, BinOp(Int, Int, Float)};

  case Primop::FLOAT_TIMES: return {{}, BinOp(Float, Float, Float)};
  case Primop::FLOAT_PLUS: return {{}, BinOp(Float, Float, Float)};
  case Primop::FLOAT_MINUS: return {{}, BinOp(Float, Float, Float)};
  case Primop::FLOAT_DIV: return {{}, BinOp(Float, Float, Float)};

  case Primop::FLOAT_NEG: return {{}, pool->Arrow(Float, Float)};

  case Primop::FLOAT_EQ: return {{}, BinOp(Float, Float, Bool)};
  case Primop::FLOAT_NEQ: return {{}, BinOp(Float, Float, Bool)};
  case Primop::FLOAT_LESS: return {{}, BinOp(Float, Float, Bool)};
  case Primop::FLOAT_LESSEQ: return {{}, BinOp(Float, Float, Bool)};
  case Primop::FLOAT_GREATER: return {{}, BinOp(Float, Float, Bool)};
  case Primop::FLOAT_GREATEREQ: return {{}, BinOp(Float, Float, Bool)};

  case Primop::STRING_EQ: return {{}, BinOp(String, String, Bool)};
  case Primop::STRING_LESS: return {{}, BinOp(String, String, Bool)};
  case Primop::STRING_GREATER: return {{}, BinOp(String, String, Bool)};
  case Primop::STRING_CONCAT: return {{}, BinOp(String, String, String)};
  case Primop::STRING_EMPTY: return {{}, pool->Arrow(String, Bool)};

  case Primop::STRING_SIZE: return {{}, pool->Arrow(String, Int)};
  case Primop::STRING_FIND: return {{}, BinOp(String, String, Int)};
  case Primop::STRING_SUBSTR:
    return {{}, pool->Arrow(pool->Product({String, Int, Int}), String)};
  case Primop::STRING_REPLACE:
    // string-replace(haystack, needle, replacement)
    return {{}, pool->Arrow(pool->Product({String, String, String}), String)};
  case Primop::STRING_FIRST_CODEPOINT:
    return {{}, pool->Arrow(String, String)};
  case Primop::NORMALIZE_WHITESPACE: return {{}, pool->Arrow(String, String)};
  case Primop::INT_TO_STRING: return {{}, pool->Arrow(Int, String)};
  case Primop::STRING_TO_LAYOUT: return {{}, pool->Arrow(String, Layout)};
  case Primop::OUT_STRING: return {{}, pool->Arrow(String, Unit())};
    // page number, content
  case Primop::OUT_LAYOUT: return {{}, BinOp(Int, Layout, Unit())};
  case Primop::EMIT_BADNESS: return {{}, pool->Arrow(Float, Unit())};
  case Primop::SET_DOC_INFO: return {{}, pool->Arrow(Obj, Unit())};

  case Primop::OBJ_EMPTY: return {{}, pool->Arrow(Obj, Bool)};

  case Primop::FONT_LOAD_FILE: return {{}, pool->Arrow(String, String)};
  case Primop::FONT_REGISTER:
    // register-font(font-name, family-name, property-mask)
    return {{}, pool->Arrow(pool->Product({String, String, Int}), Unit())};

  case Primop::IMAGE_LOAD_FILE: return {{}, pool->Arrow(String, String)};
  case Primop::IMAGE_PROPS: return {{}, pool->Arrow(String, Obj)};

  case Primop::REPHRASE: return {{}, pool->Arrow(Layout, Layout)};
  case Primop::GET_BOXES: return {{}, pool->Arrow(Layout, Layout)};
  case Primop::PACK_BOXES: return {{}, BinOp(Float, Layout, Layout)};
  case Primop::IS_TEXT: return {{}, pool->Arrow(Layout, Bool)};
  case Primop::GET_TEXT: return {{}, pool->Arrow(Layout, String)};
  case Primop::GET_ATTRS: return {{}, pool->Arrow(Layout, Obj)};
  case Primop::SET_ATTRS: return {{}, BinOp(Obj, Layout, Layout)};
  case Primop::LAYOUT_VEC_SIZE: return {{}, pool->Arrow(Layout, Int)};
  case Primop::LAYOUT_VEC_SUB: return {{}, BinOp(Layout, Int, Layout)};

  case Primop::DEBUG_PRINT_DOC: return {{}, pool->Arrow(Layout, Unit())};

  default:
    LOG(FATAL) << "Unknown primop in PrimopType";
    return {{}, nullptr};
  }
}
