
#ifndef _REPHRASE_EL_PASS_H
#define _REPHRASE_EL_PASS_H

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "bignum/big.h"
#include "el.h"

#include "base/logging.h"

namespace el {

template<typename... Args>
struct Pass {
  explicit Pass(AstPool *pool) : pool(pool) {}

  virtual const Type *DoType(const Type *t, Args... args) {
    switch (t->type) {
    case TypeType::VAR:
      return DoVarType(t->var, t->children, args...);
    case TypeType::ARROW:
      return DoArrow(t->a, t->b, args...);
    case TypeType::PRODUCT:
      return DoProduct(t->children, args...);
    case TypeType::RECORD:
      return DoRecordType(t->str_children, args...);
    default:
      LOG(FATAL) << "Unhandled type in el::Pass::DoType!";
    }
  }

  virtual const Exp *DoExp(const Exp *e, Args... args) {
    switch (e->type) {
    case ExpType::STRING: return DoString(e->str, e->pos, args...);
    case ExpType::JOIN: return DoJoin(e->children, e->pos, args...);
    case ExpType::TUPLE: return DoTuple(e->children, e->pos, args...);
    case ExpType::RECORD: return DoRecord(e->str_children, e->pos, args...);
    case ExpType::OBJECT:
      return DoObject(e->str, e->str_children, e->pos, args...);
    case ExpType::WITH:
      return DoWith(e->a, e->str, e->str2, e->b, e->pos, args...);
    case ExpType::WITHOUT:
      return DoWithout(e->a, e->str, e->str2, e->pos, args...);
    case ExpType::INT: return DoInt(e->integer, e->pos, args...);
    case ExpType::BOOL: return DoBool(e->boolean, e->pos, args...);
    case ExpType::FLOAT: return DoFloat(e->d, e->pos, args...);
    case ExpType::VAR: return DoVar(e->str, e->pos, args...);
    case ExpType::LAYOUT: return DoLayoutExp(e->layout, e->pos, args...);
    case ExpType::LET: return DoLet(e->decs, e->a, e->pos, args...);
    case ExpType::IF: return DoIf(e->a, e->b, e->c, e->pos, args...);
    case ExpType::APP: return DoApp(e->a, e->b, e->pos, args...);
    case ExpType::ANN: return DoAnn(e->a, e->t, e->pos, args...);
    case ExpType::CASE: return DoCase(e->a, e->clauses, e->pos, args...);
    case ExpType::FN: return DoFn(e->str, e->clauses, e->pos, args...);
    case ExpType::ANDALSO: return DoAndalso(e->a, e->b, e->pos, args...);
    case ExpType::ORELSE: return DoOrelse(e->a, e->b, e->pos, args...);
    case ExpType::FAIL: return DoFail(e->a, e->pos, args...);
    }
    LOG(FATAL) << "Unhandled type in el::Pass::DoExp!";
    return nullptr;
  }

  virtual const Dec *DoDec(const Dec *d, Args... args) {
    switch (d->type) {
    case DecType::VAL: return DoValDec(d->pat, d->exp, d->pos, args...);
    case DecType::FUN: return DoFunDec(d->funs, d->pos, args...);
    case DecType::LOCAL: return DoLocalDec(d->decs1, d->decs2, d->pos, args...);
    case DecType::DATATYPE: return DoDatatypeDec(
        d->tyvars, d->datatypes, d->pos, args...);
    case DecType::OBJECT: return DoObjectDec(d->object, d->pos, args...);
    case DecType::TYPE: return DoTypeDec(
        d->tyvars, d->str, d->t, d->pos, args...);
    case DecType::OPEN: return DoOpenDec(d->exp, d->pos, args...);
    }
    LOG(FATAL) << "Unhandled type in el::Pass::DoDec!";
    return nullptr;
  }

  virtual const Pat *DoPat(const Pat *p, Args... args) {
    switch (p->type) {
    case PatType::VAR: return DoVarPat(p->str, p->pos, args...);
    case PatType::WILD: return DoWildPat(p->pos, args...);
    case PatType::TUPLE: return DoTuplePat(p->children, p->pos, args...);
    case PatType::RECORD: return DoRecordPat(p->str_children, p->pos, args...);
    case PatType::OBJECT:
      return DoObjectPat(p->str, p->str_children, p->pos, args...);
    case PatType::ANN: return DoAnnPat(p->a, p->ann, p->pos, args...);
    case PatType::AS: return DoAsPat(p->a, p->b, p->pos, args...);
    case PatType::INT: return DoIntPat(p->integer, p->pos, args...);
    case PatType::STRING: return DoStringPat(p->str, p->pos, args...);
    case PatType::BOOL: return DoBoolPat(p->boolean, p->pos, args...);
    case PatType::APP: return DoAppPat(p->str, p->a, p->pos, args...);
    }
    LOG(FATAL) << "Unhandled type in el::Pass::DoPat!";
    return nullptr;
  }

