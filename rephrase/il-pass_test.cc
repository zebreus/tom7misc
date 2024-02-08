
#include "il-pass.h"

#include "frontend.h"
#include "ansi.h"
#include "base/logging.h"

static constexpr bool VERBOSE = false;

struct IdentityPass : public il::Pass<> {
  using Pass::Pass;
};

static void TestIdentity() {
  Frontend frontend;
  if (VERBOSE) {
    frontend.SetVerbose(2);
  }
  const il::Exp *e = frontend.RunFrontendOn(
      "il-pass_test",
      "let\n"
      "    datatype dir = Up of {} | Down of {}\n"
      "    val (x : int) = 7\n"
      "    val f = fn x => x\n"
      "in (f x, f Down)\n"
      "end\n");

  il::AstPool pool;

  IdentityPass pass(&pool);
  // On the first pass, we should get the correct guesses, but by
  // default it simplifies bound enums, so this does actually do
  // some allocations.
  const il::Exp *e1 = pass.DoExp(e);
  // But now there should be no evars, and thus no copying.
  const il::Exp *e2 = pass.DoExp(e1);

  CHECK(e1 == e2) << "The guesses should always be correct, so we "
    "should get back the same object:"
    ABLUE("\ne1") ":\n" <<
    ExpString(e1) <<
    APURPLE("\ne2") ":\n" <<
    ExpString(e2);
}

int main(int argc, char **argv) {
  ANSI::Init();

  TestIdentity();

  printf("OK\n");
  return 0;
}
