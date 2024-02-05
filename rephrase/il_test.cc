
#include "il.h"

#include "ansi.h"
#include "base/logging.h"

namespace il {

// TODO: Should probably test more here.
static void TestTypeSubst() {
  AstPool pool;
  const Type *arrow = pool.Arrow(pool.StringType(),
                                 pool.RefType(pool.IntType()));
  // Perform a no-op substitution and see that we get back the
  // exact same object (no copies).

  const Type *arrow2 = pool.SubstType(pool.RecordType({}),
                                      "unused",
                                      arrow);
  CHECK(arrow == arrow2) << "Expected that a no-op substitution "
    "does not copy anything.";
}

}  // il

int main(int argc, char **argv) {
  ANSI::Init();

  il::TestTypeSubst();

  printf("OK\n");
  return 0;
}
