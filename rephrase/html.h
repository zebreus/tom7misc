
// TO cc-lib?

#ifndef _REPHRASE_HTML_H
#define _REPHRASE_HTML_H

#include <vector>
#include <string>
#include <unordered_map>

struct HTMLNode {
  bool is_tag = false;
  // If a tag, then this is the tag name. Otherwise it is the textual
  // contents.
  std::string str;
  std::unordered_map<std::string, std::string> attrs;
  std::vector<HTMLNode> children;
};

struct HTML {
  static std::vector<HTMLNode> Parse(const std::string &html,
                                     std::string *parse_error = nullptr);
  static void DebugPrint(const std::vector<HTMLNode> &nodes);
};

#endif
