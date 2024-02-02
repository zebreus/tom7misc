
#ifndef _REPHRASE_PATTERN_COMPILATION_H
#define _REPHRASE_PATTERN_COMPILATION_H

// Pattern compilation (elaboration).

#include "context.h"
#include "elaboration.h"

namespace il {

struct PatternCompilation {
  explicit PatternCompilation(Elaboration *elab);

  std::pair<const Exp *, const Type *> Compile(
      const Context &G,
      // The case object (EL variable).
      const std::string &obj,
      // The type of the case object.
      const Type *obj_type,
      // Pattern rows, mapping EL pattern to EL expression.
      // To explicitly insert a default, use the wildcard pattern
      // at the end.
      const std::vector<std::pair<const el::Pat *, const el::Exp *>> &rows);

  // Compile an irrefutable pattern, which is a single row
  // just consisting of Record, wild, variable, and as patterns.
  // (It would be possible to support single-arm sums too.)
  // This is used for val declarations.
  //
  // let pat = rhs
  // in body
  std::pair<const Exp *, const Type *> CompileIrrefutable(
      const Context &G,
      const el::Pat *pat,
      const el::Exp *rhs,
      const el::Exp *body);


private:
  struct Matrix;
  std::pair<const Exp *, const Type *> Comp(
      const Context &G,
      Matrix m);

  void CheckAffine(const Matrix &m) const;
  const el::Exp *SimpleBind(std::string nv, std::string objv,
                            const el::Exp *body);

  std::pair<Context, std::vector<const Dec *>>
  CompileIrrefutableRec(
      const Context &G,
      const el::Pat *pat,
      const il::Exp *rhs,
      const il::Type *rhs_type,
      bool rhs_valuable);

  std::pair<Context, std::vector<const Dec *>>
  GeneralizeOne(
      const Context &G,
      const std::vector<std::string> &vars,
      const il::Exp *rhs,
      const il::Type *type,
      bool rhs_valuable);

  Elaboration *elab = nullptr;
};

}  // namespace il

#endif
