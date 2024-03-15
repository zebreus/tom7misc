
#include "unification.h"

#include "ansi.h"
#include "base/logging.h"
#include "il.h"

namespace il {

static void Simple() {
  AstPool pool;

  EVar A, B, C;

  CHECK(EVar::SameEVar(A, A));
  CHECK(!EVar::SameEVar(A, B));
  const il::Type *t1 = pool.Arrow(pool.StringType(), pool.EVar(B));
  const il::Type *t2 = pool.Arrow(pool.EVar(A), pool.IntType());

  {
    const auto &[dom1, cod1] = t1->Arrow();
    const auto &[dom2, cod2] = t2->Arrow();
    CHECK(cod1->type == TypeType::EVAR);
    CHECK(dom2->type == TypeType::EVAR);
  }

  Unification::Unify([]() { return "test"; }, t1, t2);

  {
    const auto &[dom1, cod1] = t1->Arrow();
    const auto &[dom2, cod2] = t2->Arrow();
    CHECK(cod1->type == TypeType::EVAR);
    CHECK(dom2->type == TypeType::EVAR);

    const il::Type *dom = dom2->EVar().GetBound();
    const il::Type *cod = cod1->EVar().GetBound();

    CHECK(dom->type == TypeType::STRING);
    CHECK(cod->type == TypeType::INT);
  }

  CHECK(C.GetBound() == nullptr);
  CHECK(A.GetBound()->type == TypeType::STRING);
  CHECK(B.GetBound()->type == TypeType::INT);
}

}  // il

int main(int argc, char **argv) {
  ANSI::Init();

  il::Simple();

  printf("OK\n");
  return 0;
}
