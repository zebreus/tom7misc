
#ifndef _REPHRASE_IL_PASS_H
#define _REPHRASE_IL_PASS_H

#include <cstdint>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "bignum/big.h"
#include "il.h"

#include "base/logging.h"
#include "primop.h"
#include "unification.h"

// This is a recursive identity function over the IL AST.
// The idea is that you can override just the constructs
// that you're interested in.

namespace il {

template<typename... Args>
struct Pass {
  explicit Pass(AstPool *pool) : pool(pool) {}

  virtual Program DoProgram(const Program &program, Args... args) {
    Program out;
    out.globals.reserve(program.globals.size());
    for (const Global &glob : program.globals) {
      Global gg;
      gg.tyvars = glob.tyvars;
      gg.sym = glob.sym;
      gg.type = DoType(glob.type, args...);
      gg.exp = DoExp(glob.exp, args...);
      out.globals.push_back(std::move(gg));
    }
    out.body = DoExp(program.body, args...);
    return out;
  }

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
    case TypeType::EXISTS: {
      const auto &[alpha, body] = t->Exists();
      return DoExists(alpha, body, t, args...);
    }
    case TypeType::FORALL: {
      const auto &[alpha, body] = t->Forall();
      return DoForall(alpha, body, t, args...);
    }

    case TypeType::SUM: return DoSum(t->Sum(), t, args...);
    case TypeType::RECORD: return DoRecordType(t->Record(), t, args...);
    case TypeType::EVAR: return DoEVar(t->EVar(), t, args...);
    case TypeType::REF: return DoRefType(t->Ref(), t, args...);
    case TypeType::VEC: return DoVecType(t->Vec(), t, args...);
    case TypeType::STRING: return DoStringType(t, args...);
    case TypeType::FLOAT: return DoFloatType(t, args...);
    case TypeType::INT: return DoIntType(t, args...);
    case TypeType::WORD: return DoWordType(t, args...);
    case TypeType::BOOL: return DoBoolType(t, args...);
    case TypeType::OBJ: return DoObjType(t, args...);
    case TypeType::LAYOUT: return DoLayoutType(t, args...);
    }
    LOG(FATAL) << "Unhandled type type in Pass::DoExp!";
    return nullptr;
  }

  virtual const Exp *DoExp(const Exp *e, Args... args) {
    switch (e->type) {
    case ExpType::STRING: return DoString(e->String(), e, args...);
    case ExpType::FLOAT: return DoFloat(e->Float(), e, args...);
    case ExpType::NODE: {
      const auto &[attrs, v] = e->Node();
      return DoNode(attrs, v, e, args...);
    }
    case ExpType::RECORD: return DoRecord(e->Record(), e, args...);
    case ExpType::OBJECT: return DoObject(e->Object(), e, args...);
    case ExpType::INT: return DoInt(e->Int(), e, args...);
    case ExpType::WORD: return DoWord(e->Word(), e, args...);
    case ExpType::BOOL: return DoBool(e->Bool(), e, args...);
    case ExpType::VAR: {
      const auto &[ts, v] = e->Var();
      return DoVar(ts, v, e, args...);
    }
    case ExpType::GLOBAL_SYM: {
      const auto &[ts, sym] = e->GlobalSym();
      return DoGlobalSym(ts, sym, e, args...);
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
      const auto &[self, x, arrow_type, body] = e->Fn();
      return DoFn(self, x, arrow_type, body, e, args...);
    }
    case ExpType::PROJECT: {
      const auto &[lab, t, exp] = e->Project();
      return DoProject(lab, t, exp, e, args...);
    }
    case ExpType::INJECT: {
      const auto &[lab, t, exp] = e->Inject();
      return DoInject(lab, t, exp, e, args...);
    }
    case ExpType::ROLL: {
      const auto &[type, exp] = e->Roll();
      return DoRoll(type, exp, e, args...);
    }
    case ExpType::UNROLL: {
      const auto &[exp, type] = e->Unroll();
      return DoUnroll(exp, type, e, args...);
    }
    case ExpType::PRIMOP: {
      const auto &[p, ts, es] = e->Primop();
      return DoPrimop(p, ts, es, e, args...);
    }
    case ExpType::FAIL: {
      const auto &[msg, t] = e->Fail();
      return DoFail(msg, t, e, args...);
    }
    case ExpType::SEQ: {
      const auto &[es, body] = e->Seq();
      return DoSeq(es, body, e, args...);
    }
    case ExpType::INTCASE: {
      const auto &[obj, arms, def] = e->IntCase();
      return DoIntCase(obj, arms, def, e, args...);
    }
    case ExpType::WORDCASE: {
      const auto &[obj, arms, def] = e->WordCase();
      return DoWordCase(obj, arms, def, e, args...);
    }
    case ExpType::STRINGCASE: {
      const auto &[obj, arms, def] = e->StringCase();
      return DoStringCase(obj, arms, def, e, args...);
    }
    case ExpType::SUMCASE: {
      const auto &[obj, arms, def] = e->SumCase();
      return DoSumCase(obj, arms, def, e, args...);
    }
    case ExpType::UNPACK: {
      const auto &[alpha, x, rhs, body] = e->Unpack();
      return DoUnpack(alpha, x, rhs, body, e, args...);
    }
    case ExpType::PACK: {
      const auto &[t_hidden, alpha, t_packed, exp] = e->Pack();
      return DoPack(t_hidden, alpha, t_packed, exp, e, args...);
    }
    case ExpType::HAS: {
      const auto &[obj, field, oft] = e->Has();
      return DoHas(obj, field, oft, e, args...);
    }
    case ExpType::GET: {
      const auto &[obj, field, oft] = e->Get();
      return DoGet(obj, field, oft, e, args...);
    }
    case ExpType::WITH: {
      const auto &[obj, field, oft, rhs] = e->With();
      return DoWith(obj, field, oft, rhs, e, args...);
    }
    case ExpType::WITHOUT: {
      const auto &[obj, field, oft] = e->Without();
      return DoWithout(obj, field, oft, e, args...);
    }

    case ExpType::TYPEFN: {
      const auto &[alpha, exp] = e->TypeFn();
      return DoTypeFn(alpha, exp, e, args...);
    }

    case ExpType::TYPEAPP: {
      const auto &[exp, t] = e->TypeApp();
      return DoTypeApp(exp, t, e, args...);
    }

    }
    LOG(FATAL) << "Unhandled expression type in Pass::DoExp!";
    return nullptr;
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
    for (const auto &[alpha, t] : v) {
      vv.emplace_back(alpha, DoType(t, args...));
    }
    return pool->Mu(idx, vv, guess);
  }

  virtual const Type *DoExists(
      const std::string &alpha,
      const Type *body,
      const Type *guess,
      Args... args) {
    return pool->Exists(alpha, DoType(body, args...), guess);
  }

  virtual const Type *DoForall(
      const std::string &alpha,
      const Type *body,
      const Type *guess,
      Args... args) {
    return pool->Forall(alpha, DoType(body, args...), guess);
  }

  virtual const Type *DoRefType(const Type *body, const Type *guess,
                                Args... args) {
    return pool->RefType(DoType(body, args...), guess);
  }

  virtual const Type *DoVecType(const Type *body, const Type *guess,
                                Args... args) {
    return pool->VecType(DoType(body, args...), guess);
  }

  virtual const Type *DoIntType(const Type *guess, Args... args) {
    return guess;
  }

  virtual const Type *DoWordType(const Type *guess, Args... args) {
    return guess;
  }

  virtual const Type *DoStringType(const Type *guess, Args... args) {
    return guess;
  }

  virtual const Type *DoFloatType(const Type *guess, Args... args) {
    return guess;
  }

  virtual const Type *DoBoolType(const Type *guess, Args... args) {
    return guess;
  }

  virtual const Type *DoObjType(const Type *guess, Args... args) {
    return guess;
  }

  virtual const Type *DoLayoutType(const Type *guess, Args... args) {
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

  virtual const Exp *DoInt(const BigInt &i, const Exp *guess,
                           Args... args) {
    return pool->Int(i, guess);
  }

  virtual const Exp *DoWord(const uint64_t w, const Exp *guess,
                            Args... args) {
    return pool->Word(w, guess);
  }

  virtual const Exp *DoBool(bool b, const Exp *guess,
                            Args... args) {
    return pool->Bool(b, guess);
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

  virtual const Exp *DoGlobalSym(const std::vector<const Type *> &ts,
                                 const std::string &sym,
                                 const Exp *guess,
                                 Args... args) {
    std::vector<const Type *> tts;
    tts.reserve(ts.size());
    for (const Type *t : ts) tts.push_back(DoType(t, args...));
    return pool->GlobalSym(tts, sym, guess);
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

  virtual const Exp *DoObject(
      const std::vector<std::tuple<std::string, ObjFieldType, const Exp *>> &lv,
      const Exp *guess,
      Args... args) {
    std::vector<std::tuple<std::string, ObjFieldType, const Exp *>> lvv;
    lvv.reserve(lv.size());
    for (const auto &[l, oft, e] : lv) lvv.emplace_back(l, oft, DoExp(e, args...));
    return pool->Object(lvv, guess);
  }

  virtual const Exp *DoProject(const std::string &s,
                               const Type *record_type,
                               const Exp *e,
                               const Exp *guess,
                               Args... args) {
    return pool->Project(s,
                         DoType(record_type, args...),
                         DoExp(e, args...), guess);
  }

  virtual const Exp *DoInject(const std::string &s,
                              const Type *t,
                              const Exp *e,
                              const Exp *guess,
                              Args... args) {
    return pool->Inject(s, DoType(t, args...), DoExp(e, args...), guess);
  }

  virtual const Exp *DoRoll(const Type *t, const Exp *e,
                            const Exp *guess,
                            Args... args) {
    return pool->Roll(DoType(t, args...), DoExp(e, args...), guess);
  }

  virtual const Exp *DoUnroll(const Exp *e,
                              const Type *mu_type,
                              const Exp *guess,
                              Args... args) {
    return pool->Unroll(DoExp(e, args...), DoType(mu_type, args...), guess);
  }

  virtual const Exp *DoNode(const Exp *attrs,
                            const std::vector<const Exp *> &v,
                            const Exp *guess,
                            Args... args) {
    std::vector<const Exp *> vv;
    vv.reserve(v.size());
    for (const Exp *j : v) vv.push_back(DoExp(j, args...));
    return pool->Node(DoExp(attrs, args...), vv, guess);
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
    ees.reserve(es.size());
    for (const Exp *e : es) ees.push_back(DoExp(e, args...));

    return pool->Primop(po, tts, ees, guess);
  }

  virtual const Exp *DoFn(const std::string &self,
                          const std::string &x,
                          const Type *arrow_type,
                          const Exp *body,
                          const Exp *guess,
                          Args... args) {
    return pool->Fn(self, x, DoType(arrow_type, args...),
                    DoExp(body, args...), guess);
  }

  virtual const Exp *DoFail(const Exp *msg,
                            const Type *t,
                            const Exp *guess,
                            Args... args) {
    return pool->Fail(DoExp(msg, args...), DoType(t, args...), guess);
  }

  virtual const Exp *DoSeq(const std::vector<const Exp *> &es,
                           const Exp *body,
                           const Exp *guess,
                           Args... args) {
    std::vector<const Exp *> ees;
    ees.reserve(es.size());
    for (const Exp *e : es) ees.push_back(DoExp(e, args...));
    return pool->Seq(ees, DoExp(body, args...), guess);
  }

  virtual const Exp *DoIntCase(
      const Exp *obj,
      const std::vector<std::pair<BigInt, const Exp *>> &arms,
      const Exp *def,
      const Exp *guess,
      Args... args) {
    std::vector<std::pair<BigInt, const Exp *>> narms;
    narms.reserve(arms.size());
    for (const auto &[bi, arm] : arms)
      narms.emplace_back(bi, DoExp(arm, args...));
    return pool->IntCase(DoExp(obj, args...), std::move(narms),
                         DoExp(def, args...), guess);
  }

  virtual const Exp *DoWordCase(
      const Exp *obj,
      const std::vector<std::pair<uint64_t, const Exp *>> &arms,
      const Exp *def,
      const Exp *guess,
      Args... args) {
    std::vector<std::pair<uint64_t, const Exp *>> narms;
    narms.reserve(arms.size());
    for (const auto &[w, arm] : arms)
      narms.emplace_back(w, DoExp(arm, args...));
    return pool->WordCase(DoExp(obj, args...), std::move(narms),
                          DoExp(def, args...), guess);
  }

  virtual const Exp *DoStringCase(
      const Exp *obj,
      const std::vector<std::pair<std::string, const Exp *>> &arms,
      const Exp *def,
      const Exp *guess,
      Args... args) {
    std::vector<std::pair<std::string, const Exp *>> narms;
    narms.reserve(arms.size());
    for (const auto &[s, arm] : arms)
      narms.emplace_back(s, DoExp(arm, args...));
    return pool->StringCase(DoExp(obj, args...), std::move(narms),
                            DoExp(def, args...), guess);
  }

  virtual const Exp *DoSumCase(
      const Exp *obj,
      const std::vector<
          std::tuple<std::string, std::string, const Exp *>> &arms,
      const Exp *def,
      const Exp *guess,
      Args... args) {
    std::vector<
      std::tuple<std::string, std::string, const Exp *>> narms;
    narms.reserve(arms.size());
    for (const auto &[s, x, arm] : arms)
      narms.emplace_back(s, x, DoExp(arm, args...));
    return pool->SumCase(DoExp(obj, args...), std::move(narms),
                         DoExp(def, args...), guess);
  }

  virtual const Exp *DoUnpack(
      const std::string &alpha, const std::string &x, const Exp *rhs,
      const Exp *body, const Exp *guess, Args... args) {
    return pool->Unpack(alpha, x, DoExp(rhs, args...), DoExp(body, args...),
                        guess);
  }

  virtual const Exp *DoPack(const Type *t_hidden, const std::string &alpha,
                            const Type *t_packed, const Exp *body,
                            const Exp *guess, Args... args) {
    return pool->Pack(DoType(t_hidden, args...),
                      alpha,
                      DoType(t_packed, args...),
                      DoExp(body, args...),
                      guess);
  }

  virtual const Exp *DoHas(const Exp *obj, const std::string &field,
                           ObjFieldType oft,
                           const Exp *guess, Args... args) {
    return pool->Has(DoExp(obj, args...), field, oft, guess);
  }

  virtual const Exp *DoGet(const Exp *obj, const std::string &field,
                           ObjFieldType oft,
                           const Exp *guess, Args... args) {
    return pool->Get(DoExp(obj, args...), field, oft, guess);
  }

  virtual const Exp *DoWith(const Exp *obj, const std::string &field,
                            ObjFieldType oft, const Exp *rhs,
                            const Exp *guess, Args... args) {
    return pool->With(DoExp(obj, args...), field, oft, DoExp(rhs, args...),
                      guess);
  }

  virtual const Exp *DoWithout(const Exp *obj, const std::string &field,
                               ObjFieldType oft,
                               const Exp *guess, Args... args) {
    return pool->Without(DoExp(obj, args...), field, oft, guess);
  }

  virtual const Exp *DoTypeFn(const std::string &alpha, const Exp *exp,
                              const Exp *guess, Args... args) {
    return pool->TypeFn(alpha, DoExp(exp, args...), guess);
  }

  virtual const Exp *DoTypeApp(const Exp *exp, const Type *t,
                               const Exp *guess, Args... args) {
    return pool->TypeApp(DoExp(exp, args...), DoType(t, args...), guess);
  }


protected:
  AstPool *pool = nullptr;
};

}  // namespace il

#endif
