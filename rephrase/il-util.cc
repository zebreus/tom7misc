
#include "il-util.h"

#include <unordered_set>
#include <string>

#include "il.h"
#include "il-pass.h"
#include "functional-map.h"

namespace { struct Unit { }; }
using FunctionalSet = FunctionalMap<std::string, Unit>;

namespace il {

struct CountVarsPass : public Pass<FunctionalSet> {
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
      counts[v]++;
    }
    // We know we're not actually doing anything to types, so
    // don't bother recursing on those.
    return guess;
  }

  std::unordered_map<std::string, int> counts;
};

std::unordered_map<std::string, int> ILUtil::FreeExpVarCounts(const Exp *e) {
  AstPool temp;
  CountVarsPass pass(&temp);
  (void)pass.DoExp(e, {});
  return pass.counts;
}


std::unordered_set<std::string> ILUtil::FreeExpVars(const Exp *e) {
  std::unordered_set<std::string> vars;
  for (const auto &[v, c] : FreeExpVarCounts(e))
    vars.insert(v);
  return vars;
}


bool ILUtil::IsExpVarFree(const Exp *e, const std::string &x) {
  // PERF: We can avoid allocating the set (and managing the "bound"
  // set in the pass) when we're just checking for one free variable.
  return FreeExpVarCounts(e).contains(x);
}

int ILUtil::ExpVarCount(const Exp *e, const std::string &x) {
  return FreeExpVarCounts(e)[x];
}

namespace {
// Could perhaps do this as a multiple substitution?
struct SubstPass : public Pass<> {
  SubstPass(AstPool *pool, const Exp *e1, const std::string &x)
    : Pass(pool), e1(e1), target_var(x), freevars(ILUtil::FreeExpVars(e1)) {
  }

  // Note: When we hit a binding equal to the target var, we alpha
  // vary the binding, since this is correct and uniform. But it
  // would be more efficient to just stop.

  const Exp *DoLet(const std::vector<std::string> &tyvars,
                   const std::string &x_in,
                   const Exp *rhs,
                   const Exp *body,
                   const Exp *guess) override {

    std::string x = x_in;
    if (x_in == target_var || freevars.contains(x_in)) {
      std::tie(x, body) = ILUtil::AlphaVaryExp(pool, x_in, body);
    }

    return pool->Let(tyvars, x, DoExp(rhs), DoExp(body), guess);
  }

  const Exp *DoFn(const std::string &self_in,
                  const std::string &x_in,
                  const Exp *body,
                  const Exp *guess) override {
    // Alpha vary inner binding first in case self == x. (This would
    // be weird, but technically valid?)
    std::string x = x_in;
    if (x_in == target_var || freevars.contains(x_in)) {
      std::tie(x, body) = ILUtil::AlphaVaryExp(pool, x_in, body);
    }

    std::string self = self_in;
    if (self_in == target_var || freevars.contains(self_in)) {
      std::tie(self, body) = ILUtil::AlphaVaryExp(pool, self_in, body);
    }

    return pool->Fn(self, x, DoExp(body), guess);
  }

  const Exp *DoVar(const std::vector<const Type *> &ts,
                   const std::string &v,
                   const Exp *guess) override {
    if (v == target_var) {
      // The target variable.
      return e1;
    } else {
      // Types are unaffected by substitution for expression variables.
      return guess;
    }
  }

  // Term being substituted.
  const Exp *e1 = nullptr;
  // Target variable.
  const std::string target_var;
  // Free variables of the term being substituted; we use this
  // to check for capture as we descend.
  const std::unordered_set<std::string> freevars;
};
}  // namespace

const Exp *ILUtil::SubstExp(AstPool *pool,
                            const Exp *e1, const std::string &x,
                            const Exp *e2) {
  SubstPass pass(pool, e1, x);
  return pass.DoExp(e2);
}

std::pair<std::string, const Exp *> ILUtil::AlphaVaryExp(
    AstPool *pool,
    const std::string &x,
    const Exp *e) {

  // SubstPass uses AlphaVaryExp to avoid capture, but it
  // will not lead to infinite regress because the variable
  // is fresh (so it will not equal any bound variable).
  std::string xnew = pool->NewVar(x);
  const Exp *vnew = pool->Var({}, xnew);
  SubstPass pass(pool, vnew, x);

  return std::make_pair(xnew, pass.DoExp(e));
}


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
