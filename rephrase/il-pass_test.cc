
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
  const il::Program pgm = frontend.RunFrontendOn(
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
  const il::Program p1 = pass.DoProgram(pgm);
  // But now there should be no evars, and thus no copying.
  const il::Program p2 = pass.DoProgram(p1);

  CHECK(p1.globals.size() == p2.globals.size());
  bool equal = p1.body == p2.body;
  for (int i = 0; i < (int)p1.globals.size(); i++) {
    equal = equal && p1.globals[i] == p2.globals[i];
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

  printf("OK\n");
  return 0;
}
