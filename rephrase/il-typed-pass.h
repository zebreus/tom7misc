
#ifndef _REPHRASE_IL_CONTEXT_PASS_H
#define _REPHRASE_IL_CONTEXT_PASS_H

#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "bignum/big.h"
#include "context.h"
#include "il-util.h"
#include "il.h"
#include "primop.h"
#include "unification.h"

namespace il {
template<typename... Args>
struct TypedPass {
  explicit TypedPass(AstPool *pool) : pool(pool) {}

  // TODO: Should be clearer about when we apply DoType (i.e. before or
  // after exp callbacks that include types as args). I think it
  // has to do with the direction of type checking (a la
  // "bidirectional type checking). It probably doesn't matter unless
  // you're doing something really fancy with this.

  virtual const Program DoProgram(Context G,
                                  const Program &program, Args... args) {
    Context GG = G;
    for (const Global &glob : program.globals) {
      const Type *tt = DoType(G, glob.type, args...);
      GG = GG.InsertSym(glob.sym, {glob.tyvars, tt});
    }

    Program out;
    out.globals.reserve(program.globals.size());
    for (const Global &glob : program.globals) {
      Global gg;
      gg.tyvars = glob.tyvars;
      gg.sym = glob.sym;
      // printf("Do global %s\n", glob.sym.c_str());
      std::tie(gg.exp, gg.type) = DoExp(GG, glob.exp, args...);
      out.globals.push_back(std::move(gg));
    }

    // printf("Do body: %s\n", ExpString(program.body).c_str());
    const auto &[be, bt] = DoExp(GG, program.body, args...);
    out.body = be;
    return out;
  }

  virtual const Type *DoType(Context G, const Type *t, Args... args) {
    switch (t->type) {
    case TypeType::VAR: {
      const auto &[x, tv] = t->Var();
      return DoVarType(G, x, tv, t, args...);
    }
    case TypeType::ARROW: {
      const auto &[dom, cod] = t->Arrow();
      return DoArrow(G, dom, cod, t, args...);
    }
    case TypeType::MU: {
      const auto &[idx, v] = t->Mu();
      return DoMu(G, idx, v, t, args...);
    }
    case TypeType::EXISTS: {
      const auto &[alpha, tt] = t->Exists();
      return DoExists(G, alpha, tt, t, args...);
    }
    case TypeType::FORALL: {
      const auto &[alpha, tt] = t->Forall();
      return DoForall(G, alpha, tt, t, args...);
    }
    case TypeType::SUM: return DoSum(G, t->Sum(), t, args...);
    case TypeType::RECORD: return DoRecordType(G, t->Record(), t, args...);
    case TypeType::EVAR: return DoEVar(G, t->EVar(), t, args...);
    case TypeType::REF: return DoRefType(G, t->Ref(), t, args...);
    case TypeType::STRING: return DoStringType(G, t, args...);
    case TypeType::FLOAT: return DoFloatType(G, t, args...);
    case TypeType::INT: return DoIntType(G, t, args...);
    case TypeType::BOOL: return DoBoolType(G, t, args...);
    case TypeType::OBJ: return DoObjType(G, t, args...);
    case TypeType::LAYOUT: return DoLayoutType(G, t, args...);
    }
    LOG(FATAL) << "Unhandled type type in Pass::DoExp!";
  }

