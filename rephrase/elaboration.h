
#ifndef _REPHRASE_ELABORATION_H
#define _REPHRASE_ELABORATION_H

#include "el.h"
#include "il.h"

#include "context.h"
#include "initial.h"

namespace il { struct PatternCompilation; }

// Elaboration is a recursive transformation from EL to IL.
struct Elaboration {

  Elaboration(el::AstPool *el_pool, il::AstPool *il_pool);
  ~Elaboration();

  void SetVerbose(int v) { verbose = v; }

  il::Program Elaborate(const el::Exp *el_exp);

private:
  friend struct il::PatternCompilation;

  const std::pair<const il::Exp *, const il::Type *> Elab(
      const il::Context &G,
      const el::Exp *el_exp);

  const il::Type *ElabType(
      const il::Context &G,
      const el::Type *el_type);

  const il::Type *NewEVar();

  const std::pair<const il::Exp *, const il::Type *> ElabDecs(
      const il::Context &G,
      const std::vector<const el::Dec *> &decs,
      const el::Exp *exp);

  // Some expressions that are repeatedly used.
  const il::Exp *fail_match = nullptr;
  // But the type (a new evar) must be allocated each time!
  std::pair<const il::Exp *, const il::Type *> FailMatch();

  int verbose = 0;
  el::AstPool *el_pool = nullptr;
  il::AstPool *pool = nullptr;
  il::Initial init;
  std::unique_ptr<il::PatternCompilation> pattern_compilation;
};

#endif

