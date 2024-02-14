
#include "pp.h"

#include <string>

#include "base/stringprintf.h"
#include "util.h"

std::string EscapeString(const std::string &s) {
  // TODO: Escape other characters...
  return StringPrintf(
      "\"%s\"", Util::Replace(s, "\"", "\\\"").c_str());
}
