
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

  // This could probably be simplified away; it's a remnant of
  // when we used to have Dec as a separate syntactic class in
  // IL.
  struct Dec {
    std::vector<std::string> tyvars;
    std::string x;
    const il::Exp *rhs = nullptr;
  };
  const il::Exp *LetDecs(const std::vector<Dec> &decs,
                         const il::Exp *body);

  void CheckAffine(const el::Pat *orig_pat) const;

  const el::Exp *SimpleBind(std::string nv, std::string objv,
                            const el::Exp *body);

  std::pair<Context, std::vector<Dec>>
  CompileIrrefutableRec(
      const Context &G,
      const el::Pat *pat,
      const il::Exp *rhs,
      const il::Type *rhs_type,
      bool rhs_valuable);

  std::pair<Context, std::vector<Dec>>
  GeneralizeOne(
      const Context &G,
      std::vector<std::string> vars,
      const il::Exp *rhs,
      const il::Type *type,
      bool rhs_valuable);

  std::pair<const Exp *, const Type *>
  SplitRecordPattern(
      const Context &G,
      Matrix matrix,
      int x);

  std::pair<const Exp *, const Type *>
  SplitIntPattern(
      const Context &G,
      Matrix matrix,
      int x);

  std::pair<const Exp *, const Type *>
  SplitBoolPattern(
      const Context &G,
      Matrix matrix,
      int x);

  std::pair<const Exp *, const Type *>
  SplitStringPattern(
      const Context &G,
      Matrix matrix,
      int x);

  std::pair<const Exp *, const Type *>
  SplitAppPattern(
      const Context &G,
      Matrix matrix,
      int x);

  int verbose = 0;
  Elaboration *elab = nullptr;
};

}  // namespace il

#endif