  virtual const Layout *DoLayout(const Layout *lay, Args... args) {
    switch (lay->type) {
    case LayoutType::TEXT: return DoTextLayout(lay->str, args...);
    case LayoutType::JOIN: return DoJoinLayout(lay->children, args...);
    case LayoutType::EXP: return DoExpLayout(lay->exp, args...);
    }
    LOG(FATAL) << "Unhandled type in el::Pass::DoLayout!";
    return nullptr;
  }

  std::vector<const Type *> DoTypes(const std::vector<const Type *> &v,
                                    Args... args) {
    std::vector<const Type *> vv;
    vv.reserve(v.size());
    for (const Type *t : v)
      vv.push_back(DoType(t, args...));
    return vv;
  }

  // Types.
  virtual const Type *DoVarType(const std::string &s,
                                const std::vector<const Type *> &v,
                                Args... args) {
    return pool->VarType(s, DoTypes(v, args...));
  }

  virtual const Type *DoProduct(const std::vector<const Type *> &v,
                                Args... args) {
    return pool->Product(DoTypes(v, args...));
  }

  virtual const Type *DoRecordType(
      const std::vector<std::pair<std::string, const Type *>> &v,
      Args... args) {
    std::vector<std::pair<std::string, const Type *>> vv;
    for (const auto &[lab, t] : v)
      vv.emplace_back(lab, DoType(t, args...));
    return pool->RecordType(vv);
  }

  virtual const Type *DoArrow(const Type *dom, const Type *cod,
                              Args... args) {
    return pool->Arrow(DoType(dom, args...),
                       DoType(cod, args...));
  }

  // Expressions.

  virtual const Exp *DoString(const std::string &s,
                              size_t pos, Args... args) {
    return pool->String(s, pos);
  }

  virtual const Exp *DoLayoutExp(const Layout *lay, size_t pos, Args... args) {
    return pool->LayoutExp(DoLayout(lay, args...));
  }

  virtual const Exp *DoVar(const std::string &v, size_t pos, Args... args) {
    return pool->Var(v, pos);
  }

  virtual const Exp *DoInt(const BigInt &i, size_t pos, Args... args) {
    return pool->Int(i);
  }

  virtual const Exp *DoBool(bool b, size_t pos, Args... args) {
    return pool->Bool(b);
  }

  virtual const Exp *DoFloat(double d, size_t pos, Args... args) {
    return pool->Float(d);
  }

  virtual const Exp *DoTuple(const std::vector<const Exp *> &v,
                             size_t pos,
                             Args... args) {
    std::vector<const Exp *> vv;
    for (const auto &e : v) vv.push_back(DoExp(e, args...));
    return pool->Tuple(vv, pos);
  }

  virtual const Exp *DoRecord(
      const std::vector<std::pair<std::string, const Exp *>> &v,
      size_t pos,
      Args... args) {
    std::vector<std::pair<std::string, const Exp *>> vv;
    for (const auto &[s, e] : v) vv.emplace_back(s, DoExp(e, args...));
    return pool->Record(vv, pos);
  }

  virtual const Exp *DoObject(
      const std::string &objtype,
      const std::vector<std::pair<std::string, const Exp *>> &v,
      size_t pos,
      Args... args) {
    std::vector<std::pair<std::string, const Exp *>> vv;
    for (const auto &[s, e] : v) vv.emplace_back(s, DoExp(e, args...));
    return pool->Object(objtype, vv, pos);
  }

  virtual const Exp *DoWith(
      const Exp *obj,
      const std::string &objtype,
      const std::string &field,
      const Exp *rhs,
      size_t pos,
      Args... args) {
    return pool->With(DoExp(obj, args...), objtype, field, DoExp(rhs, args...));
  }

  virtual const Exp *DoWithout(
      const Exp *obj,
      const std::string &objtype,
      const std::string &field,
      size_t pos,
      Args... args) {
    return pool->Without(DoExp(obj, args...), objtype, field);
  }

