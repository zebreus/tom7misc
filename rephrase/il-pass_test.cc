
#include "il-pass.h"

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "frontend.h"
#include "il.h"

static constexpr bool VERBOSE = false;

struct IdentityPass : public il::Pass<> {
  using Pass::Pass;
};

static void TestIdentity() {
  Frontend frontend;
  if (VERBOSE) {
    frontend.SetVerbose(2);
  }
  const il::Program pgm = frontend.RunFrontendOn(
      "il-pass_test",
      "let\n"
      "    datatype dir = Up of {} | Down of {}\n"
      "    val (x : int) = 7\n"
      "    val f = fn x => x\n"
      "    object Article of { title : string, year : int }\n"
      "in (f x, f Down, f ({(Article) year = 1997} without (Article) year))\n"
      "end\n");

  il::AstPool pool;

  IdentityPass pass(&pool);
  // On the first pass, we should get the correct guesses, but by
  // default it simplifies bound evars, so this does actually do
  // some allocations.
  const il::Program p1 = pass.DoProgram(pgm);
  // But now there should be no evars, and thus no copying.
  const il::Program p2 = pass.DoProgram(p1);

  CHECK(p1.globals.size() == p2.globals.size());
  bool equal = p1.body == p2.body;
  for (int i = 0; i < (int)p1.globals.size(); i++) {
    const auto &[tv1, sym1, ty1, exp1] = p1.globals[i];
    const auto &[tv2, sym2, ty2, exp2] = p2.globals[i];
    equal = equal && tv1 == tv2 && sym1 == sym2 &&
                 ty1 == ty2 && exp1 == exp2;
  }

  CHECK(equal) << "The guesses should always be correct, so we "
    "should get back the same object:"
    ABLUE("\ne1") ":\n" <<
    ProgramString(p1) <<
    APURPLE("\ne2") ":\n" <<
    ProgramString(p2);
}

int main(int argc, char **argv) {
  ANSI::Init();

  TestIdentity();

  Print("OK\n");
  return 0;
}
