
#include "simplification.h"

#include "il.h"

#include "il-pass.h"
#include "il-util.h"
#include "bignum/big-overloads.h"

namespace il {

Simplification::Simplification(AstPool *pool) : pool(pool) {}

void Simplification::SetVerbose(int v) {
  verbose = v;
}

// True if the expression is a value and cheaper/smaller than
// a variable lookup, and so it should always be inlined.
static bool IsSmallValue(const Exp *e) {
  switch (e->type) {
  case ExpType::FLOAT:
    return true;
  case ExpType::INTEGER:
    // Since we use bigint, avoid substituting huge numbers.
    // (This could probably be increased a lot without problems!)
    return e->Integer() < 4'000'000ULL;
  case ExpType::STRING:
    // PERF: Should consider inlining small strings by other means?
    return e->String().empty();
  case ExpType::VAR:
    return true;
  case ExpType::RECORD:
    return e->Record().size() == 0;
  default:
    return false;
  }
}

static bool IsEffectless(const Exp *e) {
  switch (e->type) {
  case ExpType::FLOAT: return true;
  case ExpType::INTEGER: return true;
  case ExpType::STRING: return true;
  case ExpType::VAR: return true;
  case ExpType::FN: return true;

  case ExpType::RECORD: {
    for (const auto &[lab, child] : e->Record()) {
      if (!IsEffectless(child)) return false;
    }
    return true;
  }

  case ExpType::PROJECT:
    return IsEffectless(std::get<1>(e->Project()));
  case ExpType::INJECT:
    return IsEffectless(std::get<1>(e->Inject()));

  case ExpType::ROLL:
    return IsEffectless(std::get<1>(e->Roll()));

  case ExpType::PRIMOP: {
    const auto &[po, ts, es] = e->Primop();
    if (IsPrimopTotal(po)) {
      for (const Exp *child : es) {
        if (!IsEffectless(child)) {
          return false;
        }
      }
      return true;
    }

    return false;
  }

  default:
    return false;
  }
}

// This is almost the same as effectless except that some primops can
// be dropped (e.g. GET) despite not being formally total. We have to
// do the whole thing instead of appealing to IsEffectless for other
// cases, since we want something like (GET, GET) to be considered
// discardable.
static bool IsDiscardable(const Exp *e) {
  switch (e->type) {
  case ExpType::FLOAT: return true;
  case ExpType::INTEGER: return true;
  case ExpType::STRING: return true;
  case ExpType::VAR: return true;
  case ExpType::FN: return true;

  case ExpType::RECORD: {
    for (const auto &[lab, child] : e->Record()) {
      if (!IsDiscardable(child)) return false;
    }
    return true;
  }

  case ExpType::PROJECT:
    return IsDiscardable(std::get<1>(e->Project()));
  case ExpType::INJECT:
    return IsDiscardable(std::get<1>(e->Inject()));

  case ExpType::ROLL:
    return IsDiscardable(std::get<1>(e->Roll()));

  case ExpType::PRIMOP: {
    const auto &[po, ts, es] = e->Primop();
    if (IsPrimopDiscardable(po)) {
      for (const Exp *child : es) {
        if (!IsDiscardable(child)) {
          return false;
        }
      }
      return true;
    }

    return false;
  }

  default:
    return false;
  }
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
      Simplified("remove recursive fn var");
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

        Simplified("eta-contracted let x = e in x");
        return DoExp(rhs);
      } else {
        // This is handled by the below since x is not free in xx.
      }
    }

    int count = ILUtil::ExpVarCount(body, x);

    // TODO A polymorphic declaration can be free too. But
    // we would need to substitute through the tyvars in
    // the rhs with dummy types so that they remain well-formed.
    // (Or drop the whole binding.)
    if (tyvars.empty() && count == 0) {
      Simplified("remove unused binding");
      return pool->Seq({DoExp(rhs), DoExp(body)});
    }

    const bool small_value = IsSmallValue(rhs);
    const bool effectless = small_value || IsEffectless(rhs);

    // TODO: support inlining of polymorphic variables.
    // I think we just want to extend substitution with support for
    // this.
    if (count == 1 && effectless && tyvars.empty()) {
      // Inline any effectless expression that occurs just once.
      Simplified("inlined single-use binding");
      return ILUtil::SubstExp(pool, DoExp(rhs), x, DoExp(body));
    }

    if (small_value && tyvars.empty()) {
      Simplified("inlined small value");
      return ILUtil::SubstExp(pool, DoExp(rhs), x, DoExp(body));
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
        Simplified("reduce app");
        return pool->Let({}, x, arg, DoExp(body));
      }
    }
    return pool->App(DoExp(f), arg, guess);
  }

  const Exp *DoSeq(const std::vector<const Exp *> &v,
                   const Exp *guess) override {
    // First process them all recursively, so that they are flat.
    std::vector<const Exp *> vv;
    vv.reserve(v.size());
    for (const Exp *c : v) vv.push_back(DoExp(c));
    std::vector<const Exp *> vflat;
    for (const Exp *c : vv) {
      // printf("IsEffectless %s?\n", ExpString(c).c_str());
      if (IsDiscardable(c)) {
        // FIXME: We shouldn't drop every item in the seq
        Simplified("dropped effectless seq");
      } else {
        if (c->type == ExpType::SEQ) {
          Simplified("flattened nested seq");
          for (const Exp *cc : c->Seq()) {
            vflat.push_back(cc);
          }
        } else {
          vflat.push_back(c);
        }
      }
    }

    return pool->Seq(vflat, guess);
  }

  // Call this whenever the expression definitely got smaller.
  void Simplified(const char *msg) {
    simplified++;
  }

  void Reset() {
    simplified = 0;
  }

  int simplified = 0;
};


const il::Exp *Simplification::Simplify(const Exp *exp) {
  PeepholePass peephole(pool);

  do {
    peephole.Reset();
    exp = peephole.DoExp(exp);
  } while (peephole.simplified > 0);

  return exp;
}

}  // namespace il
