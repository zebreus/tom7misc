
#include "initial.h"

#include <vector>
#include <string>
#include <utility>

#include "primop.h"
#include "il.h"

namespace il {

Initial::Initial(AstPool *pool) {

  const il::Type *Alpha = pool->VarType("a");
  const il::Type *Int = pool->IntType();
  const il::Type *Float = pool->FloatType();
  const il::Type *Bool = pool->BoolType();
  const il::Type *Obj = pool->ObjType();
  auto Ref = [&](const Type *a) { return pool->RefType(a); };

  auto LookupPrimop = [&pool](Primop p) {
      const auto &[tv, t] = PrimopType(pool, p);
      return VarInfo{
        .tyvars = tv,
        .type = t,
        .primop = {p},
      };
    };

  const std::vector<std::pair<std::string, Primop>> primops = {
    {"+", Primop::INT_PLUS},
    {"-", Primop::INT_MINUS},
    {"*", Primop::INT_TIMES},
    {"/", Primop::INT_DIV_TO_FLOAT},
    {"div", Primop::INT_DIV},
    {"mod", Primop::INT_MOD},

    // Perhaps these should just be overloaded α * α -> bool,
    // with some hack to resolve them?
    {"==", Primop::INT_EQ},
    {"!=", Primop::INT_NEQ},
    {"<", Primop::INT_LESS},
    {"<=", Primop::INT_LESSEQ},
    {">", Primop::INT_GREATER},
    {">=", Primop::INT_GREATEREQ},

    {"ref", Primop::REF},
    {":=", Primop::REF_SET},
    {"!", Primop::REF_GET},

    {"int-to-string", Primop::INT_TO_STRING},
    // {"int-eq", Primop::INT_EQ},
    {"string-concat", Primop::STRING_CONCAT},
    {"string-eq", Primop::STRING_EQ},
    {"string-less", Primop::STRING_LESS},
    {"string-greater", Primop::STRING_GREATER},

    {"print", Primop::OUT_STRING},
  };

  std::vector<std::pair<std::string, VarInfo>> exp_vars;
  exp_vars.reserve(primops.size());
  for (const auto &[x, p] : primops) {
    exp_vars.emplace_back(x, LookupPrimop(p));
  }

  const il::Type *String = pool->StringType();
  auto Kind0 = [&](const Type *t) {
      return TypeVarInfo{.tyvars = {}, .type = t};
    };

  const std::vector<std::pair<std::string, TypeVarInfo>> type_vars = {
    {"obj", Kind0(Obj)},
    {"bool", Kind0(Bool)},
    {"int", Kind0(Int)},
    {"float", Kind0(Float)},
    {"string", Kind0(String)},
    {"ref", TypeVarInfo{.tyvars = {"a"}, .type = Ref(Alpha)}},
  };

  // No initial object names.
  std::vector<std::pair<std::string, ObjVarInfo>> obj_vars;

  ctx = ElabContext(exp_vars, type_vars, obj_vars);
}

const ElabContext &Initial::InitialContext() const { return ctx; }

}  // il