  virtual std::pair<const Exp *, const Type *>
  DoExp(Context G, const Exp *e, Args... args) {
    switch (e->type) {
    case ExpType::STRING: return DoString(G, e->String(), e, args...);
    case ExpType::FLOAT: return DoFloat(G, e->Float(), e, args...);
    case ExpType::NODE: {
      const auto &[attrs, children] = e->Node();
      return DoNode(G, attrs, children, e, args...);
    }
    case ExpType::RECORD: return DoRecord(G, e->Record(), e, args...);
    case ExpType::OBJECT: return DoObject(G, e->Object(), e, args...);
    case ExpType::INT: return DoInt(G, e->Int(), e, args...);
    case ExpType::BOOL: return DoBool(G, e->Bool(), e, args...);
    case ExpType::VAR: {
      const auto &[ts, v] = e->Var();
      return DoVar(G, ts, v, e, args...);
    }
    case ExpType::GLOBAL_SYM: {
      const auto &[ts, sym] = e->GlobalSym();
      return DoGlobalSym(G, ts, sym, e, args...);
    }
    case ExpType::LAYOUT: {
      LOG(FATAL) << "Unimplemented";
    }
    case ExpType::LET: {
      const auto &[tyvars, x, rhs, body] = e->Let();
      return DoLet(G, tyvars, x, rhs, body, e, args...);
    }
    case ExpType::IF: {
      const auto &[cond, t, f] = e->If();
      return DoIf(G, cond, t, f, e, args...);
    }
    case ExpType::APP: {
      const auto &[f, x] = e->App();
      return DoApp(G, f, x, e, args...);
    }
    case ExpType::FN: {
      const auto &[self, x, t, body] = e->Fn();
      return DoFn(G, self, x, t, body, e, args...);
    }
    case ExpType::PROJECT: {
      const auto &[lab, exp] = e->Project();
      return DoProject(G, lab, exp, e, args...);
    }
    case ExpType::INJECT: {
      const auto &[lab, sum_type, exp] = e->Inject();
      return DoInject(G, lab, sum_type, exp, e, args...);
    }
    case ExpType::ROLL: {
      const auto &[type, exp] = e->Roll();
      return DoRoll(G, type, exp, e, args...);
    }
    case ExpType::UNROLL:
      return DoUnroll(G, e->Unroll(), e, args...);
    case ExpType::PRIMOP: {
      const auto &[p, ts, es] = e->Primop();
      return DoPrimop(G, p, ts, es, e, args...);
    }
    case ExpType::FAIL: {
      const auto &[fe, ft] = e->Fail();
      return DoFail(G, fe, ft, e, args...);
    }
    case ExpType::SEQ: {
      const auto &[es, body] = e->Seq();
      return DoSeq(G, es, body, e, args...);
    }
    case ExpType::INTCASE: {
      const auto &[obj, arms, def] = e->IntCase();
      return DoIntCase(G, obj, arms, def, e, args...);
    }
    case ExpType::STRINGCASE: {
      const auto &[obj, arms, def] = e->StringCase();
      return DoStringCase(G, obj, arms, def, e, args...);
    }
    case ExpType::SUMCASE: {
      const auto &[obj, arms, def] = e->SumCase();
      return DoSumCase(G, obj, arms, def, e, args...);
    }
    case ExpType::UNPACK: {
      const auto &[alpha, x, rhs, body] = e->Unpack();
      return DoUnpack(G, alpha, x, rhs, body, e, args...);
    }
    case ExpType::PACK: {
      const auto &[t_hidden, alpha, t_packed, exp] = e->Pack();
      return DoPack(G, t_hidden, alpha, t_packed, exp, e, args...);
    }
    case ExpType::HAS: {
      const auto &[obj, field, oft] = e->Has();
      return DoHas(G, obj, field, oft, e, args...);
    }
    case ExpType::GET: {
      const auto &[obj, field, oft] = e->Get();
      return DoGet(G, obj, field, oft, e, args...);
    }
    case ExpType::WITH: {
      const auto &[obj, field, oft, rhs] = e->With();
      return DoWith(G, obj, field, oft, rhs, e, args...);
    }
    case ExpType::WITHOUT: {
      const auto &[obj, field, oft] = e->Without();
      return DoWithout(G, obj, field, oft, e, args...);
    }

    default:
      LOG(FATAL) << "Unhandled expression type in Pass::DoExp!";
    }
  }


  // Types.

  virtual const Type *DoEVar(Context G,
                             EVar a,
                             const Type *guess,
                             Args... args) {
    // Recurse inside bound evars.
    if (const Type *t = a.GetBound()) {
      // Supposing we did nothing to t, it's unclear whether it's
      // better to return the guess (which keeps the indirection
      // from the evar around) or collapse it away (now we can't
      // share up the tree).
      return DoType(G, t, args...);
    } else {
      return pool->EVar(a, guess);
    }
  }

  virtual const Type *DoVarType(Context G,
                                const std::string &s,
                                const std::vector<const Type *> &v,
                                const Type *guess,
                                Args... args) {
    std::vector<const Type *> vv;
    vv.reserve(v.size());
    for (const Type *t : v) vv.push_back(DoType(G, t, args...));
    return pool->VarType(s, vv, guess);
  }

