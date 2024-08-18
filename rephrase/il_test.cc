
#include "il.h"

#include <cstdio>
#include <string>
#include <unordered_set>

#include "ansi.h"
#include "base/logging.h"

#include "il-util.h"

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
  CHECK(TypeCmp::Compare(arrow, arrow2) == TypeCmp::Order::EQ);
  CHECK(TypeCmp::Compare(arrow, pool.StringType()) != TypeCmp::Order::EQ);
  CHECK(TypeCmp::Compare(arrow, pool.Arrow(arrow, arrow))
        != TypeCmp::Order::EQ);

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

static void TestUnroll() {
  AstPool pool;

  const Type *mu = pool.Mu(0, {{"a", pool.VarType("b", {})},
                               {"b", pool.VarType("a", {})}});


  const Type *u = pool.UnrollType(mu);
  std::unordered_set<std::string> ftvs = ILUtil::FreeTypeVars(mu);
  CHECK(ftvs.empty()) << TypeString(mu) << "\nWith free vars:" <<
    ILUtil::VarSetString(ftvs);
  std::unordered_set<std::string> uftvs = ILUtil::FreeTypeVars(u);
  CHECK(uftvs.empty()) << TypeString(u) << "\nWith free vars:" <<
    ILUtil::VarSetString(uftvs);

}

static void TestCompare() {
  AstPool pool;

  using enum TypeCmp::Order;

  CHECK(TypeCmp::Compare(pool.Arrow(pool.IntType(), pool.StringType()),
                       pool.Arrow(pool.IntType(), pool.StringType())) == EQ);

  CHECK(TypeCmp::Compare(pool.Arrow(pool.StringType(), pool.StringType()),
                         pool.Arrow(pool.IntType(), pool.StringType())) != EQ);

  CHECK(TypeCmp::Compare(pool.RecordType({{"a", pool.IntType()}}),
                         pool.RecordType({})) != EQ);
  CHECK(TypeCmp::Compare(pool.RecordType({{"a", pool.IntType()}}),
                         pool.RecordType({{"a", pool.IntType()}})) == EQ);
  CHECK(TypeCmp::Compare(pool.RecordType({{"a", pool.StringType()}}),
                         pool.RecordType({{"a", pool.IntType()}})) != EQ);
  CHECK(TypeCmp::Compare(
            pool.RecordType(
                {{"a", pool.IntType()}, {"b", pool.StringType()}}),
            pool.RecordType(
                {{"a", pool.IntType()}, {"c", pool.StringType()}})) == EQ);
}

}  // il

int main(int argc, char **argv) {
  ANSI::Init();

  il::TestTypeSubst();
  il::TestUnroll();
  il::TestCompare();

  printf("OK\n");
  return 0;
}
