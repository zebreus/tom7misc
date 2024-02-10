
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

  const Exp *DoLet(const std::vector<std::string> &tyvars,
                   const std::string &x,
                   const Exp *rhs,
                   const Exp *body,
                   const Exp *guess) override {
    if (body->type == ExpType::VAR) {
      const auto &[vtv, xx] = body->Var();
      if (x == xx) {
        // let (a, b, ...) x = rhs in x<t1, t2, ...> end -->
        // [t1/a][t2/b]rhs
        CHECK(tyvars.size() == vtv.size());

        for (int i = 0; i < (int)tyvars.size(); i++) {
          // make sure the bound tyvar is fresh (not appearing in any
          // t1..tn).
          const auto &[a, nrhs] =
            ILUtil::AlphaVaryTypeInExp(pool, tyvars[i], rhs);
          rhs = nrhs;
          // and substitute for it
          rhs = ILUtil::SubstTypeInExp(pool, vtv[i], a, rhs);
        }

        return DoExp(rhs);
      } else {
        // This is handled by the below since x is not free in xx.
      }
    }

    // TODO A polymorphic declaration can be free too. But
    // we would need to substitute through the tyvars in
    // the rhs with dummy types so that they remain well-formed.
    // (Or drop the whole binding.)
    if (tyvars.empty() && !ILUtil::IsExpVarFree(body, x)) {
      return pool->Seq({DoExp(rhs), DoExp(body)});
    }

    return pool->Let(tyvars, x, DoExp(rhs), DoExp(body), guess);
  }


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
