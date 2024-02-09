#ifndef _REPHRASE_IL_UTIL_H
#define _REPHRASE_IL_UTIL_H

#include <unordered_set>

#include "il.h"

namespace il {

struct ILUtil {
  // Get the free variables of the expression.
  static std::unordered_set<std::string> FreeExpVars(const Exp *e);
};

}  // namespace il

#endif
