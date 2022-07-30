
#include <cstdint>

#include "expression.h"
#include "util.h"
#include "half.h"

// Manually convert an old-style expression (via ExpString) to
// the serialized format.

static const Exp *Gen(Exp::Allocator *alloc) {
  auto T = [alloc](const Exp *e, uint16_t c, uint16_t iters) {
      return alloc->TimesC(e, c, iters);
    };
  auto P = [alloc](const Exp *e, uint16_t c, uint16_t iters) {
      return alloc->PlusC(e, c, iters);
    };
  auto E = [alloc](const Exp *a, const Exp *b) {
      return alloc->PlusE(a, b);
    };
  const Exp *V = alloc->Var();

  return
  /* paste expression here */
    V
    ;
}

int main(int argc, char **argv) {
  Exp::Allocator alloc;
  const Exp *e = Gen(&alloc);

  std::string s = Exp::Serialize(e);
  std::string err;
  const Exp *ee = Exp::Deserialize(&alloc, s, &err);
  CHECK(ee) << err;
  CHECK(Exp::Eq(e, ee));
  Util::WriteFile("converted.txt", s);

  return 0;
}
