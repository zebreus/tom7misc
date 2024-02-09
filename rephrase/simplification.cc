
#include "simplification.h"

#include "il.h"

#include "il-pass.h"
#include "il-util.h"

namespace il {

Simplification::Simplification(AstPool *pool) : pool(pool) {}

void Simplification::SetVerbose(int v) {
  verbose = v;
}

struct PeepholePass : public il::Pass<> {
  using Pass::Pass;

  // For fn expressions, if the function's self variable is not used,
  // it is not actually recursive.
  const Exp *DoFn(const std::string &self,
                  const std::string &x,
                  const Exp *body,
                  const Exp *guess) override {
    if (!self.empty() && !ILUtil::IsExpVarFree(body, self)) {
      return pool->Fn("", x, DoExp(body), guess);
    }

    return pool->Fn(self, x, DoExp(body), guess);
  }

  #if 0
  const Exp *DoLet(const std::vector<std::string> tyvars,
                   const std::string &x,
                   const Exp *body,
                   const Exp *guess) override {
    if (!self.empty() && !ILUtil::IsExpVarFree(body, self)) {
      return pool->Fn("", x, DoExp(body), guess);
    }

    return pool->Fn(self, x, DoExp(body), guess);
  }
  #endif

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
        return pool->Let({}, x, arg, DoExp(body));
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
