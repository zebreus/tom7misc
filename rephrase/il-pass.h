
#ifndef _REPHRASE_IL_PASS_H
#define _REPHRASE_IL_PASS_H

#include <string>

#include "il.h"

#include "base/logging.h"

// This is a recursive identity function over the IL AST.
// The idea is that you can override just the constructs
// that you're interested in.

namespace il {

template<typename... Args>
struct Pass {
  explicit Pass(AstPool *pool) : pool(pool) {}


  virtual const Type *DoType(const Type *t, Args... args) {
    switch (t->type) {
    case TypeType::VAR: {
      const auto &[x, tv] = t->Var();
      return DoVarType(x, tv, t, args...);
    }
    case TypeType::ARROW: {
      const auto &[dom, cod] = t->Arrow();
      return DoArrow(dom, cod, t, args...);
    }
    case TypeType::MU: {
      const auto &[idx, v] = t->Mu();
      return DoMu(idx, v, t, args...);
    }
    case TypeType::SUM: return DoSum(t->Sum(), t, args...);
    case TypeType::RECORD: return DoRecordType(t->Record(), t, args...);
    case TypeType::EVAR: return DoEVar(t->EVar(), t, args...);
    case TypeType::REF: return DoRefType(t->Ref(), t, args...);
    case TypeType::STRING: return DoStringType(t, args...);
    case TypeType::FLOAT: return DoFloatType(t, args...);
    case TypeType::INT: return DoIntType(t, args...);
      LOG(FATAL) << "Unhandled type type in Pass::DoExp!";
    }
  }

  virtual const Exp *DoExp(const Exp *e, Args... args) {
    switch (e->type) {
    case ExpType::STRING: return DoString(e->String(), e, args...);
    case ExpType::FLOAT: return DoFloat(e->Float(), e, args...);
    case ExpType::JOIN: return DoJoin(e->Join(), e, args...);
    case ExpType::RECORD: return DoRecord(e->Record(), e, args...);
    case ExpType::INTEGER: return DoInt(e->Integer(), e, args...);
    case ExpType::VAR: {
      const auto &[ts, v] = e->Var();
      return DoVar(ts, v, e, args...);
    }
    case ExpType::LAYOUT: {
      LOG(FATAL) << "Unimplemented";
    }
    case ExpType::LET: {
      const auto &[tyvars, x, rhs, body] = e->Let();
      return DoLet(tyvars, x, rhs, body, e, args...);
    }
    case ExpType::IF: {
      const auto &[cond, t, f] = e->If();
      return DoIf(cond, t, f, e, args...);
    }
    case ExpType::APP: {
      const auto &[f, x] = e->App();
      return DoApp(f, x, e, args...);
    }
    case ExpType::FN: {
      const auto &[self, x, body] = e->Fn();
      return DoFn(self, x, body, e, args...);
    }
    case ExpType::PROJECT: {
      const auto &[lab, exp] = e->Project();
      return DoProject(lab, exp, e, args...);
    }
    case ExpType::INJECT: {
      const auto &[lab, exp] = e->Inject();
      return DoInject(lab, exp, e, args...);
    }
    case ExpType::ROLL: {
      const auto &[type, exp] = e->Roll();
      return DoRoll(type, exp, e, args...);
    }
    case ExpType::PRIMOP: {
      const auto &[p, ts, es] = e->Primop();
      return DoPrimop(p, ts, es, e, args...);
    }
    case ExpType::FAIL: {
      return DoFail(e->Fail(), e, args...);
    }
    default:
      LOG(FATAL) << "Unhandled expression type in Pass::DoExp!";
    }
  }

  // All the cases take the AST node's components as arguments,
  // followed by the node itself (the "guess" for the constructors)
  // and then the args.

  virtual const Type *DoEVar(EVar a,
                             const Type *guess,
                             Args... args) {
    // Recurse inside bound evars.
    if (const Type *t = a.GetBound()) {
      // Supposing we did nothing to t, it's unclear whether it's
      // better to return the guess (which keeps the indirection
      // from the evar around) or collapse it away (now we can't
      // share up the tree).
      return DoType(t, args...);
    } else {
      return pool->EVar(a, guess);
    }
  }

  virtual const Type *DoVarType(const std::string &s,
                                const std::vector<const Type *> &v,
                                const Type *guess,
                                Args... args) {
    std::vector<const Type *> vv;
    vv.reserve(v.size());
    for (const Type *t : v) vv.push_back(DoType(t, args...));
    return pool->VarType(s, vv, guess);
  }

  virtual const Type *DoRecordType(
      const std::vector<std::pair<std::string, const Type *>> &v,
      const Type *guess,
      Args... args) {
    std::vector<std::pair<std::string, const Type *>> vv;
    vv.reserve(v.size());
    for (const auto &[lab, t] : v) {
      vv.emplace_back(lab, DoType(t, args...));
    }
    return pool->RecordType(vv, guess);
  }

  virtual const Type *DoArrow(const Type *dom, const Type *cod,
                              const Type *guess,
                              Args... args) {
    return pool->Arrow(DoType(dom, args...), DoType(cod, args...),
                       guess);
  }

