
#include "primop.h"

#include <tuple>
#include <utility>
#include <string>
#include <vector>

#include "il.h"
#include "base/logging.h"

const char *PrimopString(Primop p) {
  switch (p) {
  case Primop::REF: return "REF";
  case Primop::GET: return "GET";
  case Primop::SET: return "SET";
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
  case Primop::INT_DIV_TO_FLOAT: return "INT_DIV_TO_FLOAT";
  case Primop::FLOAT_TIMES: return "FLOAT_TIMES";
  case Primop::FLOAT_PLUS: return "FLOAT_PLUS";
  case Primop::FLOAT_MINUS: return "FLOAT_MINUS";
  case Primop::FLOAT_DIV: return "FLOAT_DIV";
  default: return "?? UNKNOWN PRIMOP ??";
  }
}

std::tuple<int, int> PrimopArity(Primop po) {
  switch(po) {
  case Primop::REF: return std::make_tuple(1, 1);
  case Primop::GET: return std::make_tuple(1, 1);
  case Primop::SET: return std::make_tuple(1, 2);
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
  case Primop::INT_DIV_TO_FLOAT: return std::make_tuple(0, 2);
  case Primop::FLOAT_TIMES: return std::make_tuple(0, 2);
  case Primop::FLOAT_PLUS: return std::make_tuple(0, 2);
  case Primop::FLOAT_MINUS: return std::make_tuple(0, 2);
  case Primop::FLOAT_DIV: return std::make_tuple(0, 2);
  default:
    LOG(FATAL) << "Unknown primop: " << PrimopString(po);
    return std::make_tuple(0, 0);
  }
};

bool IsPrimopTotal(Primop p) {
  switch (p) {
  case Primop::REF: return false;
  case Primop::GET: return false;
  case Primop::SET: return false;
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
    return true;
  case Primop::INT_DIV_TO_FLOAT:
    // Here, division by zero produces nan or +/- inf.
    return true;
  case Primop::FLOAT_TIMES: return true;
  case Primop::FLOAT_PLUS: return true;
  case Primop::FLOAT_MINUS: return true;
  case Primop::FLOAT_DIV: return true;
  default:
    printf("Uknown primop in IsPrimopTotal");
    return false;
  }
}

bool IsPrimopDiscardable(Primop p) {
  switch (p) {
  case Primop::REF:
    // Creating a reference is not itself observable.
    return true;
  case Primop::GET: return true;
  case Primop::SET: return false;

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
  case Primop::GET:
    return {{"a"}, pool->Arrow(Ref(Alpha()), Alpha())};
  case Primop::SET:
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

  case Primop::INT_DIV_TO_FLOAT: return {{}, BinOp(Int, Int, Float)};

  case Primop::FLOAT_TIMES: return {{}, BinOp(Float, Float, Float)};
  case Primop::FLOAT_PLUS: return {{}, BinOp(Float, Float, Float)};
  case Primop::FLOAT_MINUS: return {{}, BinOp(Float, Float, Float)};
  case Primop::FLOAT_DIV: return {{}, BinOp(Float, Float, Float)};

  case Primop::FLOAT_NEG: return {{}, pool->Arrow(Float, Float)};

  case Primop::STRING_EQ: return {{}, BinOp(String, String, Bool)};

  default:
    LOG(FATAL) << "Unknown primop in PrimopType";
    return {{}, nullptr};
  }
}
