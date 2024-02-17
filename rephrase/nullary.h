#ifndef _REPHRASE_NULLARY_H
#define _REPHRASE_NULLARY_H

#include "el.h"

namespace el {

struct Nullary {
  explicit Nullary(AstPool *pool);

  const Exp *Rewrite(const Exp *);

 private:
  AstPool *pool;
};

}  // el

#endif