  virtual const Type *DoRecordType(
      Context G,
      const std::vector<std::pair<std::string, const Type *>> &v,
      const Type *guess,
      Args... args) {
    std::vector<std::pair<std::string, const Type *>> vv;
    vv.reserve(v.size());
    for (const auto &[lab, t] : v) {
      vv.emplace_back(lab, DoType(G, t, args...));
    }
    return pool->RecordType(vv, guess);
  }

  virtual const Type *DoArrow(Context G,
                              const Type *dom, const Type *cod,
                              const Type *guess,
                              Args... args) {
    return pool->Arrow(DoType(G, dom, args...),
                       DoType(G, cod, args...),
                       guess);
  }

  virtual const Type *DoSum(
      Context G,
      const std::vector<std::pair<std::string, const Type *>> &v,
      const Type *guess,
      Args... args) {
    std::vector<std::pair<std::string, const Type *>> vv;
    vv.reserve(v.size());
    for (const auto &[lab, t] : v) {
      vv.emplace_back(lab, DoType(G, t, args...));
    }
    return pool->SumType(vv, guess);
  }

  virtual const Type *DoMu(
      Context G,
      int idx,
      const std::vector<std::pair<std::string, const Type *>> &v,
      const Type *guess,
      Args... args) {
    // All variables are bound in all arms.
    Context GG = G;
    for (const auto &[alpha, t_] : v) {
      GG = GG.InsertType(alpha);
    }

    std::vector<std::pair<std::string, const Type *>> vv;
    vv.reserve(v.size());
    for (const auto &[alpha, t] : v) {
      vv.emplace_back(alpha, DoType(GG, t, args...));
    }
    return pool->Mu(idx, vv, guess);
  }

  virtual const Type *DoExists(
      Context G,
      const std::string &alpha,
      const Type *body,
      const Type *guess,
      Args... args) {
    return pool->Exists(alpha,
                        DoType(G.InsertType(alpha), body, args...),
                        guess);
  }

  virtual const Type *DoForall(
      Context G,
      const std::string &alpha,
      const Type *body,
      const Type *guess,
      Args... args) {
    return pool->Forall(alpha,
                        DoType(G.InsertType(alpha), body, args...),
                        guess);
  }

  virtual const Type *DoRefType(Context G,
                                const Type *body, const Type *guess,
                                Args... args) {
    return pool->RefType(DoType(G, body, args...), guess);
  }

  virtual const Type *DoIntType(Context G,
                                const Type *guess, Args... args) {
    return guess;
  }

  virtual const Type *DoStringType(Context G,
                                   const Type *guess, Args... args) {
    return guess;
  }

  virtual const Type *DoFloatType(Context G,
                                  const Type *guess, Args... args) {
    return guess;
  }

  virtual const Type *DoBoolType(Context G,
                                 const Type *guess, Args... args) {
    return guess;
  }

  virtual const Type *DoObjType(Context G,
                                const Type *guess, Args... args) {
    return guess;
  }

  virtual const Type *DoLayoutType(Context G,
                                   const Type *guess, Args... args) {
    return guess;
  }


  // Expressions.
  // Unlike il-pass, here we return the expression and its synthesized
  // type.

  virtual std::pair<const Exp *, const Type *>
  DoString(Context G,
           const std::string &s,
           const Exp *guess,
           Args... args) {
    return {pool->String(s, guess), pool->StringType()};
  }

  virtual std::pair<const Exp *, const Type *>
  DoFloat(Context G,
          double d, const Exp *guess,
          Args... args) {
    return {pool->Float(d, guess), pool->FloatType()};
  }

  virtual std::pair<const Exp *, const Type *>
  DoVar(Context G,
        const std::vector<const Type *> &ts,
        const std::string &v,
        const Exp *guess,
        Args... args) {
    const PolyType *pt = G.Find(v);
    CHECK(pt != nullptr) << "Unbound var: " << v;

    std::vector<const Type *> tts;
    tts.reserve(ts.size());
    for (const Type *t : ts) tts.push_back(DoType(G, t, args...));

    CHECK(ts.size() == pt->first.size()) << "Type error: Wrong number "
      "of type arguments to polyvar " << v;

    // Instantiate the var's polytype at the provided types.
    const Type *t = pt->second;
    for (int i = 0; i < (int)pt->first.size(); i++) {
      t = pool->SubstType(ts[i], pt->first[i], t);
    }

    return {pool->Var(tts, v, guess), t};
  }