  virtual const Type *DoSum(
      const std::vector<std::pair<std::string, const Type *>> &v,
      const Type *guess,
      Args... args) {
    std::vector<std::pair<std::string, const Type *>> vv;
    vv.reserve(v.size());
    for (const auto &[lab, t] : v) {
      vv.emplace_back(lab, DoType(t, args...));
    }
    return pool->SumType(vv, guess);
  }

  virtual const Type *DoMu(
      int idx,
      const std::vector<std::pair<std::string, const Type *>> &v,
      const Type *guess,
      Args... args) {
    std::vector<std::pair<std::string, const Type *>> vv;
    vv.reserve(v.size());
    for (const auto &[lab, t] : v) {
      vv.emplace_back(lab, DoType(t, args...));
    }
    return pool->Mu(idx, vv, guess);
  }

  virtual const Type *DoRefType(const Type *body, const Type *guess,
                                Args... args) {
    return pool->RefType(DoType(body, args...), guess);
  }

  virtual const Type *DoIntType(const Type *guess, Args... args) {
    return guess;
  }

  virtual const Type *DoStringType(const Type *guess, Args... args) {
    return guess;
  }

  virtual const Type *DoFloatType(const Type *guess, Args... args) {
    return guess;
  }


  // Expressions

  virtual const Exp *DoString(const std::string &s,
                              const Exp *guess,
                              Args... args) {
    return pool->String(s, guess);
  }

  virtual const Exp *DoFloat(double d, const Exp *guess,
                             Args... args) {
    return pool->Float(d, guess);
  }

  virtual const Exp *DoVar(const std::vector<const Type *> &ts,
                           const std::string &v,
                           const Exp *guess,
                           Args... args) {
    std::vector<const Type *> tts;
    tts.reserve(ts.size());
    for (const Type *t : ts) tts.push_back(DoType(t, args...));
    return pool->Var(tts, v, guess);
  }

  virtual const Exp *DoInt(BigInt i, const Exp *guess,
                           Args... args) {
    return pool->Int(i, guess);
  }

  virtual const Exp *DoRecord(
      const std::vector<std::pair<std::string, const Exp *>> &lv,
      const Exp *guess,
      Args... args) {
    std::vector<std::pair<std::string, const Exp *>> lvv;
    lvv.reserve(lv.size());
    for (const auto &[l, e] : lv) lvv.emplace_back(l, DoExp(e, args...));
    return pool->Record(lvv, guess);
  }

  virtual const Exp *DoProject(const std::string &s, const Exp *e,
                               const Exp *guess,
                               Args... args) {
    return pool->Project(s, DoExp(e, args...), guess);
  }

  virtual const Exp *DoInject(const std::string &s, const Exp *e,
                              const Exp *guess,
                              Args... args) {
    return pool->Inject(s, DoExp(e, args...), guess);
  }

  virtual const Exp *DoRoll(const Type *t, const Exp *e,
                            const Exp *guess,
                            Args... args) {
    return pool->Roll(DoType(t, args...), DoExp(e, args...), guess);
  }

  virtual const Exp *DoJoin(const std::vector<const Exp *> &v,
                            const Exp *guess,
                            Args... args) {
    std::vector<const Exp *> vv;
    vv.reserve(v.size());
    for (const Exp *j : v) vv.push_back(DoExp(j, args...));
    return pool->Join(vv, guess);
  }

  virtual const Exp *DoLet(const std::vector<std::string> &tyvars,
                           const std::string &x,
                           const Exp *rhs,
                           const Exp *body,
                           const Exp *guess,
                           Args... args) {
    return pool->Let(tyvars, x, DoExp(rhs, args...),
                     DoExp(body, args...), guess);
  }

  virtual const Exp *DoIf(const Exp *cond, const Exp *t, const Exp *f,
                          const Exp *guess,
                          Args... args) {
    return pool->If(DoExp(cond, args...),
                    DoExp(t, args...),
                    DoExp(f, args...),
                    guess);
  }

  virtual const Exp *DoApp(const Exp *f, const Exp *arg,
                           const Exp *guess,
                           Args... args) {
    return pool->App(DoExp(f, args...), DoExp(arg, args...), guess);
  }

  virtual const Exp *DoPrimop(Primop po,
                              const std::vector<const Type *> &ts,
                              const std::vector<const Exp *> &es,
                              const Exp *guess,
                              Args... args) {
    std::vector<const Type *> tts;
    tts.reserve(ts.size());
    for (const Type *t : ts) tts.push_back(DoType(t, args...));

    std::vector<const Exp *> ees;
    ees.reserve(ees.size());
    for (const Exp *e : es) ees.push_back(DoExp(e, args...));

    return pool->Primop(po, tts, ees, guess);
  }

  virtual const Exp *DoFn(const std::string &self,
                          const std::string &x,
                          const Exp *body,
                          const Exp *guess,
                          Args... args) {
    return pool->Fn(self, x, DoExp(body, args...), guess);
  }

  virtual const Exp *DoFail(const std::string &msg,
                            const Exp *guess,
                            Args... args) {
    return pool->Fail(msg, guess);
  }

protected:
  AstPool *pool = nullptr;
};

}  // namespace il

#endif
