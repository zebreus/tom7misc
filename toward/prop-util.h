
#ifndef _TOWARD_PROP_UTIL_H
#define _TOWARD_PROP_UTIL_H

#include <string_view>

#include "prop.h"

struct PropUtil {
  // Generate an smtlib assertion that the two propositions are
  // the same (given the "valid" proposition is also true).
  //
  // Can just feed this to z3.

  static std::string CompareZ3(
      const World &world, const Prop &prop_before, const Prop &prop_after,
      const Prop &prop_valid);

 private:
  PropUtil() = delete;
};

#endif
