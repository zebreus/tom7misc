
#include "initial.h"

using namespace il;

Context Initial::InitialContext(AstPool *pool) {

  auto PairType = [&](const Type *a, const Type *b) {
      return pool->Tuple({a, b});
    };

  auto BinopType = [&](const Type *a, const Type *b, const Type *ret) {
      return pool->Arrow(PairType(a, b), ret);
    };

  const il::Type *Int = pool->VarType("int");

  const std::vector<std::pair<std::string, const il::Type *>> exp_vars = {
    {"+", BinopType(Int, Int, Int)},
    {"-", BinopType(Int, Int, Int)},
  };

  const std::vector<std::pair<std::string, int>> type_vars = {
    {"list", 1},
    {"option", 1},
    {"bool", 0},
    {"int", 0},
    {"string", 0},
    {"ref", 1},
  };

  return Context(exp_vars, type_vars);
}