  virtual std::pair<const Exp *, const Type *>
  DoGlobalSym(Context G,
              const std::vector<const Type *> &ts,
              const std::string &sym,
              const Exp *guess,
              Args... args) {

    const PolyType *pt = G.FindSym(sym);
    CHECK(pt != nullptr) << "Unbound global symbol: " << sym
                         << "\nContext:\n" << G.ToString();

    std::vector<const Type *> tts;
    tts.reserve(ts.size());
    for (const Type *t : ts) tts.push_back(DoType(G, t, args...));

    CHECK(ts.size() == pt->first.size()) << "Type error: Wrong number "
      "of type arguments to polymorphic symbol " << sym;

    // Instantiate the symbol's polytype at the provided types.
    const Type *t = pt->second;
    for (int i = 0; i < (int)pt->first.size(); i++) {
      t = pool->SubstType(ts[i], pt->first[i], t);
    }

    return {pool->GlobalSym(tts, sym, guess), t};
  }

  virtual std::pair<const Exp *, const Type *>
  DoInt(Context G,
        BigInt i, const Exp *guess,
        Args... args) {
    return {pool->Int(i, guess), pool->IntType()};
  }

  virtual std::pair<const Exp *, const Type *>
  DoBool(Context G,
         bool b, const Exp *guess,
         Args... args) {
    return {pool->Bool(b, guess), pool->BoolType()};
  }

  virtual std::pair<const Exp *, const Type *>
  DoRecord(
      Context G,
      const std::vector<std::pair<std::string, const Exp *>> &lv,
      const Exp *guess,
      Args... args) {
    std::vector<std::pair<std::string, const Exp *>> lvv;
    lvv.reserve(lv.size());
    std::vector<std::pair<std::string, const Type *>> ts;
    for (const auto &[l, e] : lv) {
      const auto &[ee, tt] = DoExp(G, e, args...);
      lvv.emplace_back(l, ee);
      ts.emplace_back(l, tt);
    }
    return {pool->Record(lvv, guess), pool->RecordType(ts)};
  }

  virtual std::pair<const Exp *, const Type *>
  DoObject(
      Context G,
      const std::vector<std::tuple<std::string, ObjFieldType, const Exp *>> &lv,
      const Exp *guess,
      Args... args) {
    std::vector<std::tuple<std::string, ObjFieldType, const Exp *>> lvv;
    lvv.reserve(lv.size());
    for (const auto &[l, oft, e] : lv) {
      const auto &[ee, tt] = DoExp(G, e, args...);
      lvv.emplace_back(l, oft, ee);
    }
    return {pool->Object(lvv, guess), pool->ObjType()};
  }

  virtual std::pair<const Exp *, const Type *>
  DoProject(Context G,
            const std::string &s, const Exp *e,
            const Exp *guess,
            Args... args) {
    const auto &[ee, tt] = DoExp(G, e, args...);
    CHECK(tt->type == TypeType::RECORD);
    for (const auto &[l, t] : tt->Record()) {
      if (l == s) {
        return {pool->Project(s, ee, guess), t};
      }
    }
    CHECK(false) << "Type error: Label " << s << " not found in " <<
      TypeString(tt);
  }

  virtual std::pair<const Exp *, const Type *>
  DoInject(Context G,
           const std::string &s,
           const Type *sum_type,
           const Exp *e,
           const Exp *guess,
           Args... args) {
    const auto &[ee, tt] = DoExp(G, e, args...);
    return {pool->Inject(s, sum_type, ee, guess),
            DoType(G, sum_type, args...)};
  }

  virtual std::pair<const Exp *, const Type *>
  DoRoll(Context G,
         const Type *t,
         const Exp *e,
         const Exp *guess,
         Args... args) {
    const Type *rt = DoType(G, t, args...);
    CHECK(rt->type == TypeType::MU);
    const auto &[ee, tt] = DoExp(G, e, args...);
    // TODO: Could check that unroll(rt) = tt, but need
    // type equality mod alpha equivalence.
    return {pool->Roll(rt, ee, guess), rt};
  }

