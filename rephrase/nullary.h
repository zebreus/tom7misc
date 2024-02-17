#ifndef _REPHRASE_NULLARY_H
#define _REPHRASE_NULLARY_H

#include "el.h"

namespace el {

struct Nullary {
  explicit Nullary(AstPool *pool);

  // Rewrites nullary datatype constructors (e.g. NONE) to take an
  // explicit unit (e.g. NONE of {}). This also requires rewriting the
  // constructors when they appear in patterns (PatType::VAR) or
  // expressions (ExpType::VAR).
  //
  // This assumes ctors cannot be shadowed by regular variables.
  const Exp *Rewrite(const Exp *);

 private:
  AstPool *pool;
};

}  // el

#endif
