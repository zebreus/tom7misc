
#ifndef _REPHRASE_INITIAL_H
#define _REPHRASE_INITIAL_H

#include "context.h"
#include "il.h"

namespace il {

struct Initial {
  explicit Initial(AstPool *pool);

  const ElabContext &InitialContext() const;

  // Types needed by elaboration.

 private:
  ElabContext ctx;
};

}  // il

#endif
