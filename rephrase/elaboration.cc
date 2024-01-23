
#include "elaboration.h"

#include <utility>
#include <string>

#include "el.h"
#include "il.h"
#include "ansi.h"
#include "initial.h"

// Elaboration is a recursive transformation

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
    const Context &ctx,
    const el::Exp *el_exp) {

  switch (el_exp->type) {
  case el::ExpType::STRING:
    return std::make_pair(pool->String(el_exp->str),
                          pool->StringType());
  case el::ExpType::JOIN:
    break;
  case el::ExpType::TUPLE:
    break;
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
