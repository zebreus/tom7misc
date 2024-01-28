
#include "elaboration.h"

#include <utility>
#include <string>

#include "el.h"
#include "il.h"
#include "initial.h"

#include "base/stringprintf.h"

// This code has to mention both el and il stuff with the same
// name. But there are many things that are unambiguous.
using Context = il::Context;
using PolyType = il::PolyType;
using Unification = il::Unification;
using SingletonKind = il::SingletonKind;

const il::Exp *Elaboration::Elaborate(const el::Exp *el_exp) {
  Context G = init.InitialContext();

  const auto &[e, t] = Elab(G, el_exp);

  // Should check that the program has type layout?
  if (verbose > 0) {
    printf("Program type: %s\n", TypeString(t).c_str());
  }

  return e;
}

const il::Type *Elaboration::ElabType(const Context &G,
                                      const el::Type *el_type) {
  switch (el_type->type) {
  case el::TypeType::VAR: {
    const SingletonKind *k = G.FindType(el_type->var);
    CHECK(k != nullptr) << "Unbound (type) variable: " << el_type->var;
    CHECK(k->tyvars.empty()) << "Unimplemented kind>0";
    return k->type;
  }
  case el::TypeType::ARROW: {
    const il::Type *dom = ElabType(G, el_type->a);
    const il::Type *cod = ElabType(G, el_type->b);
    return pool->Arrow(dom, cod);
  }
  case el::TypeType::PRODUCT: {
    // Translate into the equivalent record.
    std::vector<std::pair<std::string, const il::Type *>> rec;
    rec.reserve(el_type->children.size());
    for (int i = 0; i < (int)el_type->children.size(); i++) {
      const el::Type *t = el_type->children[i];
      const il::Type *tt = ElabType(G, t);
      rec.emplace_back(StringPrintf("%d", i + 1), tt);
    }
    return pool->RecordType(std::move(rec));
  }
  case el::TypeType::RECORD: {
    std::vector<std::pair<std::string, const il::Type *>> rec;
    rec.reserve(el_type->str_children.size());
    for (const auto &[lab, t] : el_type->str_children) {
      const il::Type *tt = ElabType(G, t);
      rec.emplace_back(lab, tt);
    }
    return pool->RecordType(std::move(rec));
  }
  }
}

const std::pair<const il::Exp *, const il::Type *> Elaboration::Elab(
    const Context &G,
    const el::Exp *el_exp) {

  switch (el_exp->type) {
  case el::ExpType::STRING:
    return std::make_pair(pool->String(el_exp->str),
                          pool->StringType());
  case el::ExpType::ANN: {
    // Type annotations are erased during elaboration, after
    // ensuring they hold through unification.
    const auto &[e, t] = Elab(G, el_exp->a);
    const il::Type *tann = ElabType(G, el_exp->t);
    Unification::Unify("type annotation", t, tann);
    return std::make_pair(e, t);
  }
  case el::ExpType::JOIN:
    LOG(FATAL) << "Unimplemented in elaboration: JOIN";
    break;

  case el::ExpType::TUPLE: {
    // This means a record expression with fields {1: e1, 2: e2, ...}.
    std::vector<std::pair<std::string, const il::Type *>> lct;
    std::vector<std::pair<std::string, const il::Exp *>> lce;
    lct.reserve(el_exp->children.size());
    lce.reserve(el_exp->children.size());

    for (int i = 0; i < (int)el_exp->children.size(); i++) {
      std::string lab = StringPrintf("%d", i + 1);
      const auto &[e, t] = Elab(G, el_exp->children[i]);
      lce.emplace_back(lab, e);
      lct.emplace_back(lab, t);
    }

    return std::make_pair(pool->Record(std::move(lce)),
                          pool->RecordType(std::move(lct)));
  }
  case el::ExpType::INTEGER:
    return std::make_pair(pool->Int(el_exp->integer),
                          pool->IntType());
  case el::ExpType::VAR: {
    const il::PolyType *pt = G.Find(el_exp->str);
    CHECK(pt != nullptr) << "Unbound variable: " << el_exp->str;
    // If the variable is polymorphic, then instantiate it with
    // evars.
    LOG(FATAL) << "Unimplemented";
    break;
  }
  case el::ExpType::LET:
    break;
  case el::ExpType::IF:
    break;
  case el::ExpType::APP:
    break;
  default:
    break;
  }
  LOG(FATAL) << "Unimplemented exp type: " << el::ExpString(el_exp);
  return std::make_pair(nullptr, nullptr);
}
