
#include "pp.h"

#include <string>

#include "util.h"

std::string EscapeString(const std::string &s) {
  // TODO: Escape other characters...
  return Util::Replace(s, "\"", "\\\"");
}
