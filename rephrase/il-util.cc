
#include "il-util.h"

#include <unordered_set>
#include <string>

#include "il.h"
#include "il-pass.h"
#include "functional-map.h"
#include "util.h"

static constexpr bool VERBOSE = true;

namespace { struct Unit { }; }
using FunctionalSet = FunctionalMap<std::string, Unit>;

namespace il {

struct CountVarsPass : public Pass<FunctionalSet> {
  using Pass::Pass;

  // Anything that binds a variable needs to pass it in the functional
  // set, so we can detect that it's not free. Only a few constructs
  // bind expression variables.

  // Since we know that this pass does nothing, we can just always return
  // the guess instead of calling constructors.

  // Exp vars can't appear in types, so don't even recurse into them.
  const Type *DoType(const Type *guess, FunctionalSet bound) override {
    return guess;
  }

  const Exp *DoLet(const std::vector<std::string> &tyvars,
                   const std::string &x,
                   const Exp *rhs,
                   const Exp *body,
                   const Exp *guess,
                   FunctionalSet bound) override {
    DoExp(rhs, bound);
    DoExp(body, bound.Insert(x, {}));
    return guess;
  }

  const Exp *DoFn(const std::string &self,
                  const std::string &x,
                  const Exp *body,
                  const Exp *guess,
                  FunctionalSet bound) override {
    FunctionalSet bbound = self.empty() ? bound : bound.Insert(self, {});
    DoExp(body, bbound.Insert(x, {}));
    return guess;
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

  const Exp *DoSumCase(
      const Exp *obj,
      const std::vector<
          std::tuple<std::string, std::string, const Exp *>> &arms,
      const Exp *def,
      const Exp *guess,
      FunctionalSet bound) override {
    DoExp(obj, bound);
    for (const auto &[s, x, arm] : arms) {
      DoExp(arm, bound.Insert(x, {}));
    }
    DoExp(def, bound);
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
  SubstPass(AstPool *pool,
            const std::vector<std::string> &tyvars,
            const Exp *e1,
            const std::string &x)
    : Pass(pool),
      tyvars(tyvars),
      e1(e1),
      target_var(x),
      freevars(ILUtil::FreeExpVars(e1)) {
  }

  // Note: When we hit a binding equal to the target var, we alpha
  // vary the binding, since this is correct and uniform. But it
  // would be more efficient to just stop.

  const Exp *DoLet(const std::vector<std::string> &let_tyvars,
                   const std::string &x_in,
                   const Exp *rhs,
                   const Exp *body,
                   const Exp *guess) override {

    std::string x = x_in;
    if (x_in == target_var || freevars.contains(x_in)) {
      std::tie(x, body) =
        ILUtil::AlphaVaryExp(pool, let_tyvars.size(), x_in, body);
    }

    return pool->Let(let_tyvars, x, DoExp(rhs), DoExp(body), guess);
  }

  const Exp *DoFn(const std::string &self_in,
                  const std::string &x_in,
                  const Exp *body,
                  const Exp *guess) override {
    // Alpha vary inner binding first in case self == x. (This would
    // be weird, but technically valid?)
    std::string x = x_in;
    if (x_in == target_var || freevars.contains(x_in)) {
      // These variables cannot be polymorphic, so 0 tyvars.
      std::tie(x, body) = ILUtil::AlphaVaryExp(pool, 0, x_in, body);
    }

    std::string self = self_in;
    if (self_in == target_var || freevars.contains(self_in)) {
      // These variables cannot be polymorphic, so 0 tyvars.
      std::tie(self, body) = ILUtil::AlphaVaryExp(pool, 0, self_in, body);
    }

    return pool->Fn(self, x, DoExp(body), guess);
  }

  const Exp *DoVar(const std::vector<const Type *> &ts,
                   const std::string &v,
                   const Exp *guess) override {
    if (v == target_var) {
      // The target variable.
      CHECK(ts.size() == tyvars.size()) << "Internal type error: When "
        "substituting for " << v << ", expected the number of type "
        "variables in the substitution (" << tyvars.size() << ") to "
        "be the same as the number of types in the variable's occurrence "
        "(" << ts.size() << ").";
      const Exp *result = e1;
      // Now we are substituting Λ(α1 ... αn).e1 for v<τ1 ... τn>, so
      // compute [τ1/α1]...[τn/αn]e1.
      for (int i = (int)ts.size() - 1; i >= 0; i--) {
        result = ILUtil::SubstTypeInExp(pool, ts[i], tyvars[i], result);
      }
      return e1;
    } else {
      // Types are unaffected by substitution for expression variables.
      return guess;
    }
  }

  const Exp *DoSumCase(
      const Exp *obj,
      const std::vector<
          std::tuple<std::string, std::string, const Exp *>> &arms,
      const Exp *def,
      const Exp *guess) override {
    std::vector<
      std::tuple<std::string, std::string, const Exp *>> narms;
    narms.reserve(arms.size());
    for (const auto &[s, x, arm] : arms) {
      // Arms bind variables, so we may need to rename to avoid capture.
      if (x == target_var || freevars.contains(x)) {
        // These variables cannot be polymorphic, so 0 tyvars.
        const auto &[nx, narm] = ILUtil::AlphaVaryExp(pool, 0, x, arm);
        narms.emplace_back(s, nx, DoExp(narm));
      } else {
        narms.emplace_back(s, x, DoExp(arm));
      }
    }
    return pool->SumCase(DoExp(obj), std::move(narms),
                         DoExp(def), guess);
  }


  // The type variables bound in e1.
  const std::vector<std::string> &tyvars;
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
  return SubstPolyExp(pool, {}, e1, x, e2);
}

const Exp *ILUtil::SubstPolyExp(AstPool *pool,
                                const std::vector<std::string> &tyvars,
                                const Exp *e1, const std::string &x,
                                const Exp *e2) {
  if (VERBOSE) {
    printf("Subst [Λ(%s).%s/%s](%s).\n",
           Util::Join(tyvars, ",").c_str(),
           ExpString(e1).c_str(), x.c_str(),
           ExpString(e2).c_str());
  }
  SubstPass pass(pool, tyvars, e1, x);
  return pass.DoExp(e2);
}

std::pair<std::string, const Exp *> ILUtil::AlphaVaryExp(
    AstPool *pool,
    int num_tyvars,
    const std::string &x,
    const Exp *e) {

  // SubstPass uses AlphaVaryExp to avoid capture, but it
  // will not lead to infinite regress because the variable
  // is fresh (so it will not equal any bound variable).
  std::string xnew = pool->NewVar(x);
  std::vector<std::string> tyvars;
  std::vector<const Type *> types;
  // n distinct type variables (often zero).
  for (int i = 0; i < num_tyvars; i++) {
    std::string a = pool->NewVar("a");
    tyvars.push_back(a);
    types.push_back(pool->VarType(a));
  }
  // Substitute the expression Λ(α1 ... αn).y<α1 ... αn> where y
  // is the fresh variable.
  const Exp *vnew = pool->Var(types, xnew);
  SubstPass pass(pool, tyvars, vnew, x);

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

namespace {
struct CountLabelsPass : public Pass<> {
  using Pass::Pass;

  // Labels can't appear in types. Don't recurse into them.
  const Type *DoType(const Type *guess) override {
    return guess;
  }

  const Exp *DoGlobalSym(const std::vector<const Type *> &ts,
                         const std::string &sym,
                         const Exp *guess) override {
    counts[sym]++;
    return guess;
  }

  std::unordered_map<std::string, int> counts;
};
}  // namespace

std::unordered_map<std::string, int> ILUtil::LabelCounts(const Exp *e) {
  AstPool pool;
  CountLabelsPass pass(&pool);
  pass.DoExp(e);
  return pass.counts;
}

namespace {
struct SubstForLabelPass : public Pass<> {
  SubstForLabelPass(AstPool *pool,
                    const std::vector<std::string> &tyvars,
                    const Exp *e1,
                    const std::string &sym)
    : Pass(pool),
      tyvars(tyvars),
      e1(e1),
      target_sym(sym) {
    CHECK(ILUtil::FreeExpVars(e1).empty()) << "Expression being substituted "
      "for a label (" << sym << ") must be closed:\n" << ExpString(e1);
  }

  // Labels can't appear in types. Don't recurse into them.
  const Type *DoType(const Type *guess) override {
    return guess;
  }

  const Exp *DoGlobalSym(const std::vector<const Type *> &ts,
                         const std::string &sym,
                         const Exp *guess) override {
    if (sym == target_sym) {
      // The target variable.
      CHECK(ts.size() == tyvars.size()) << "Internal type error: When "
        "substituting for the label " << sym << ", expected the number "
        "of type variables in the substitution (" << tyvars.size() <<
        ") to be the same as the number of types in the symbol's "
        "occurrence (" << ts.size() << ").";
      const Exp *result = e1;
      // Now we are substituting Λ(α1 ... αn).e1 for v<τ1 ... τn>, so
      // compute [τ1/α1]...[τn/αn]e1.
      for (int i = (int)ts.size() - 1; i >= 0; i--) {
        result = ILUtil::SubstTypeInExp(pool, ts[i], tyvars[i], result);
      }
      return e1;
    } else {
      // Types are unaffected by substitution for expression variables.
      return guess;
    }
  }

  // The type variables bound in e1.
  const std::vector<std::string> &tyvars;
  // Term being substituted.
  const Exp *e1 = nullptr;
  // Target variable.
  const std::string target_sym;
};
}  // namespace

const Exp *ILUtil::SubstPolyExpForLabel(
    AstPool *pool,
    // α1, α2, ... αn
    const std::vector<std::string> &tyvars,
    const Exp *e1,
    const std::string &sym,
    const Exp *e2) {
  return SubstForLabelPass(pool, tyvars, e1, sym).DoExp(e2);
}


namespace {
struct FinalizeEVarsPass : public Pass<> {
  FinalizeEVarsPass(AstPool *pool, const Type *replacement_type)
    : Pass(pool),
      replacement_type(replacement_type) {
  }

  const Type *DoEVar(EVar a, const Type *guess) override {
    // Recurse inside bound evars.
    if (const Type *t = a.GetBound()) {
      return DoType(t);
    } else {
      // And set free ones.
      a.Set(replacement_type);
      return replacement_type;
    }
  }

  // Type being assigned to each evar.
  const Type *replacement_type = nullptr;
};
}  // namespace

Program ILUtil::FinalizeEVars(AstPool *pool, const Program &program) {
  FinalizeEVarsPass pass(pool, pool->SumType({}));
  return pass.DoProgram(program);
}

}  // namespace il
