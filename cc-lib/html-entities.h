
#ifndef _CC_LIB_HTML_ENTITIES_H
#define _CC_LIB_HTML_ENTITIES_H

#include <optional>
#include <string_view>
#include <string>
#include <unordered_map>

struct HTMLEntities {

  // Get an entity without the & or ;.
  // For example GetEntity("CirclePlus") is U+2295 encoded as UTF-8.
  // Case sensitive. Note that the string is sometimes
  // multiple codepoints, although it is usually just one.
  static std::optional<std::string> GetEntity(std::string_view ent);

  // XXX
  static const std::unordered_map<std::string, std::string> &GetMap();

};

#endif
