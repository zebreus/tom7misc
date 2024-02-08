
#include "il-pass.h"

#include "frontend.h"

struct IdentityPass : public il::Pass<> {
  using Pass::Pass;
};

static void TestIdentity() {
  Frontend frontend;
  frontend.SetVerbose(2);
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
  const il::Exp *ee = pass.DoExp(e);

  CHECK(e == ee) << "The guesses should always be correct, so we "
    "should get back the same object.";
}

int main(int argc, char **argv) {

  TestIdentity();

  printf("OK\n");
  return 0;
}
