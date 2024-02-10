
#include "il-util.h"

#include <unordered_set>
#include <string>

#include "il.h"
#include "il-pass.h"
#include "functional-map.h"

namespace { struct Unit { }; }
using FunctionalSet = FunctionalMap<std::string, Unit>;

namespace il {

struct FreeVarsPass : public Pass<FunctionalSet> {
  using Pass::Pass;

  // Anything that binds a variable needs to pass it in the functional
  // set, so we can detect that it's not free. Only a few constructs
  // bind expression variables.

  const Exp *DoLet(const std::vector<std::string> &tyvars,
                   const std::string &x,
                   const Exp *rhs,
                   const Exp *body,
                   const Exp *guess,
                   FunctionalSet bound) override {
    return pool->Let(tyvars, x, DoExp(rhs, bound),
                     DoExp(body, bound.Insert(x, {})),
                     guess);
  }

  const Exp *DoFn(const std::string &self,
                  const std::string &x,
                  const Exp *body,
                  const Exp *guess,
                  FunctionalSet bound) override {
    FunctionalSet bbound = self.empty() ? bound : bound.Insert(self, {});
    return pool->Fn(self, x,
                    DoExp(body,
                          bbound.Insert(x, {})),
                    guess);
  }

  const Exp *DoVar(const std::vector<const Type *> &ts,
                   const std::string &v,
                   const Exp *guess,
                   FunctionalSet bound) override {
    if (bound.FindPtr(v) == nullptr) {
      // Then it is free.
      freevars.insert(v);
    }
    // We know we're not actually doing anything to types, so
    // don't bother recursing on those.
    return guess;
  }


  std::unordered_set<std::string> freevars;
};

std::unordered_set<std::string> ILUtil::FreeExpVars(const Exp *e) {
  AstPool temp;
  FreeVarsPass pass(&temp);
  (void)pass.DoExp(e, {});
  return pass.freevars;
}

bool ILUtil::IsExpVarFree(const Exp *e, const std::string &x) {
  // PERF: We can avoid allocating the set (and managing the "bound"
  // set in the pass) if we're just checking for one free variable.
  const std::unordered_set<std::string> freevars = FreeExpVars(e);
  return freevars.contains(x);
}

#if 0
namespace {
// Could perhaps do this as a multiple substitution?
struct SubstPass : public Pass<> {
  SubstPass(AstPool *pool, const Exp *e1, const std::string &x)
    : pool(pool), e1(e1), x(x), freevars(FreeExpVars(e1)) {

  }

  // Need to collect the vars for the body, so we can't just do
  // this by overriding DoDec.
  const Exp *DoLet(const std::vector<const Dec *> &ds,
                   const Exp *e,
                   const Exp *guess) {
    std::vector<const Dec *> dds;
    dds.reserve(ds.size());
    for (const Dec *d : ds) {
      if (dd->type == DecType::VAL) {
        const auto &[tvs, x, rhs] = dd->Val();
        if (freevars.contains(x)) {
          // alpha-vary the bound variable.
          std::string newx = pool->NewVar(x);
        }
      }
      const Dec *dd = DoDec(d, bound);
    }
    return pool->Let(dds, DoExp(e, bound), guess);
  }

  const Exp *DoFn(const std::string &self,
                  const std::string &x,
                  const Exp *body,
                  const Exp *guess,
                  FunctionalSet bound) override {
    FunctionalSet bbound = self.empty() ? bound : bound.Insert(self, {});
    return pool->Fn(self, x,
                    DoExp(body,
                          bbound.Insert(x, {})),
                    guess);
  }

  const Exp *DoVar(const std::vector<const Type *> &ts,
                   const std::string &v,
                   const Exp *guess,
                   FunctionalSet bound) override {
    if (bound.FindPtr(v) == nullptr) {
      // Then it is free.
      freevars.insert(v);
    }
    // We know we're not actually doing anything to types, so
    // don't bother recursing on those.
    return guess;
  }

  // Term being substituted.
  const Exp *e1 = nullptr;
  // Target variable.
  const std::string x;
  // Free variables of the term being substituted; we use this
  // to check for capture as we descend.
  const std::unordered_set<std::string> freevars;
};
}  // namespace

const Exp *ILUtil::SubstExp(AstPool *pool,
                            const Exp *e1, const std::string &x,
                            const Exp *e2) {
  // Get the free vars of the substituted expression, so that
  // we can check for capture as we descend.


  SubstPass pass(&pool, e1, x);
  return pass.DoExp(e2);
}
#endif

// Could perhaps do this as a multiple substitution?
struct SubstTypePass : public Pass<> {
  SubstTypePass(AstPool *pool, const Type *t1, const std::string &x)
    : Pass(pool), t1(t1), x(x) {
  }

  // When we get to any type, we just defer to native substitution.
  const Type *DoType(const Type *t) override {
    return pool->SubstType(t1, x, t);
  }

  // Type being substituted.
  const Type *t1 = nullptr;
  // Target variable.
  const std::string x;
};

const Exp *ILUtil::SubstTypeInExp(
    AstPool *pool,
    const Type *t, const std::string &x,
    const Exp *e) {
  SubstTypePass pass(pool, t, x);
  return pass.DoExp(e);
}

std::pair<std::string, const Exp *> ILUtil::AlphaVaryTypeInExp(
    AstPool *pool,
    const std::string &a,
    const Exp *e) {
  std::string newa = pool->NewVar(a);
  return std::make_pair(
      newa,
      SubstTypeInExp(pool, pool->VarType(newa, {}), a, e));
}


}  // namespace il