  virtual std::pair<const Exp *, const Type *>
  DoUnroll(Context G,
           const Exp *e,
           const Exp *guess,
           Args... args) {
    const auto &[ee, tt] = DoExp(G, e, args...);
    return {pool->Unroll(ee, guess), pool->UnrollType(tt)};
  }

  virtual std::pair<const Exp *, const Type *>
  DoNode(Context G,
         const Exp *attrs,
         const std::vector<const Exp *> &v,
         const Exp *guess,
         Args... args) {
    std::vector<const Exp *> vv;
    vv.reserve(v.size());
    for (const Exp *j : v) {
      const auto &[jj, tt] = DoExp(G, j, args...);
      vv.push_back(jj);
    }
    const auto &[aa, tt_] = DoExp(G, attrs, args...);
    return {pool->Node(aa, vv, guess), pool->LayoutType()};
  }

  virtual std::pair<const Exp *, const Type *>
  DoLet(Context G,
        const std::vector<std::string> &tyvars,
        const std::string &x,
        const Exp *rhs,
        const Exp *body,
        const Exp *guess,
        Args... args) {
    Context GG = G;
    for (const std::string &alpha : tyvars) GG = GG.InsertType(alpha);
    const auto &[ee, tt] = DoExp(GG, rhs, args...);
    Context GGG = G.Insert(x, {tyvars, tt});
    const auto &[ebody, tbody] = DoExp(GGG, body, args...);
    return {pool->Let(tyvars, x, ee, ebody, guess), tbody};
  }

  virtual std::pair<const Exp *, const Type *>
  DoIf(Context G,
       const Exp *cond, const Exp *t, const Exp *f,
       const Exp *guess,
       Args... args) {
    const auto &[ce, ctt] = DoExp(G, cond, args...);
    CHECK(ctt->type == TypeType::BOOL);
    const auto &[te, ttt] = DoExp(G, t, args...);
    const auto &[fe, ftt] = DoExp(G, f, args...);
    return {pool->If(ce, te, fe), ttt};
  }

  virtual std::pair<const Exp *, const Type *>
  DoApp(Context G,
        const Exp *f, const Exp *arg,
        const Exp *guess,
        Args... args) {
    const auto &[ff, ft] = DoExp(G, f, args...);
    const auto &[aa, at] = DoExp(G, arg, args...);
    CHECK(ft->type == TypeType::ARROW) << "Bug: Application of non-function "
      "type " << TypeString(ft) << "\nWhich was this expression:\n" <<
      ExpString(ff) << "\nWhich was applied to this arg:\n" <<
      ExpString(aa);

    const auto &[dom, cod] = ft->Arrow();
    return {pool->App(ff, aa, guess), cod};
  }

  virtual std::pair<const Exp *, const Type *>
  DoPrimop(Context G,
           Primop po,
           const std::vector<const Type *> &ts,
           const std::vector<const Exp *> &es,
           const Exp *guess,
           Args... args) {

    const auto &[tyvars, po_type] = PrimopType(pool, po);
    CHECK(po_type->type == TypeType::ARROW) << "Bug: Primop doesn't have "
      "function type?\nType: " << TypeString(po_type);

    const auto &[dom, cod] = po_type->Arrow();

    std::vector<const Type *> tts;
    tts.reserve(ts.size());
    for (const Type *t : ts) tts.push_back(DoType(G, t, args...));

    std::vector<const Exp *> ees;
    ees.reserve(es.size());
    for (const Exp *e : es) {
      const auto &[ee, tt] = DoExp(G, e, args...);
      ees.push_back(ee);
    }

    CHECK(tts.size() == tyvars.size()) << "Type error: Primop " <<
      PrimopString(po) << " instantiated with " << tts.size() <<
      " types, but it takes " << tyvars.size();

    const Type *ret_type = cod;
    for (int i = 0; i < (int)tyvars.size(); i++) {
      ret_type = pool->SubstType(tts[i], tyvars[i], ret_type);
    }

    return {pool->Primop(po, tts, ees, guess), ret_type};
  }

