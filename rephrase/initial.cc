#include "initial.h"

#include <tuple>
#include <vector>
#include <string>
#include <utility>

#include "context.h"
#include "primop.h"
#include "il.h"

namespace il {

Initial::Initial(AstPool *pool) {

  const il::Type *Alpha = pool->VarType("a");
  const il::Type *Int = pool->IntType();
  const il::Type *Float = pool->FloatType();
  const il::Type *Bool = pool->BoolType();
  const il::Type *Obj = pool->ObjType();
  const il::Type *Layout = pool->LayoutType();
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

    {"andb", Primop::INT_ANDB},
    {"xorb", Primop::INT_XORB},
    {"orb", Primop::INT_ORB},

    {"+.", Primop::FLOAT_PLUS},
    {"-.", Primop::FLOAT_MINUS},
    {"*.", Primop::FLOAT_TIMES},
    {"/.", Primop::FLOAT_DIV},

    {"==.", Primop::FLOAT_EQ},
    {"!=.", Primop::FLOAT_NEQ},
    {"<.", Primop::FLOAT_LESS},
    {"<=.", Primop::FLOAT_LESSEQ},
    {">.", Primop::FLOAT_GREATER},
    {">=.", Primop::FLOAT_GREATEREQ},
    {"int-to-float", Primop::INT_TO_FLOAT},

    // Perhaps these should just be overloaded α * α -> bool,
    // with some hack to resolve them?
    {"==", Primop::INT_EQ},
    {"!=", Primop::INT_NEQ},
    {"<", Primop::INT_LESS},
    {"<=", Primop::INT_LESSEQ},
    {">", Primop::INT_GREATER},
    {">=", Primop::INT_GREATEREQ},

    {"^", Primop::STRING_CONCAT},

    {"ref", Primop::REF},
    {":=", Primop::REF_SET},
    {"!", Primop::REF_GET},

    {"int-to-string", Primop::INT_TO_STRING},
    {"round", Primop::FLOAT_ROUND},

    {"cos", Primop::COS},
    {"sin", Primop::SIN},
    // {"int-eq", Primop::INT_EQ},
    {"string-concat", Primop::STRING_CONCAT},
    {"string-eq", Primop::STRING_EQ},
    {"string-less", Primop::STRING_LESS},
    {"string-greater", Primop::STRING_GREATER},
    {"string-empty", Primop::STRING_EMPTY},
    {"string-size", Primop::STRING_SIZE},
    // Internal so that we can wrap with option.
    {"internal-string-find", Primop::STRING_FIND},
    {"substr", Primop::STRING_SUBSTR},
    {"string-replace", Primop::STRING_REPLACE},
    {"string-first-codepoint", Primop::STRING_FIRST_CODEPOINT},
    {"normalize-whitespace", Primop::NORMALIZE_WHITESPACE},
    {"string-lowercase", Primop::STRING_LOWERCASE},
    {"string-uppercase", Primop::STRING_UPPERCASE},

    {"layout", Primop::STRING_TO_LAYOUT},

    {"obj-empty", Primop::OBJ_EMPTY},
    {"obj-merge", Primop::OBJ_MERGE},

    {"emit-badness", Primop::EMIT_BADNESS},
    {"set-doc-info", Primop::SET_DOC_INFO},
    {"internal-set-page-info", Primop::SET_PAGE_INFO},

    {"rephrase-once", Primop::REPHRASE_ONCE},
    {"internal-rephrasings", Primop::REPHRASINGS},
    {"internal-get-boxes", Primop::GET_BOXES},
    {"internal-pack-boxes", Primop::PACK_BOXES},
    {"debug-print-doc", Primop::DEBUG_PRINT_DOC},

    {"is-text", Primop::IS_TEXT},
    {"get-text", Primop::GET_TEXT},
    {"get-attrs", Primop::GET_ATTRS},
    {"set-attrs", Primop::SET_ATTRS},
    {"layout-vec-size", Primop::LAYOUT_VEC_SIZE},
    {"layout-vec-sub", Primop::LAYOUT_VEC_SUB},

    {"font-load-file", Primop::FONT_LOAD_FILE},
    {"font-register", Primop::FONT_REGISTER},

    {"image-load-file", Primop::IMAGE_LOAD_FILE},
    {"image-props", Primop::IMAGE_PROPS},

    {"achievement", Primop::ACHIEVEMENT},
    {"internal-opt", Primop::OPT},

    {"print", Primop::OUT_STRING},
    {"internal-output", Primop::OUT_LAYOUT},
  };

  std::vector<std::pair<std::string, VarInfo>> exp_vars;
  exp_vars.reserve(primops.size());
  for (const auto &[x, p] : primops) {
    exp_vars.emplace_back(x, LookupPrimop(p));
  }

  const Type *node_type2 = pool->Arrow(pool->LayoutType(), pool->LayoutType());
  const Type *node_type1 = pool->Arrow(pool->ObjType(), node_type2);

  const Type *layout_cat_dom =
    pool->Product({pool->LayoutType(), pool->LayoutType()});
  const Type *layout_cat_cod = pool->LayoutType();
  const Type *layout_cat_arrow = pool->Arrow(layout_cat_dom, layout_cat_cod);
  const std::vector<std::tuple<std::string, const Exp *, const Type *>>
    inlined = {
    {"node", pool->Fn("", "a", node_type1,
                      pool->Fn("", "l", node_type2,
                               pool->Node(pool->Var({}, "a"),
                                          {pool->Var({}, "l")}))), node_type1},
    {"^^", pool->Fn("", "r", layout_cat_arrow,
                    pool->Node(pool->Object({}),
                               {pool->Project("1", pool->Var({}, "r")),
                                pool->Project("2", pool->Var({}, "r"))})),
     layout_cat_arrow},
  };

  for (const auto &[v, e, t] : inlined) {
    exp_vars.emplace_back(
        v,
        VarInfo{
          .tyvars = {},
          .type = t,
          .inlined = e,
        });
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
    {"layout", Kind0(Layout)},
    {"unit", Kind0(pool->RecordType({}))},
    {"ref", TypeVarInfo{.tyvars = {"a"}, .type = Ref(Alpha)}},
  };

  // No initial object names.
  std::vector<std::pair<std::string, ObjVarInfo>> obj_vars;

  ctx = ElabContext(exp_vars, type_vars, obj_vars);
}

const ElabContext &Initial::InitialContext() const { return ctx; }

}  // il
