
#include "il-util.h"

#include <unordered_set>
#include <string>
#include <vector>

#include "il.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "ansi.h"
#include "util.h"

static std::string StringSetString(
    const std::unordered_set<std::string> &ss) {
  std::vector<std::string> sv;
  sv.reserve(ss.size());
  for (const std::string &s : ss) sv.push_back(s);
  std::sort(sv.begin(), sv.end());
  return StringPrintf("{%s}", Util::Join(sv, ", ").c_str());
}

namespace il {
static void TestFreeVars() {
  AstPool pool;

  auto Var = [&pool](const std::string &x) {
      return pool.Var({}, x);
    };

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

static void TestSubstType() {
  AstPool pool;
  const Exp *e = pool.Var(
      {
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
  const Exp *efn = pool.Fn("rec", "x",
                           pool.Record({{"1", pool.Var({}, "x")},
                                        {"2", pool.Var({}, "y")}}));
  const Exp *ewithx = pool.Inject("SOME", pool.Var({}, "x"));
  // This should avoid capturing x.
  // (SOME x)/y (fn x => (x, y))
  const Exp *e2 = ILUtil::SubstExp(&pool, ewithx, "y", efn);


  {
    const auto &[self, newx, body] = e2->Fn();
    CHECK(self == "rec") << "Technically correct for this to alpha-vary "
      "the recursive variable, but it is not expected.";
    CHECK(newx != "x") << "No way to do this correctly without "
      "alpha-varying the bound x.";
    const auto &labe = body->Record();
    CHECK(labe.size() == 2);
    CHECK(labe[0].first == "1");
    CHECK(std::get<1>(labe[0].second->Var()) == newx);
    CHECK(labe[1].second->type == ExpType::INJECT) << "Not substituted?";
    const auto &[lab, e] = labe[1].second->Inject();
    CHECK(lab == "SOME");
    CHECK(std::get<1>(e->Var()) == "x");
  }
}

}  // il

int main(int argc, char **argv) {
  ANSI::Init();

  il::TestFreeVars();
  il::TestSubstType();

  printf("OK\n");
  return 0;
}