  virtual std::pair<const Exp *, const Type *>
  DoFn(Context G,
       const std::string &self,
       const std::string &x,
       const Type *arrow_type,
       const Exp *body,
       const Exp *guess,
       Args... args) {
    const Type *at = DoType(G, arrow_type, args...);

    CHECK(at->type == TypeType::ARROW) << "Bug: Function is annotated "
      "with non-arrow type?"
      "\nType: " << TypeString(arrow_type) <<
      "\nBecame: " << TypeString(at) <<
      "\nFunction  expression:\n" <<
      ExpString(guess);

    const auto &[dom, cod] = arrow_type->Arrow();
    Context GG = G.Insert(self, {{}, at}).Insert(x, {{}, dom});
    const auto &[be, bt] = DoExp(GG, body, args...);
    return {pool->Fn(self, x, at, be, guess), at};
  }

  virtual std::pair<const Exp *, const Type *>
  DoFail(Context G,
         const Exp *msg,
         const Type *t,
         const Exp *guess,
         Args... args) {
    const Type *tt = DoType(G, t, args...);
    const auto &[me, mt] = DoExp(G, msg, args...);
    return {pool->Fail(me, tt, guess), tt};
  }

  virtual std::pair<const Exp *, const Type *>
  DoSeq(Context G,
        const std::vector<const Exp *> &es,
        const Exp *body,
        const Exp *guess,
        Args... args) {
    std::vector<const Exp *> ees;
    ees.reserve(es.size());
    for (const Exp *e : es) {
      const auto &[ee, tt] = DoExp(G, e, args...);
      ees.push_back(ee);
    }
    const auto &[be, bt] = DoExp(G, body, args...);
    return {pool->Seq(ees, be, guess), bt};
  }

  virtual std::pair<const Exp *, const Type *>
  DoIntCase(
      Context G,
      const Exp *obj,
      const std::vector<std::pair<BigInt, const Exp *>> &arms,
      const Exp *def,
      const Exp *guess,
      Args... args) {
    std::vector<std::pair<BigInt, const Exp *>> narms;
    narms.reserve(arms.size());
    for (const auto &[bi, arm] : arms) {
      const auto &[ee, tt] = DoExp(G, arm, args...);
      narms.emplace_back(bi, ee);
    }
    const auto &[oe, ot] = DoExp(G, obj, args...);
    const auto &[de, dt] = DoExp(G, def, args...);
    return {pool->IntCase(oe, std::move(narms), de, guess), dt};
  }

  virtual std::pair<const Exp *, const Type *>
  DoStringCase(
      Context G,
      const Exp *obj,
      const std::vector<std::pair<std::string, const Exp *>> &arms,
      const Exp *def,
      const Exp *guess,
      Args... args) {
    std::vector<std::pair<std::string, const Exp *>> narms;
    narms.reserve(arms.size());
    for (const auto &[s, arm] : arms) {
      const auto &[ee, tt] = DoExp(G, arm, args...);
      narms.emplace_back(s, ee);
    }
    const auto &[oe, ot] = DoExp(G, obj, args...);
    const auto &[de, dt] = DoExp(G, def, args...);
    return {pool->StringCase(oe, std::move(narms), de, guess), dt};
  }

  virtual std::pair<const Exp *, const Type *>
  DoSumCase(
      Context G,
      const Exp *obj,
      const std::vector<
          std::tuple<std::string, std::string, const Exp *>> &arms,
      const Exp *def,
      const Exp *guess,
      Args... args) {

    const auto &[oe, ot] = DoExp(G, obj, args...);
    std::unordered_map<std::string, const Type *> lab_types;
    for (const auto [lab, t] : ot->Sum()) {
      // Types have already been translated!
      // lab_types[lab] = DoType(G, t, args...);
      lab_types[lab] = t;
    }

    std::vector<
      std::tuple<std::string, std::string, const Exp *>> narms;
    narms.reserve(arms.size());
    for (const auto &[lab, x, arm] : arms) {
      const auto it = lab_types.find(lab);
      CHECK(it != lab_types.end()) << "Type error: Label " << lab <<
        " in sumcase not found in object type: " << TypeString(ot).c_str();
      Context GG = G.Insert(x, {{}, it->second});
      const auto &[ae, at] = DoExp(GG, arm, args...);
      narms.emplace_back(lab, x, ae);
    }

    // Can use any arm for the type; we choose the default.
    const auto &[de, dt] = DoExp(G, def, args...);
    return {pool->SumCase(oe, std::move(narms), de, guess), dt};
  }