  virtual const Exp *DoCase(
      const Exp *obj,
      const std::vector<std::pair<const Pat *, const Exp *>> &clauses,
      size_t pos,
      Args... args) {
    std::vector<std::pair<const Pat *, const Exp *>> cc;
    for (const auto &[p, e] : clauses) {
      cc.emplace_back(DoPat(p, args...), DoExp(e, args...));
    }
    return pool->Case(DoExp(obj, args...), cc, pos);
  }

  virtual const Exp *DoFn(
      const std::string &self,
      const std::vector<std::pair<const Pat *, const Exp *>> &clauses,
      size_t pos,
      Args... args) {
    std::vector<std::pair<const Pat *, const Exp *>> cc;
    for (const auto &[p, e] : clauses) {
      cc.emplace_back(DoPat(p, args...), DoExp(e, args...));
    }
    return pool->Fn(self, cc, pos);
  }

  virtual const Exp *DoJoin(const std::vector<const Exp *> &v,
                            size_t pos,
                            Args... args) {
    std::vector<const Exp *> vv;
    for (const Exp *e : v) vv.push_back(DoExp(e, args...));
    return pool->Join(vv);
  }

  virtual const Exp *DoLet(const std::vector<const Dec *> &ds,
                           const Exp *e,
                           size_t pos,
                           Args... args) {
    std::vector<const Dec *> dd;
    for (const Dec *d : ds) dd.push_back(DoDec(d, args...));
    return pool->Let(dd, DoExp(e, args...), pos);
  }

  virtual const Exp *DoIf(const Exp *cond, const Exp *t, const Exp *f,
                          size_t pos,
                          Args... args) {
    return pool->If(DoExp(cond, args...),
                    DoExp(t, args...),
                    DoExp(f, args...));
  }

  virtual const Exp *DoApp(const Exp *f, const Exp *arg,
                           size_t pos, Args... args) {
    return pool->App(DoExp(f, args...), DoExp(arg, args...), pos);
  }

  virtual const Exp *DoAnn(const Exp *e, const Type *t,
                           size_t pos, Args... args) {
    return pool->Ann(DoExp(e, args...), DoType(t, args...), pos);
  }

  virtual const Exp *DoAndalso(
      const Exp *a, const Exp *b,
      size_t pos,
      Args... args) {
    return pool->Andalso(DoExp(a, args...), DoExp(b, args...), pos);
  }

  virtual const Exp *DoOrelse(
      const Exp *a, const Exp *b,
      size_t pos,
      Args... args) {
    return pool->Orelse(DoExp(a, args...), DoExp(b, args...), pos);
  }

  virtual const Exp *DoFail(const Exp *e, size_t pos, Args... args) {
    return pool->Fail(DoExp(e, args...));
  }


  // Declarations.

  virtual const Dec *DoValDec(const Pat *pat, const Exp *rhs,
                              size_t pos, Args... args) {
    return pool->ValDec(DoPat(pat, args...), DoExp(rhs, args...));
  }

  virtual const Dec *DoFunDec(const std::vector<FunDec> &funs,
                              size_t pos, Args... args) {
    std::vector<FunDec> ffs;
    ffs.reserve(funs.size());
    for (const auto &fd : funs) {
      FunDec ffd;
      ffd.name = fd.name;
      for (const auto &[ps, e] : fd.clauses) {
        std::vector<const Pat *> pps;
        pps.reserve(ps.size());
        for (const Pat *p : ps) pps.push_back(DoPat(p, args...));
        ffd.clauses.emplace_back(std::move(pps), DoExp(e, args...));
      }
      ffs.push_back(std::move(ffd));
    }
    return pool->FunDec(std::move(ffs), pos);
  }

  virtual const Dec *DoLocalDec(const std::vector<const Dec *> &decs1,
                                const std::vector<const Dec *> &decs2,
                                size_t pos,
                                Args... args) {
    std::vector<const Dec *> dd1, dd2;
    for (const Dec *d : decs1) dd1.push_back(DoDec(d, args...));
    for (const Dec *d : decs2) dd2.push_back(DoDec(d, args...));
    return pool->LocalDec(dd1, dd2);
  }

