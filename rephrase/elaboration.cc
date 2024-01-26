
#include "elaboration.h"

#include <utility>
#include <string>

#include "el.h"
#include "il.h"
#include "initial.h"

#include "base/stringprintf.h"

using Context = il::Context;

const il::Exp *Elaboration::Elaborate(const el::Exp *el_exp) {
  Context G = init.InitialContext();

  const auto &[e, t] = Elab(G, el_exp);

  // Should check that the program has type layout?
  if (verbose > 0) {
    printf("Program type: %s\n", TypeString(t).c_str());
  }

  return e;
}

const std::pair<const il::Exp *, const il::Type *> Elaboration::Elab(
    const Context &G,
    const el::Exp *el_exp) {

  switch (el_exp->type) {
  case el::ExpType::STRING:
    return std::make_pair(pool->String(el_exp->str),
                          pool->StringType());
  case el::ExpType::JOIN:
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
  case el::ExpType::VAR:
    break;
  case el::ExpType::LAYOUT:
    break;
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
