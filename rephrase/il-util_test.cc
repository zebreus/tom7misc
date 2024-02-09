
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
                  {}, "w", pool.Int(1),
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
}

}  // il

int main(int argc, char **argv) {
  ANSI::Init();

  il::TestFreeVars();

  printf("OK\n");
  return 0;
}
