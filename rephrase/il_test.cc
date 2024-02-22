
#include "il.h"

#include "ansi.h"
#include "base/logging.h"

namespace il {

static constexpr bool VERBOSE = false;

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

  {
    // (μ α.α -> β)
    const Type *arrow =
      pool.Arrow(
          pool.Mu(0, {{"x", pool.Arrow(pool.VarType("x", {}),
                                       pool.VarType("y", {}))}}),
          pool.VarType("x", {}));

    const Type *res = pool.SubstType(pool.IntType(), "y", arrow);
    if (VERBOSE) {
      printf("res type: %s\n", TypeString(res).c_str());
    }

    const auto &[dom, cod] = res->Arrow();
    const auto &[muidx, bundle] = dom->Mu();
    CHECK(muidx == 0);
    CHECK(bundle.size() == 1);
    const auto &[x, body] = bundle[0];
    const auto &[ddom, ccod] = body->Arrow();
    // Result of substitution.
    CHECK(ccod->type == TypeType::INT);
    // Maybe alpha-varied, but must still match!
    const std::string xx = std::get<0>(ddom->Var());
    CHECK(xx == x) << xx << " vs " << x;
  }


}

}  // il

int main(int argc, char **argv) {
  ANSI::Init();

  il::TestTypeSubst();

  printf("OK\n");
  return 0;
}
