
#ifndef _REPHRASE_EL_PASS_H
#define _REPHRASE_EL_PASS_H

#include <string>

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
      // TODO!
    default:
      LOG(FATAL) << "Unhandled type in el::Pass::DoExp!";
    }
  }

  virtual const Dec *DoDec(const Dec *e, Args... args) {
    switch (e->type) {
      // TODO!
    default:
      LOG(FATAL) << "Unhandled type in el::Pass::DoDec!";
    }
  }

  virtual const Pat *DoPat(const Pat *e, Args... args) {
    switch (e->type) {
      // TODO!
    default:
      LOG(FATAL) << "Unhandled type in el::Pass::DoPat!";
    }
  }

  virtual const Layout *DoLayout(const Layout *lay, Args...) {
    switch (lay->type) {
      // TODO!
    default:
      LOG(FATAL) << "Unhandled type in el::Pass::DoLayout!";
    }
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

  virtual const Exp *DoString(const std::string &s, Args... args) {
    return pool->String(s);
  }

  virtual const Exp *DoLayoutExp(const Layout *lay, Args... args) {
    return pool->LayoutExp(DoLayout(lay, args...));
  }

  virtual const Exp *DoVar(const std::string &v, Args... args) {
    return pool->Var(v);
  }

  virtual const Exp *DoInt(const BigInt &i, Args... args) {
    return pool->Int(i);
  }

  virtual const Exp *DoFloat(double d, Args... args) {
    return pool->Float(d);
  }

  virtual const Exp *DoTuple(const std::vector<const Exp *> &v,
                             Args... args) {
    std::vector<const Exp *> vv;
    for (const auto &e : v) vv.push_back(DoExp(e, args...));
    return pool->Tuple(vv);
  }

  virtual const Exp *DoRecord(
      const std::vector<std::pair<std::string, const Exp *>> &v,
      Args... args) {
    std::vector<std::pair<std::string, const Exp *>> vv;
    for (const auto &[s, e] : v) vv.emplace_back(s, DoExp(e, args...));
    return pool->Record(vv);
  }

  virtual const Exp *DoCase(
      const Exp *obj,
      const std::vector<std::pair<const Pat *, const Exp *>> &clauses,
      Args... args) {
    std::vector<std::pair<const Pat *, const Exp *>> cc;
    for (const auto &[p, e] : clauses) {
      cc.emplace_back(DoPat(p, args...), DoExp(e, args...));
    }
    return pool->Case(DoExp(obj, args...), cc);
  }

  virtual const Exp *DoFn(
      const std::string &self,
      const std::vector<std::pair<const Pat *, const Exp *>> &clauses,
      Args... args) {
    std::vector<std::pair<const Pat *, const Exp *>> cc;
    for (const auto &[p, e] : clauses) {
      cc.emplace_back(DoPat(p, args...), DoExp(e, args...));
    }
    return pool->Fn(self, cc);
  }

  virtual const Exp *DoJoin(const std::vector<const Exp *> &v,
                            Args... args) {
    std::vector<const Exp *> vv;
    for (const Exp *e : v) vv.push_back(DoExp(e, args...));
    return pool->Join(vv);
  }

  virtual const Exp *DoLet(const std::vector<const Dec *> &ds,
                           const Exp *e,
                           Args... args) {
    std::vector<const Dec *> dd;
    for (const Dec *d : ds) dd.push_back(DoDec(d, args...));
    return pool->Let(dd, DoExp(e, args...));
  }

  virtual const Exp *DoIf(const Exp *cond, const Exp *t, const Exp *f,
                          Args... args) {
    return pool->If(DoExp(cond, args...),
                    DoExp(t, args...),
                    DoExp(f, args...));
  }

  virtual const Exp *DoApp(const Exp *f, const Exp *arg, Args... args) {
    return pool->App(DoExp(f, args...), DoExp(arg, args...));
  }

  virtual const Exp *DoAnn(const Exp *e, const Type *t, Args... args) {
    return pool->Ann(DoExp(e, args...), DoType(t, args...));
  }

  virtual const Exp *DoFail(const Exp *e, Args... args) {
    return pool->Fail(DoExp(e, args...));
  }


  // Declarations.

  // Patterns.

  // Layout.

protected:
  AstPool *pool = nullptr;
};

}  // namespace el

#endif
