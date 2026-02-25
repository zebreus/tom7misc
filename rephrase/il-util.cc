
#include "il-util.h"

#include <algorithm>
#include <format>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "functional-set.h"
#include "il-pass.h"
#include "il.h"
#include "unification.h"
#include "util.h"

static constexpr bool VERBOSE = false;

using StringSet = FunctionalSet<std::string>;

namespace il {

// Expression variables.
namespace {
// PERF: We can use different accumulators so that when we're just getting
// the free variable set, we don't have to maintain a whole vector of
// all the occurrences.
struct TallyVarsPass : public Pass<StringSet> {
  using Pass::Pass;

  // Anything that binds a variable needs to pass it in the functional
  // set, so we can detect that it's not free. Only a few constructs
  // bind expression variables.

  // Since we know that this pass does nothing, we can just always return
  // the guess instead of calling constructors.

  // Exp vars can't appear in types, so don't even recurse into them.
  const Type *DoType(const Type *guess, StringSet bound) override {
    return guess;
  }

  const Exp *DoLet(const std::vector<std::string> &tyvars,
                   const std::string &x,
                   const Exp *rhs,
                   const Exp *body,
                   const Exp *guess,
                   StringSet bound) override {
    DoExp(rhs, bound);
    DoExp(body, bound.Insert(x));
    return guess;
  }

  const Exp *DoFn(const std::string &self,
                  const std::string &x,
                  const Type *arrow_type,
                  const Exp *body,
                  const Exp *guess,
                  StringSet bound) override {
    StringSet bbound = self.empty() ? bound : bound.Insert(self);
    DoExp(body, bbound.Insert(x));
    return guess;
  }

  const Exp *DoVar(const std::vector<const Type *> &ts,
                   const std::string &v,
                   const Exp *guess,
                   StringSet bound) override {
    if (!bound.Contains(v)) {
      // Then it is free.
      tally[v].push_back(ts);
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
      StringSet bound) override {
    DoExp(obj, bound);
    for (const auto &[s, x, arm] : arms) {
      DoExp(arm, bound.Insert(x));
    }
    DoExp(def, bound);
    return guess;
  }

  const Exp *DoUnpack(
      const std::string &alpha, const std::string &x, const Exp *rhs,
      const Exp *body, const Exp *guess, StringSet bound) override {
    DoExp(rhs, bound);
    DoExp(body, bound.Insert(x));
    return guess;
  }

  // For each occurrence, the type variables it was applied to.
  std::unordered_map<std::string, std::vector<std::vector<const Type *>>>
  tally;
};
}  // namespace

std::unordered_map<std::string, std::vector<std::vector<const Type *>>>
ILUtil::FreeExpVarTally(const Exp *e) {
  AstPool temp;
  TallyVarsPass pass(&temp);
  (void)pass.DoExp(e, {});
  return pass.tally;
}

std::unordered_map<std::string, int> ILUtil::FreeExpVarCounts(const Exp *e) {
  std::unordered_map<std::string, int> ret;
  for (const auto &[s, v] : FreeExpVarTally(e))
    ret[s] = (int)v.size();
  return ret;
}

std::unordered_set<std::string> ILUtil::FreeExpVars(const Exp *e) {
  std::unordered_set<std::string> vars;
  for (const auto &[s, v] : FreeExpVarTally(e))
    vars.insert(s);
  return vars;
}

bool ILUtil::IsExpVarFree(const Exp *e, const std::string &x) {
  // PERF: We can avoid allocating the set (and managing the "bound"
  // set in the pass) when we're just checking for one free variable.
  return FreeExpVarTally(e).contains(x);
}

int ILUtil::ExpVarCount(const Exp *e, const std::string &x) {
  return FreeExpVarCounts(e)[x];
}

namespace {
// Could perhaps do this as a multiple substitution?
struct SubstPass : public Pass<> {
  SubstPass(AstPool *p,
            const std::vector<std::string> &tyvars,
            const Exp *e1,
            const std::string &x)
    : Pass(p),
      tyvars(tyvars),
      e1(e1),
      target_var(x),
      freevars(ILUtil::FreeExpVars(e1)) {
  }

