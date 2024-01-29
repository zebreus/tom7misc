
#include "initial.h"

#include <vector>
#include <string>
#include <utility>

namespace il {

Initial::Initial(AstPool *pool) {

  auto PairType = [&](const Type *a, const Type *b) {
      return pool->Product({a, b});
    };

  auto BinOpType = [&](const Type *a, const Type *b, const Type *ret) ->
    const Type * {
      return pool->Arrow(PairType(a, b), ret);
    };

  auto BinOp = [&](const Type *a, const Type *b, const Type *ret,
                   Primop po) -> VarInfo {
      return VarInfo{
        .tyvars = {},
        .type = BinOpType(a, b, ret),
        .primop = {po}};
    };

  const il::Type *Unit = pool->RecordType({});
  const il::Type *Alpha = pool->VarType("a");
  const il::Type *Int = pool->IntType();
  auto Ref = [&](const Type *a) { return pool->RefType(a); };
  // This is probably wrong: We need to expand the type of list,
  // or better
  // auto List = [&](const Type *a) { return pool->VarType("list", {a}); };

  const std::vector<std::pair<std::string, VarInfo>> exp_vars = {
    {"+", BinOp(Int, Int, Int, Primop::INT_PLUS)},
    {"-", BinOp(Int, Int, Int, Primop::INT_MINUS)},
    {":=", VarInfo{
        .tyvars = {"a"},
        .type = BinOpType(Ref(Alpha), Alpha, Unit),
        .primop = {Primop::SET},
      }},
    {"!", VarInfo{
        .tyvars = {"a"},
        .type = pool->Arrow(Ref(Alpha), Alpha),
        .primop = {Primop::GET},
      }},
    {"ref", VarInfo{
        .tyvars = {"a"},
        .type = pool->Arrow(Alpha, Ref(Alpha)),
        .primop = {Primop::REF}
      }},

#if 0
    /*
    {"SOME", PolyType{
        .tyvars = {"a"},
        .type = pool->Arrow(Alpha, Option(Alpha))}},
    */

    // TODO: List stuff can just be introduced by a preamble.
    {"::", PolyType{
        .tyvars = {"a"},
        .type = BinOpType(Alpha, List(Alpha), List(Alpha))}},
#endif

  };

  const il::Type *String = pool->StringType();
  auto Kind0 = [&](const Type *t) {
      return TypeVarInfo{.tyvars = {}, .type = t};
    };

  const std::vector<std::pair<std::string, TypeVarInfo>> type_vars = {
    // These need datatype declarations.
    //    {"list", 1},
    //    {"option", 1},
    //    {"bool", 0},
    {"int", Kind0(Int)},
    {"string", Kind0(String)},
    {"ref", TypeVarInfo{.tyvars = {"a"}, .type = Ref(Alpha)}},
  };

  ctx = Context(exp_vars, type_vars);
}

const Context &Initial::InitialContext() const { return ctx; }

}  // il
