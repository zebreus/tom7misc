
#ifndef _REPHRASE_PATTERN_COMPILATION_H
#define _REPHRASE_PATTERN_COMPILATION_H

// Pattern compilation (elaboration).

#include <cstddef>
#include <string>
#include <tuple>
#include <utility>
#include <functional>
#include <vector>

#include "context.h"
#include "elaboration.h"
#include "il.h"
#include "el.h"

namespace il {

struct PatternCompilation {
  explicit PatternCompilation(Elaboration *elab);

  std::pair<const Exp *, const Type *> Compile(
      const ElabContext &G,
      // The case object (EL variable).
      const std::string &obj,
      // The type of the case object.
      const Type *obj_type,
      // Pattern rows, mapping EL pattern to EL expression.
      // To explicitly insert a default, use the wildcard pattern
      // at the end.
      const std::vector<std::pair<const el::Pat *, const el::Exp *>> &rows,
      // The source position that should be reported in errors.
      size_t pos);

  // Compile an irrefutable pattern, which is a single row
  // just consisting of Record, wild, variable, and as patterns.
  // (It would be possible to support single-arm sums too.)
  // This is used for val declarations.
  //
  // leval pat = rhs
  std::tuple<std::vector<Elaboration::ILDec>,
             std::vector<il::ElabContext::Binding>,
             il::ElabContext>
  CompileIrrefutable(
      const ElabContext &G,
      const el::Pat *pat,
      const el::Exp *rhs);

 private:
  struct Matrix;
  std::pair<const Exp *, const Type *> Comp(
      const ElabContext &G,
      Matrix m);

  void CheckAffine(const el::Pat *orig_pat) const;

  const el::Exp *SimpleBind(std::string nv, std::string objv,
                            const el::Exp *body);

  std::tuple<std::vector<Elaboration::ILDec>,
             std::vector<ElabContext::Binding>,
             ElabContext>
  CompileIrrefutableRec(
      const ElabContext &G,
      const el::Pat *pat,
      const il::Exp *rhs,
      const il::Type *rhs_type,
      bool rhs_valuable);

  std::tuple<std::vector<Elaboration::ILDec>,
             std::vector<ElabContext::Binding>,
             ElabContext>
  GeneralizeOne(
      const ElabContext &G,
      std::vector<std::string> vars,
      const il::Exp *rhs,
      const il::Type *type,
      bool rhs_valuable);

  std::pair<const Exp *, const Type *>
  SplitRecordPattern(
      const ElabContext &G,
      Matrix matrix,
      int x);

  std::pair<const Exp *, const Type *>
  SplitAsPattern(
      const ElabContext &G,
      Matrix matrix,
      int x);

  std::pair<const Exp *, const Type *>
  SplitIntPattern(
      const ElabContext &G,
      Matrix matrix,
      int x);

  std::pair<const Exp *, const Type *>
  SplitBoolPattern(
      const ElabContext &G,
      Matrix matrix,
      int x);

  std::pair<const Exp *, const Type *>
  SplitStringPattern(
      const ElabContext &G,
      Matrix matrix,
      int x);

  std::pair<const Exp *, const Type *>
  SplitAppPattern(
      const ElabContext &G,
      Matrix matrix,
      int x);

  std::pair<const Exp *, const Type *>
  SplitObjectPattern(
      const ElabContext &G,
      Matrix matrix,
      int x);

  // Generic error for unification. Should improve this!
  std::function<std::string()> MatrixError(const Matrix &matrix,
                                           const std::string &e);

  int verbose = 0;
  Elaboration *elab = nullptr;
};

}  // namespace il

#endif
