
#include "simplification.h"

#include "il.h"

#include "il-pass.h"

namespace il {

Simplification::Simplification(AstPool *pool) : pool(pool) {}

void Simplification::SetVerbose(int v) {
  verbose = v;
}

struct PeepholePass : public il::Pass<> {
  using Pass::Pass;

  // If we have App(fn x => body, arg), with the function not recursive,
  // then this is equivalent to
  // let x = arg in body
  // The let sometimes allows for futher simplification.
  const Exp *DoApp(const Exp *f, const Exp *arg,
                   const Exp *guess) override {
    arg = DoExp(arg);

    if (f->type == ExpType::FN) {
      const auto &[self, x, body] = f->Fn();
      if (self.empty()) {
        return pool->LetFlat(pool->ValDec({}, x, arg), DoExp(body));
      }
    }
    return pool->App(DoExp(f), arg, guess);
  }

};


const il::Exp *Simplification::Simplify(const Exp *exp) {
  PeepholePass peephole(pool);

  exp = peephole.DoExp(exp);

  return exp;
}

}  // namespace il
