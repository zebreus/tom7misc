
#ifndef _REPHRASE_ELABORATION_H
#define _REPHRASE_ELABORATION_H

#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

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

  // IL has just LET (tyvars) x = e in e'.
  // This represents the (tyvars) x = e part so that
  // it's easier to manipulate a vector of them.
  struct ILDec {
    std::vector<std::string> tyvars;
    std::string x;
    const il::Exp *rhs = nullptr;
  };

  const std::pair<const il::Exp *, const il::Type *> Elab(
      const il::ElabContext &G,
      const el::Exp *el_exp);

  const il::Type *ElabType(
      const il::ElabContext &G,
      const el::Type *el_type);

  // Always layout type.
  const il::Exp *ElabLayout(
      const il::ElabContext &G,
      const el::Layout *lay);

  const il::Type *NewEVar();

  // Instantiate a polymorphic type at new fresh evars, so that
  // it can be used at any consistent type.
  const il::Type *EVarize(const std::vector<std::string> &tyvars,
                          const il::Type *t);

  std::tuple<std::vector<ILDec>,
             std::vector<il::ElabContext::Binding>,
             il::ElabContext>
  ElabDec(const il::ElabContext &G,
          const el::Dec *dec);

  std::pair<const il::Exp *, const il::Type *> ElabDecs(
      const il::ElabContext &G,
      const std::vector<const el::Dec *> &decs,
      const el::Exp *exp);

  const il::Exp *LetDecs(const std::vector<ILDec> &decs,
                         const il::Exp *body);

  // This is repeatedly used.
  std::pair<const il::Exp *, const il::Type *> FailMatch();

  // Globals collected during elaboration. They all have global
  // scope (including each other's bodies) and distinct names.
  std::vector<il::Global> globals;

  int verbose = 0;
  el::AstPool *el_pool = nullptr;
  il::AstPool *pool = nullptr;
  il::Initial init;
  std::unique_ptr<il::PatternCompilation> pattern_compilation;
};

#endif