  // Expression variables can't appear in types. Don't recurse into them.
  const Type *DoType(const Type *guess) override {
    return guess;
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
                  const Type *arrow_type,
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

    return pool->Fn(self, x, DoType(arrow_type), DoExp(body), guess);
  }

  const Exp *DoVar(const std::vector<const Type *> &ts,
                   const std::string &v,
                   const Exp *guess) override {
    if (v == target_var) {
      if (VERBOSE) {
        Print(AGREEN("{}") " with {} type params\n",
              v, ts.size());
      }
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
      return result;
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

  const Exp *DoUnpack(
      const std::string &alpha, const std::string &x, const Exp *rhs,
      const Exp *body, const Exp *guess) override {
    if (x == target_var || freevars.contains(x)) {
      const auto &[newx, newbody] = ILUtil::AlphaVaryExp(pool, 0, x, body);
      return pool->Unpack(alpha, newx, DoExp(rhs), DoExp(newbody), guess);
    } else {
      return pool->Unpack(alpha, x, DoExp(rhs), DoExp(body), guess);
    }

    return guess;
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
    Print(APURPLE("Subst") " [Λ({}).{}/" ABLUE("{}") "]({}).\n",
          Util::Join(tyvars, ","),
          ExpString(e1), x,
          ExpString(e2));
  }
  SubstPass pass(pool, tyvars, e1, x);
  const Exp *ret = pass.DoExp(e2);
  if (VERBOSE) {
    Print(AORANGE("Result") ":\n{}\n", ExpString(ret));
  }
  return ret;
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


// If is_fresh, then we ignore the possibility of capture; if t1 has
// any variables in it, they must all be fresh and appear nowhere in the
// target expression.
template<bool is_fresh>
struct SubstTypePass : public Pass<> {
  SubstTypePass(AstPool *p, const Type *t1, const std::string &x)
    : Pass(p), t1(t1), target_var(x), freevars(ILUtil::FreeTypeVars(t1)) {
  }

  // When we get to any type, we just defer to native substitution.
  const Type *DoType(const Type *t) override {
    return pool->SubstType(t1, target_var, t);
  }

  // Avoid capture in the expression constructs that bind type
  // variables.
  const Exp *DoUnpack(
      const std::string &alpha, const std::string &x, const Exp *rhs,
      const Exp *body, const Exp *guess) override {
    if constexpr (is_fresh) {
      return Pass::DoUnpack(alpha, x, rhs, body, guess);
    } else {

      if (alpha == target_var || freevars.contains(alpha)) {
        const auto &[newalpha, newbody] =
          ILUtil::AlphaVaryTypeInExp(pool, alpha, body);
        return pool->Unpack(newalpha, x, DoExp(rhs), DoExp(newbody), guess);
      } else {
        return Pass::DoUnpack(alpha, x, rhs, body, guess);
      }
    }
  }

  const Exp *DoPack(const Type *t_hidden, const std::string &alpha,
                    const Type *t_packed, const Exp *body,
                    const Exp *guess) override {
    if constexpr (is_fresh) {
      return Pass::DoPack(t_hidden, alpha, t_packed, body, guess);
    } else {
      if (alpha == target_var || freevars.contains(alpha)) {
        // Alpha's scope includes both t_packed and body. We use
        // Fail just to pair those together.
        const Exp *fail = pool->Fail(body, t_packed);
        const auto &[newalpha, newfail] =
          ILUtil::AlphaVaryTypeInExp(pool, alpha, fail);
        const auto &[newbody, newt_packed] = newfail->Fail();

        return pool->Pack(DoType(t_hidden), newalpha,
                          DoType(newt_packed), newbody, guess);

      } else {
        return Pass::DoPack(t_hidden, alpha, t_packed, body, guess);
      }
    }
  }

  const Exp *DoTypeFn(
      const std::string &alpha, const Exp *body,
      const Exp *guess) override {
    if constexpr (is_fresh) {
      return Pass::DoTypeFn(alpha, body, guess);
    } else {
      if (alpha == target_var || freevars.contains(alpha)) {
        const auto &[newalpha, newbody] =
          ILUtil::AlphaVaryTypeInExp(pool, alpha, body);
        return pool->TypeFn(newalpha, DoExp(body), guess);
      } else {
        return Pass::DoTypeFn(alpha, body, guess);
      }
    }
  }


  const Type *DoMu(
      int idx,
      const std::vector<std::pair<std::string, const Type *>> &v,
      const Type *guess) override {
    LOG(FATAL) << "This should not be called, because we defer to SubstType.";
    return nullptr;
  }


  const Type *DoExists(
      const std::string &alpha,
      const Type *body,
      const Type *guess) override {
    LOG(FATAL) << "This should not be called, because we defer to SubstType.";
    return nullptr;
  }

  const Type *DoForall(
      const std::string &alpha,
      const Type *body,
      const Type *guess) override {
    LOG(FATAL) << "This should not be called, because we defer to SubstType.";
    return nullptr;
  }

  // Type being substituted.
  const Type *t1 = nullptr;
  // Target variable.
  const std::string target_var;
  const std::unordered_set<std::string> freevars;
};

const Exp *ILUtil::SubstTypeInExp(
    AstPool *pool,
    const Type *t, const std::string &x,
    const Exp *e) {
  SubstTypePass<false> pass(pool, t, x);
  return pass.DoExp(e);
}

std::pair<std::string, const Exp *> ILUtil::AlphaVaryTypeInExp(
    AstPool *pool,
    const std::string &a,
    const Exp *e) {
  std::string newa = pool->NewVar(a);
  // Here, newa is fresh so we can use a simple substitution.
  SubstTypePass<true> pass(pool, pool->VarType(newa, {}), a);
  return std::make_pair(newa, pass.DoExp(e));
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
  SubstForLabelPass(AstPool *p,
                    const std::vector<std::string> &tyvars,
                    const Exp *e1,
                    const std::string &sym)
    : Pass(p),
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
  FinalizeEVarsPass(AstPool *p, const Type *replacement_type)
    : Pass(p),
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


// Type variables.
// Exactly analogous to the expression variable version above, but
// the type language is smaller.
namespace {
struct CountTypeVarsPass : public Pass<StringSet> {
  using Pass::Pass;

  const Type *DoEVar(EVar a,
                     const Type *guess,
                     StringSet bound) override {
    // Recurse inside bound evars.
    if (const Type *t = a.GetBound()) {
      (void)DoType(t, bound);
    }
    // Here we always return the guess since we want complete
    // sharing.
    return guess;
  }

  const Type *DoVarType(const std::string &s,
                        const std::vector<const Type *> &v,
                        const Type *guess,
                        StringSet bound) override {
    for (const Type *t : v) (void)DoType(t, bound);
    if (!bound.Contains(s)) {
      // Then it is free.
      counts[s]++;
    }
    return guess;
  }

  // Constructors that bind type variables.
  const Type *DoMu(
      int idx,
      const std::vector<std::pair<std::string, const Type *>> &v,
      const Type *guess,
      StringSet bound) override {
    // All variables are bound in all arms.
    for (const auto &[alpha, t] : v) {
      bound = bound.Insert(alpha);
    }

    for (const auto &[alpha, t] : v) {
      (void)DoType(t, bound);
    }

    return guess;
  }

  const Type *DoExists(
      const std::string &alpha,
      const Type *body,
      const Type *guess,
      StringSet bound) override {
    (void)DoType(body, bound.Insert(alpha));
    return guess;
  }

  const Type *DoForall(
      const std::string &alpha,
      const Type *body,
      const Type *guess,
      StringSet bound) override {
    (void)DoType(body, bound.Insert(alpha));
    return guess;
  }

  // Globals can bind type vars.

  Program DoProgram(const Program &program, StringSet bound)
    override {
    for (const Global &glob : program.globals) {
      StringSet bb = bound;
      for (const std::string &a : glob.tyvars) bb.Insert(a);
      (void)DoType(glob.type, bb);
      (void)DoExp(glob.exp, bb);
    }
    (void)DoExp(program.body, bound);
    return program;
  }

  // Expressions that bind type vars.

  const Exp *DoLet(const std::vector<std::string> &tyvars,
                   const std::string &x,
                   const Exp *rhs,
                   const Exp *body,
                   const Exp *guess,
                   StringSet bound) override {
    StringSet bb = bound;
    for (const std::string &alpha : tyvars) bb = bb.Insert(alpha);
    (void)DoExp(rhs, bb);
    (void)DoExp(body, bound);
    return guess;
  }

  const Exp *DoUnpack(
      const std::string &alpha, const std::string &x, const Exp *rhs,
      const Exp *body, const Exp *guess, StringSet bound) override {
    (void)DoExp(rhs, bound);
    StringSet bb = bound.Insert(alpha);
    (void)DoExp(body, bb);
    return guess;
  }

  const Exp *DoPack(const Type *t_hidden, const std::string &alpha,
                    const Type *t_packed, const Exp *body,
                    const Exp *guess, StringSet bound) override {
    (void)DoType(t_hidden, bound);
    StringSet bb = bound.Insert(alpha);
    (void)DoType(t_packed, bb);
    (void)DoExp(body, bb);
    return guess;
  }

  const Exp *DoTypeFn(const std::string &alpha, const Exp *body,
                      const Exp *guess, StringSet bound) override {
    (void)DoExp(body, bound.Insert(alpha));
    return guess;
  }


  std::unordered_map<std::string, int> counts;
};
}  // namespace

std::unordered_map<std::string, int> ILUtil::FreeTypeVarCounts(const Type *t) {
  AstPool temp;
  CountTypeVarsPass pass(&temp);
  (void)pass.DoType(t, {});
  return pass.counts;
}

std::unordered_set<std::string> ILUtil::FreeTypeVars(const Type *t) {
  std::unordered_set<std::string> vars;
  for (const auto &[v, c] : FreeTypeVarCounts(t))
    vars.insert(v);
  return vars;
}

std::unordered_set<std::string> ILUtil::FreeTypeVarsInExp(const Exp *e) {
  AstPool temp;
  CountTypeVarsPass pass(&temp);
  pass.DoExp(e, {});
  std::unordered_set<std::string> vars;
  for (const auto &[v, c] : pass.counts)
    vars.insert(v);
  return vars;
}

namespace {
struct UniversePass : public Pass<> {
  using Pass::Pass;

  // Here we are modifying the member sets (if non-null). We want to
  // find binding sites but we aren't managing scope.
  // Since we know that this pass does nothing, we can just always return
  // the guess instead of calling constructors.

  // Not owned, and may be null.
  std::unordered_set<std::string> *exp_vars;
  std::unordered_set<std::string> *typ_vars;

  UniversePass(
      AstPool *pool,
      std::unordered_set<std::string> *exp_vars,
      std::unordered_set<std::string> *typ_vars) :
    Pass(pool),
    exp_vars(exp_vars),
    typ_vars(typ_vars) {}

  void AddExpVar(const std::string &s) {
    if (exp_vars != nullptr) exp_vars->insert(s);
  }

  void AddTypVar(const std::string &s) {
    if (typ_vars != nullptr) typ_vars->insert(s);
  }

  const Type *DoType(const Type *guess) override {
    // If we aren't looking for type vars, we don't need to
    // recurse, since exp vars can't appear in types.
    CHECK(typ_vars == nullptr) << "Unimplemented, but this "
      "is easy!";
    if (typ_vars == nullptr) return guess;
    return Pass::DoType(guess);
  }

  const Exp *DoLet(const std::vector<std::string> &tyvars,
                   const std::string &x,
                   const Exp *rhs,
                   const Exp *body,
                   const Exp *guess) override {
    if (typ_vars != nullptr)
      for (const std::string &a : tyvars)
        AddTypVar(a);
    AddExpVar(x);
    DoExp(rhs);
    DoExp(body);
    return guess;
  }

  const Exp *DoFn(const std::string &self,
                  const std::string &x,
                  const Type *arrow_type,
                  const Exp *body,
                  const Exp *guess) override {
    if (!self.empty()) {
      AddExpVar(self);
    }
    AddExpVar(x);
    DoType(arrow_type);
    DoExp(body);
    return guess;
  }

  const Exp *DoVar(const std::vector<const Type *> &ts,
                   const std::string &v,
                   const Exp *guess) override {
    for (const Type *t : ts) {
      DoType(t);
    }
    AddExpVar(v);
    return guess;
  }

  const Exp *DoSumCase(
      const Exp *obj,
      const std::vector<
          std::tuple<std::string, std::string, const Exp *>> &arms,
      const Exp *def,
      const Exp *guess) override {
    DoExp(obj);
    for (const auto &[s, x, arm] : arms) {
      AddExpVar(x);
      DoExp(arm);
    }
    DoExp(def);
    return guess;
  }

  const Exp *DoUnpack(
      const std::string &alpha, const std::string &x, const Exp *rhs,
      const Exp *body, const Exp *guess) override {
    AddTypVar(alpha);
    AddExpVar(x);
    DoExp(rhs);
    DoExp(body);
    return guess;
  }

  Program DoProgram(const Program &program) override {
    if (typ_vars != nullptr) {
      for (const Global &g : program.globals) {
        for (const std::string &a : g.tyvars) {
          AddTypVar(a);
        }
      }
    }

    return Pass::DoProgram(program);
  }

};
}


void ILUtil::GetUniverse(
    const Program &program,
    // If null, ignored (and more efficient).
    std::unordered_set<std::string> *exp_vars,
    std::unordered_set<std::string> *typ_vars) {
  if (exp_vars == nullptr && typ_vars == nullptr)
    return;

  // Nothing actually allocated, but we need one for Pass.
  AstPool temp;
  UniversePass pass(&temp, exp_vars, typ_vars);
  (void)pass.DoProgram(program);
}


std::string ILUtil::VarSetString(const std::unordered_set<std::string> &s) {
  std::vector<std::string> v(s.begin(), s.end());
  std::sort(v.begin(), v.end());
  return std::format("{{" "{}" "}}", Util::Join(v, ", "));
}

std::optional<il::ObjFieldType> ILUtil::GetObjFieldType(const il::Type *t) {
  switch (t->type) {
  case il::TypeType::INT: return {il::ObjFieldType::INT};
  case il::TypeType::STRING: return {il::ObjFieldType::STRING};
  case il::TypeType::FLOAT: return {il::ObjFieldType::FLOAT};
  case il::TypeType::BOOL: return {il::ObjFieldType::BOOL};
  case il::TypeType::OBJ: return {il::ObjFieldType::OBJ};
  case il::TypeType::LAYOUT: return {il::ObjFieldType::LAYOUT};
  default: return std::nullopt;
  }
}

const Type *ILUtil::ObjFieldTypeType(AstPool *pool, ObjFieldType oft) {
  switch (oft) {
  case ObjFieldType::STRING: return pool->StringType();
  case ObjFieldType::FLOAT: return pool->FloatType();
  case ObjFieldType::INT: return pool->IntType();
  case ObjFieldType::BOOL: return pool->BoolType();
  case ObjFieldType::OBJ: return pool->ObjType();
  case ObjFieldType::LAYOUT: return pool->LayoutType();
  }
  LOG(FATAL) << "Unimplemented ObjFieldType";
  return nullptr;
}

std::optional<const Type *> ILUtil::GetTypeIfKnown(const Type *t) {
  for (;;) {
    if (t->type == TypeType::EVAR) {
      t = t->EVar().GetBound();
      if (t == nullptr) return std::nullopt;
      // Otherwise continue on the referent.
    } else {
      return {t};
    }
  }
}

const Type * ILUtil::GetKnownType(const char *what, const Type *t) {
  const auto &to = GetTypeIfKnown(t);
  CHECK(to.has_value()) << "(" << what << ") Bug: Not expecting any "
    "free EVars at this point. In: " << TypeString(t);
  return to.value();
}

}  // namespace il
