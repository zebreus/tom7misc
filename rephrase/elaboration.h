
#ifndef _REPHRASE_ELABORATION_H
#define _REPHRASE_ELABORATION_H

#include "el.h"
#include "il.h"

#include "context.h"
#include "initial.h"

struct Elaboration {

  Elaboration(il::AstPool *pool) : pool(pool), init(pool) {}

  void SetVerbose(int v) { verbose = v; }

  const il::Exp *Elaborate(const el::Exp *el_exp);

private:

  const std::pair<const il::Exp *, const il::Type *> Elab(
      const Context &ctx,
      const el::Exp *el_exp);

  int verbose = 0;
  il::AstPool *pool;
  Initial init;
};

#endif

