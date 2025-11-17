
// This is an in-progress port of ftfy. Doesn't work yet!!

#ifndef _CC_LIB_FIX_ENCODING_H
#define _CC_LIB_FIX_ENCODING_H

#include <string>
#include <string_view>

struct FixEncoding {

  // Is the string's encoding likely bad (not really UTF-8)?
  // This is heuristic.
  static bool IsBad(std::string_view str);

  static std::string Fix(std::string_view str);

};

#endif