  virtual const Dec *DoDatatypeDec(const std::vector<std::string> &tyvars,
                                   const std::vector<DatatypeDec> &ds,
                                   size_t pos,
                                   Args... args) {
    std::vector<DatatypeDec> dds;
    dds.reserve(ds.size());
    for (const auto &dd : ds) {
      DatatypeDec ddd;
      ddd.name = dd.name;
      for (const auto &[lab, t] : dd.arms) {
        ddd.arms.emplace_back(lab,
                              t == nullptr ? nullptr : DoType(t, args...));
      }
      dds.push_back(std::move(ddd));
    }
    return pool->DatatypeDec(tyvars, std::move(dds));
  }

  virtual const Dec *DoObjectDec(const ObjectDec &objdec,
                                 size_t pos, Args... args) {
    std::vector<std::pair<std::string, const Type *>> fields;
    fields.reserve(objdec.fields.size());
    for (const auto &[lab, t] : objdec.fields) {
      fields.emplace_back(lab, DoType(t, args...));
    }
    return pool->ObjectDec(ObjectDec{
        .name = objdec.name,
        .fields = std::move(fields)
      });
  }

  virtual const Dec *DoTypeDec(const std::vector<std::string> &tyvars,
                               const std::string &var,
                               const Type *t,
                               size_t pos,
                               Args... args) {
    return pool->TypeDec(tyvars, var, DoType(t, args...));
  }

  virtual const Dec *DoOpenDec(const Exp *e, size_t pos, Args... args) {
    return pool->OpenDec(DoExp(e, args...));
  }


  // Patterns.

  virtual const Pat *DoVarPat(const std::string &v, size_t pos, Args... args) {
    return pool->VarPat(v, pos);
  }

  virtual const Pat *DoWildPat(size_t pos, Args... args) {
    return pool->WildPat();
  }

  virtual const Pat *DoTuplePat(const std::vector<const Pat *> &v,
                                size_t pos,
                                Args... args) {
    std::vector<const Pat *> ps;
    for (const Pat *p : v) {
      ps.push_back(DoPat(p, args...));
    }
    return pool->TuplePat(std::move(ps));
  }

  virtual const Pat *DoRecordPat(
      const std::vector<std::pair<std::string, const Pat *>> &v,
      size_t pos,
      Args... args) {
    std::vector<std::pair<std::string, const Pat *>> ps;
    for (const auto &[lab, p] : v) {
      ps.emplace_back(lab, DoPat(p, args...));
    }
    return pool->RecordPat(std::move(ps), pos);
  }

  virtual const Pat *DoObjectPat(
      const std::string &objtype,
      const std::vector<std::pair<std::string, const Pat *>> &v,
      size_t pos,
      Args... args) {
    std::vector<std::pair<std::string, const Pat *>> ps;
    for (const auto &[lab, p] : v) {
      ps.emplace_back(lab, DoPat(p, args...));
    }
    return pool->ObjectPat(objtype, std::move(ps));
  }

  virtual const Pat *DoAnnPat(const Pat *a, const Type *t,
                              size_t pos, Args... args) {
    return pool->AnnPat(DoPat(a, args...), DoType(t, args...), pos);
  }

  virtual const Pat *DoAsPat(const Pat *a, const Pat *b,
                             size_t pos, Args... args) {
    return pool->AsPat(DoPat(a, args...), DoPat(b, args...));
  }

  virtual const Pat *DoIntPat(const BigInt &bi, size_t pos, Args... args) {
    return pool->IntPat(bi);
  }

  virtual const Pat *DoBoolPat(bool b, size_t pos, Args... args) {
    return pool->BoolPat(b);
  }

  virtual const Pat *DoStringPat(const std::string &s,
                                 size_t pos, Args... args) {
    return pool->StringPat(s);
  }

  virtual const Pat *DoAppPat(const std::string &s, const Pat *p,
                              size_t pos, Args... args) {
    return pool->AppPat(s, DoPat(p, args...));
  }

  // Layout.

  virtual const Layout *DoTextLayout(const std::string &content, Args... args) {
    return pool->TextLayout(content);
  }

  virtual const Layout *DoJoinLayout(
      const std::vector<const Layout *> &v, Args... args) {
    std::vector<const Layout *> vv;
    vv.reserve(v.size());
    for (const Layout *lay : v) vv.push_back(DoLayout(lay, args...));
    return pool->JoinLayout(vv);
  }

  const Layout *DoExpLayout(const Exp *exp, Args... args) {
    return pool->ExpLayout(DoExp(exp, args...));
  }

protected:
  AstPool *pool = nullptr;
};

}  // namespace el

#endif
