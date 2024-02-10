#ifndef _REPHRASE_IL_UTIL_H
#define _REPHRASE_IL_UTIL_H

#include <unordered_set>

#include "il.h"

namespace il {

struct ILUtil {
  // Get the free variables of the expression.
  static std::unordered_set<std::string> FreeExpVars(const Exp *e);

  static bool IsExpVarFree(const Exp *e, const std::string &x);

  // [e1/x]e2. Avoids capture.
  static const Exp *SubstExp(AstPool *pool,
                             const Exp *e1, const std::string &x,
                             const Exp *e2);

  // [t/x]e.
  static const Exp *SubstTypeInExp(
      AstPool *pool,
      const Type *t, const std::string &x,
      const Exp *e);

  static std::pair<std::string, const Exp *> AlphaVaryTypeInExp(
      AstPool *pool,
      const std::string &a,
      const Exp *e);

};

}  // namespace il

#endif
