#ifndef _REPHRASE_NULLARY_H
#define _REPHRASE_NULLARY_H

#include "el.h"

namespace el {

struct Nullary {
  explicit Nullary(AstPool *pool);

  void SetVerbose(int verbose);

  // Rewrites nullary datatype constructors (e.g. NONE) to take an
  // explicit unit (e.g. NONE of {}). This also requires rewriting the
  // constructors when they appear in patterns (PatType::VAR) or
  // expressions (ExpType::VAR).
  //
  // This assumes ctors cannot be shadowed by regular variables (which
  // is impossible because they would be treated as constructor patterns),
  // but does handle the annoying case of a function declaration shadowing
  // a constructor.
  const Exp *Rewrite(const Exp *);

 private:
  int verbose = 0;
  AstPool *pool;
};

}  // el

#endif
