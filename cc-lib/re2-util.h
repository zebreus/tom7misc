
#ifndef _CC_LIB_RE2_UTIL_H
#define _CC_LIB_RE2_UTIL_H

#include "re2/re2.h"

#include <string_view>
#include <functional>
#include <string>
#include <span>

struct RE2Util {

  // for each (non-overlapping) match of pattern in source, call
  // f with the match to produce the replacement string. The 0th
  // element of the match is the full substring that the pattern
  // matched, 1 the first capturing group, and so on.
  //
  // The pattern should not match the empty string, because there
  // would be no possible finite result.
  static std::string MapReplacement(
      std::string_view source,
      const RE2 &pattern,
      const std::function<std::string(std::span<const std::string_view>)> &f);

};


#endif
