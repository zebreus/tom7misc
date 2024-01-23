
#include "initial.h"

using namespace il;

Initial::Initial(AstPool *pool) {

  auto PairType = [&](const Type *a, const Type *b) {
      return pool->Tuple({a, b});
    };

  auto BinOpType = [&](const Type *a, const Type *b, const Type *ret) ->
    const Type * {
      return pool->Arrow(PairType(a, b), ret);
    };

  auto Mono = [&](const Type *t) -> PolyType {
      return PolyType{.tyvars = {}, .type = t};
    };

  const il::Type *Unit = pool->Record({});
  const il::Type *Alpha = pool->VarType("a");
  const il::Type *Int = pool->VarType("int");
  auto List = [&](const Type *a) { return pool->VarType("list", {a}); };
  auto Ref = [&](const Type *a) { return pool->VarType("ref", {a}); };

  const std::vector<std::pair<std::string, PolyType>> exp_vars = {
    {"+", Mono(BinOpType(Int, Int, Int))},
    {"-", Mono(BinOpType(Int, Int, Int))},
    {":=", PolyType{
        .tyvars = {"a"},
        .type = BinOpType(Ref(Alpha), Alpha, Unit)}},
    {"!", PolyType{
        .tyvars = {"a"},
        .type = pool->Arrow(Ref(Alpha), Alpha)}},
    {"ref", PolyType{
        .tyvars = {"a"},
        .type = pool->Arrow(Alpha, Ref(Alpha))}},

    /*
    {"SOME", PolyType{
        .tyvars = {"a"},
        .type = pool->Arrow(Alpha, Option(Alpha))}},
    */

    // TODO: List stuff can just be introduced by a preamble.
    {"::", PolyType{
        .tyvars = {"a"},
        .type = BinOpType(Alpha, List(Alpha), List(Alpha))}},

  };

  const std::vector<std::pair<std::string, int>> type_vars = {
    {"list", 1},
    {"option", 1},
    {"bool", 0},
    {"int", 0},
    {"string", 0},
    {"ref", 1},
  };

  ctx = Context(exp_vars, type_vars);
}

const Context &Initial::InitialContext() const { return ctx; }
