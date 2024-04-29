#ifndef _REPHRASE_IL_UTIL_H
#define _REPHRASE_IL_UTIL_H

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "il.h"

namespace il {

struct ILUtil {
  // Get the free variables of the expression.
  static std::unordered_set<std::string> FreeExpVars(const Exp *e);
  // .. with the number of occurrences.
  static std::unordered_map<std::string, int> FreeExpVarCounts(const Exp *e);
  // ... or the type variables it is applied to at each occurrence
  // (including duplicates).
  static std::unordered_map<
    std::string, std::vector<std::vector<const Type *>>>
  FreeExpVarTally(const Exp *e);

  static bool IsExpVarFree(const Exp *e, const std::string &x);
  static int ExpVarCount(const Exp *e, const std::string &x);

  // [e1/x]e2. Avoids capture.
  static const Exp *SubstExp(AstPool *pool,
                             const Exp *e1, const std::string &x,
                             const Exp *e2);

  // subst Λ(α1, α2, ... αn).e1 for x<τ1, τ2, ... τn> in e2;
  // The types τ1..τn are substituted for the α1..αn at each occurrence.
  static const Exp *SubstPolyExp(AstPool *pool,
                                 // α1, α2, ... αn
                                 const std::vector<std::string> &tyvars,
                                 const Exp *e1,
                                 const std::string &x,
                                 const Exp *e2);

  // [τ/x]e.
  static const Exp *SubstTypeInExp(
      AstPool *pool,
      const Type *t, const std::string &x,
      const Exp *e);

  // Return an equivalent x.e (for x an expression variable) where x
  // is fresh. x may be polymorphic, taking num_tyvars tyvars (0 for a
  // monomorphic variable).
  static std::pair<std::string, const Exp *> AlphaVaryExp(
      AstPool *pool,
      int num_tyvars,
      const std::string &x,
      const Exp *e);

  // Return an equivalent α.e (for α a type variable) where α is
  // fresh.
  static std::pair<std::string, const Exp *> AlphaVaryTypeInExp(
      AstPool *pool,
      const std::string &a,
      const Exp *e);

  // Once we're done with elaboration, assign all free EVars to
  // something arbitrary. We use void (empty sum type) since in
  // principle this would help with type-directed optimizations.
  static Program FinalizeEVars(AstPool *pool, const Program &program);


  // Free type variables in a type.
  // Note that in the presence of EVars, we can't know that a type
  // will remain closed, as its evar may be unified with a free
  // variable.
  static std::unordered_map<std::string, int> FreeTypeVarCounts(const Type *t);
  static std::unordered_set<std::string> FreeTypeVars(const Type *t);
  static std::unordered_set<std::string> FreeTypeVarsInExp(const Exp *e);

  // Substitution does not affect global symbols. But we have parallel
  // functions for globals.
  // (TODO: as needed)

  static std::unordered_map<std::string, int> LabelCounts(const Exp *e);

  // subst Λ(α1, α2, ... αn).e1 for sym<τ1, τ2, ... τn> in e2;
  // The types τ1..τn are substituted for the α1..αn at each occurrence.
  static const Exp *SubstPolyExpForLabel(
      AstPool *pool,
      // α1, α2, ... αn
      const std::vector<std::string> &tyvars,
      // Expression must be closed.
      const Exp *e1,
      const std::string &sym,
      const Exp *e2);

  static std::string VarSetString(const std::unordered_set<std::string> &s);

  // Partial conversion between IL types and ObjFieldType. Only base types
  // are allowed.
  static std::optional<il::ObjFieldType> GetObjFieldType(const il::Type *t);
  static const Type *ObjFieldTypeType(AstPool *pool, ObjFieldType oft);

  // In the case that the root of the type is a bound EVar, return what it's
  // set to (recursively). If (recursively) unset, return nullopt. For
  // regular types, just return them.
  //
  // This is used for some gross parts of elaboration (objects) where the
  // rule is "it must have a type that is resolved by now."
  static std::optional<const Type *> GetTypeIfKnown(const Type *);
  // After finalizing evars, there should be no unbound ones. However,
  // we do some type-directed translations that need to case analyze
  // the types. This recursively dereferences bound EVars (as needed),
  // and aborts if a free one is found
  static const Type *GetKnownType(const char *where, const Type *);
};

}  // namespace il

#endif
