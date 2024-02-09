
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

  // Need to collect the vars for the body, so we can't just do
  // this by overriding DoDec.
  const Exp *DoLet(const std::vector<const Dec *> &ds,
                   const Exp *e,
                   const Exp *guess,
                   FunctionalSet bound) override {
    std::vector<const Dec *> dds;
    dds.reserve(ds.size());
    for (const Dec *d : ds) {
      const Dec *dd = DoDec(d, bound);
      dds.push_back(dd);
      if (dd->type == DecType::VAL) {
        const auto &[tvs, x, rhs] = dd->Val();
        bound = bound.Insert(x, {});
      }
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
struct FreeVarsPass : public Pass<> {
  FreeVarsPass(AstPool *pool, const Exp *e1, const std::string &x)
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

}  // namespace il
