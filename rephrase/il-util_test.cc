
#include "il-util.h"

#include <algorithm>
#include <cstdio>
#include <format>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "il.h"
#include "util.h"

static std::string StringSetString(
    const std::unordered_set<std::string> &ss) {
  std::vector<std::string> sv;
  sv.reserve(ss.size());
  for (const std::string &s : ss) sv.push_back(s);
  std::sort(sv.begin(), sv.end());
  return std::format("{{" "{}" "}}", Util::Join(sv, ", "));
}

namespace il {
static void TestFreeVars() {
  AstPool pool;

  auto Var = [&pool](const std::string &x) {
      return pool.Var({}, x);
    };

  // This function is actually ill-typed, because it tries
  // to return itself. But the free vars functions should
  // still work on ill-typed ASTs!
  const Type *wrong_fntype =
    pool.Arrow(pool.BoolType(), pool.IntType());

  const Exp *e =
    pool.Let(
        // x is free in the rhs here
        {}, "x", Var("x"),
        pool.Let(
          {}, "y", pool.Int(0),
          // y is not free here
          pool.Let(
              {}, "z", Var("y"),
              pool.Let(
                  {}, "w", pool.GlobalSym({}, "GLOBAL"),
                  pool.Fn("self", "u",
                          wrong_fntype,
                          pool.Record(
                              {
                                // Bound by fn
                                {"1", Var("self")},
                                // Bound by fn
                                {"2", Var("u")},
                                // Bound by val
                                {"3", Var("z")},
                                // Free
                                {"4", Var("m")}
                              }))))));

  std::unordered_set<std::string> fvs = ILUtil::FreeExpVars(e);
  CHECK(fvs.size() == 2) << StringSetString(fvs);
  CHECK(fvs.contains("x"));
  CHECK(fvs.contains("m"));

  CHECK(ILUtil::IsExpVarFree(e, "m"));
  CHECK(ILUtil::IsExpVarFree(e, "x"));
  CHECK(!ILUtil::IsExpVarFree(e, "z"));
  CHECK(!ILUtil::IsExpVarFree(e, "self"));
  CHECK(!ILUtil::IsExpVarFree(e, "huh"));
  CHECK(!ILUtil::IsExpVarFree(e, "GLOBAL"));

  std::unordered_map<std::string, int> labs = ILUtil::LabelCounts(e);
  CHECK(labs.size() == 1);
  CHECK(labs["GLOBAL"] == 1);
  CHECK(!labs.contains("x"));
}

static void TestFreeTypeVars() {
  AstPool pool;

  CHECK(ILUtil::FreeTypeVarCounts(pool.VarType("w", {}))["w"] == 1);

  {
    // (μ α.α -> β) -> α
    const Type *t =
      pool.Arrow(
          pool.Mu(0, {{"x", pool.Arrow(pool.VarType("x", {}),
                                       pool.VarType("y", {}))}}),
          pool.VarType("x", {}));

    std::unordered_map<std::string, int> ftvs =
      ILUtil::FreeTypeVarCounts(t);
    CHECK(ftvs["x"] == 1) << "This occurs once, in the codomain: " <<
      ftvs["x"];
    CHECK(ftvs["y"] == 1) << "In the body of the mu.";
  }

  {
    // (μ ρ.α -> β) -> α
    const Type *t =
      pool.Arrow(
          pool.Mu(0, {{"r", pool.Arrow(pool.VarType("x", {}),
                                       pool.VarType("y", {}))}}),
          pool.VarType("x", {}));

    std::unordered_map<std::string, int> ftvs =
      ILUtil::FreeTypeVarCounts(t);
    CHECK(ftvs["x"] == 2) << ftvs["x"];
    CHECK(ftvs["y"] == 1) << "In the body of the mu.";
  }
}


static void TestSubstType() {
  AstPool pool;
  const Exp *e = pool.Var(
      Span{
        pool.IntType(),
        pool.VarType("x", {}),
      },
      "z");

  const Exp *e2 = ILUtil::SubstTypeInExp(&pool,
                                         pool.StringType(),
                                         "x",
                                         e);
  CHECK(e2->type == ExpType::VAR);
  const auto [ts, z] = e2->Var();
  CHECK(z == "z");
  CHECK(ts.size() == 2);
  CHECK(ts[0]->type == TypeType::INT);
  // Result of substitution
  CHECK(ts[1]->type == TypeType::STRING);
}

static void TestSubstExp() {
  AstPool pool;
  const Type *sumtype =
    pool.SumType({{"SOME", pool.IntType()},
                  {"NONE", pool.RecordType({})}});

  const Type *fntype = pool.Arrow(
      pool.BoolType(),
      pool.RecordType({{"1", pool.BoolType()},
                       {"2", sumtype}}));

  const Exp *efn = pool.Fn("rec", "x",
                           fntype,
                           pool.Record({{"1", pool.Var({}, "x")},
                                        {"2", pool.Var({}, "y")}}));
  const Exp *ewithx = pool.Inject(
      "SOME",
      pool.SumType({{"SOME", pool.IntType()},
                    {"NONE", pool.RecordType({})}}),
      pool.Var({}, "x"));
  // This should avoid capturing x.
  // (SOME x)/y (fn x => (x, y))
  const Exp *e2 = ILUtil::SubstExp(&pool, ewithx, "y", efn);


  {
    const auto &[self, newx, t, body] = e2->Fn();
    CHECK(self == "rec") << "Technically correct for this to alpha-vary "
      "the recursive variable, but it is not expected.";
    CHECK(newx != "x") << "No way to do this correctly without "
      "alpha-varying the bound x.";
    const auto &labe = body->Record();
    CHECK(labe.size() == 2);
    CHECK(labe[0].first == "1");
    CHECK(std::get<1>(labe[0].second->Var()) == newx);
    CHECK(labe[1].second->type == ExpType::INJECT) << "Not substituted?";
    const auto &[lab, sum_type, e] = labe[1].second->Inject();
    CHECK(sum_type->type == TypeType::SUM);
    CHECK(lab == "SOME");
    CHECK(std::get<1>(e->Var()) == "x");

    CHECK(t == fntype) << "The types should be equal, but we also "
      "expect that we can do this without even allocating.";
  }
}

}  // il

int main(int argc, char **argv) {
  ANSI::Init();

  il::TestFreeVars();
  il::TestSubstType();
  il::TestSubstExp();
  il::TestFreeTypeVars();

  Print("OK\n");
  return 0;
}