  virtual std::pair<const Exp *, const Type *> DoUnpack(
      Context G,
      const std::string &alpha, const std::string &x, const Exp *rhs,
      const Exp *body, const Exp *guess) {
    // unpack α,x = rhs
    // in body
    const auto &[rrhs, rhstt] = DoExp(G, rhs);
    Context GG =
      G.InsertType(alpha).Insert(x, {{}, pool->VarType(alpha, {})});
    const auto &[bb, bt] = DoExp(GG, body);
    CHECK(!ILUtil::FreeTypeVars(bt).contains(alpha)) << "∃-bound type "
      "variable " << alpha << " is free in the type of the unpack "
      "expression; this is not allowed. Unpack's type: " <<
      TypeString(bt).c_str();

    return {pool->Unpack(alpha, x, rrhs, bb, guess), bt};
  }

  virtual std::pair<const Exp *, const Type *>
  DoPack(Context G, const Type *t_hidden, const std::string &alpha,
         const Type *t_packed, const Exp *body,
         const Exp *guess) {

    const Type *tth = DoType(G, t_hidden);
    Context GG = G.InsertType(alpha);
    const Type *ttp = DoType(GG, t_packed);
    const auto &[bb, bt] = DoExp(GG, body);
    return {pool->Pack(tth, alpha, ttp, bb, guess), ttp};
  }

  virtual std::pair<const Exp *, const Type *>
  DoHas(Context G,
        const Exp *obj, const std::string &field,
        ObjFieldType oft,
        const Exp *guess, Args... args) {
    const auto &[oo, tt] = DoExp(G, obj, args...);
    return {
      pool->Has(oo, field, oft, guess),
      pool->BoolType()
    };
  }

  virtual std::pair<const Exp *, const Type *>
  DoGet(Context G,
        const Exp *obj, const std::string &field,
        ObjFieldType oft,
        const Exp *guess, Args... args) {
    const auto &[oo, tt] = DoExp(G, obj, args...);
    return {
      pool->Get(oo, field, oft, guess),
      ILUtil::ObjFieldTypeType(pool, oft)
    };
  }

  virtual std::pair<const Exp *, const Type *>
  DoWith(Context G,
         const Exp *obj, const std::string &field,
         ObjFieldType oft, const Exp *rhs,
         const Exp *guess, Args... args) {
    const auto &[oo, ott] = DoExp(G, obj, args...);
    const auto &[rr, rtt] = DoExp(G, rhs, args...);
    return {
      pool->With(oo, field, oft, rr, guess),
      pool->ObjType(),
    };
  }

  virtual std::pair<const Exp *, const Type *>
  DoWithout(Context G,
            const Exp *obj, const std::string &field,
            ObjFieldType oft,
            const Exp *guess, Args... args) {
    const auto &[oo, tt] = DoExp(G, obj, args...);
    return {
      pool->Without(oo, field, oft, guess),
      pool->ObjType(),
    };
  }

  virtual std::pair<const Exp *, const Type *>
  DoTypeFn(Context G,
           const std::string &alpha, const Exp *exp,
           const Exp *guess, Args... args) {
    const auto &[ee, t] = DoExp(G.InsertType(alpha), exp, args...);
    return {
      pool->TypeFn(alpha, ee, guess),
      pool->Forall(alpha, t),
    };
  }

  virtual std::pair<const Exp *, const Type *>
  DoTypeApp(Context G,
            const Exp *exp, const Type *t,
            const Exp *guess, Args... args) {
    const auto &[ee, tforall] = DoExp(G, exp, args...);
    CHECK(tforall->type == TypeType::FORALL);
    const auto &[alpha, tbody] = tforall->Forall();
    const Type *tt = DoType(G, t, args...);
    return {
      pool->TypeApp(ee, tt, guess),
      pool->SubstType(tt, alpha, tbody),
    };
  }


protected:
  AstPool *pool = nullptr;
};

}  // namespace il

#endif
