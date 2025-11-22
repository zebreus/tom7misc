
// This is an in-progress port of ftfy. Doesn't work yet!!

#ifndef _CC_LIB_FIX_ENCODING_H
#define _CC_LIB_FIX_ENCODING_H

#include <optional>
#include <string>
#include <string_view>

struct FixEncoding {

  // Is the string's encoding likely bad (not really UTF-8)?
  // This is heuristic.
  static bool IsBad(std::string_view str);

  static std::string Fix(std::string_view str);

  // Decode UTF-8 variants (if possible), returning regular UTF-8.
  // CESU-8 is surrogate pairs (for UTF-16 systems, awful) and Java is
  // that plus an additional code for nulls. Leaves unmatched surrogate
  // pairs in place.
  static std::optional<std::string> DecodeVariantUTF8(std::string_view bytes);

};

#endif
