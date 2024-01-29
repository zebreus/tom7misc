
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

  CHECK(t1->type == TypeType::ARROW);
  CHECK(t2->type == TypeType::ARROW);
  CHECK(t1->b->type == TypeType::EVAR);
  CHECK(t2->a->type == TypeType::EVAR);

  Unification::Unify("test", t1, t2);

  CHECK(t1->type == TypeType::ARROW);
  CHECK(t2->type == TypeType::ARROW);
  CHECK(t1->b->type == TypeType::EVAR);
  CHECK(t2->a->type == TypeType::EVAR);

  const il::Type *dom = t2->a->evar.GetBound();
  const il::Type *cod = t1->b->evar.GetBound();
  CHECK(dom->type == TypeType::STRING);
  CHECK(cod->type == TypeType::INT);

  CHECK(C.GetBound() == nullptr);
}

}  // il

int main(int argc, char **argv) {
  ANSI::Init();

  il::Simple();

  printf("OK\n");
  return 0;
}
