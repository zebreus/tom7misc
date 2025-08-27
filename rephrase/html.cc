
#include "html.h"

#include <format>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <utility>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "re2/re2.h"
#include "util.h"

static constexpr bool VERBOSE = false;

using RE2 = re2::RE2;

static const std::unordered_set<std::string> &VoidTags() {
  static std::unordered_set<std::string> *v =
    new std::unordered_set<std::string>{
    "img",
  };

  return *v;
}

std::vector<HTMLNode> HTML::Parse(const std::string &input,
                                  std::string *parse_error) {
  static const RE2 start_tag_re("<([A-Za-z][-A-Za-z0-9]*)([^>]*)>");
  static const RE2 end_tag_re("</([A-Za-z][-A-Za-z0-9]*) *>");
  static const RE2 text_re("([^<]*)");
  static const RE2 attr_re(" *([A-Za-z][-A-Za-z0-9]*) *= *\"([^\"]*)\"");
  static const RE2 no_more_attrs_re(" */?");

  if (VERBOSE) {
    Print("\n\nParse: {}\n", input);
  }

  // Nodes in progress. There's always one element in this stack,
  // and the topmost one is not a real node (we will return its children).
  std::vector<HTMLNode> tag_stack = {
    HTMLNode()
  };

  auto AddText = [&](const std::string &s) {
      CHECK(!tag_stack.empty());
      HTMLNode &prev = tag_stack.back();
      if (prev.children.empty() || prev.children.back().is_tag) {
        // new empty text node.
        prev.children.push_back(HTMLNode{.is_tag = false, .str = s});
      } else {
        prev.children.back().str += s;
      }
    };

  re2::StringPiece in(input);
  std::string match;
  while (RE2::Consume(&in, text_re, &match)) {
    // TODO: unescape html
    if (!match.empty()) {
      AddText(match);
    }

    if (in.empty())
      break;

    if (VERBOSE)
      Print("Looking at: [{}]\n", std::string(in));

    std::string tag, attrs;
    if (RE2::Consume(&in, start_tag_re, &tag, &attrs)) {
      if (VERBOSE)
        Print("Matched tag_re [{}]\n", tag);
      // Enter new tag.
      tag = Util::lcase(tag);
      HTMLNode node;
      node.is_tag = true;
      node.str = {tag};

      re2::StringPiece a(attrs);
      std::string attr, value;
      while (RE2::Consume(&a, attr_re, &attr, &value)) {
        if (VERBOSE) {
          Print("Attr: {}\n", attr);
        }
        // TODO: unescape attribute
        attr = Util::lcase(attr);
        node.attrs[attr] = value;
      }

      (void)RE2::Consume(&a, no_more_attrs_re);
      if (!a.empty()) {
        if (parse_error != nullptr) {
          *parse_error =
            std::format("Something weird after attrs: [{}]",
                        std::string(a));
        }
        return {};
      }

      if (VoidTags().contains(tag)) {
        tag_stack.back().children.push_back(std::move(node));
      } else {
        tag_stack.push_back(std::move(node));
      }
    } else if (RE2::Consume(&in, end_tag_re, &tag)) {
      tag = Util::lcase(tag);

      // Close tags to match
      for (;;) {
        if (tag_stack.size() < 2) {
          if (parse_error != nullptr) {
            *parse_error = "mismatched tags";
          }
          return {};
        }

        const bool tag_matches = tag_stack.back().is_tag &&
          tag_stack.back().str == tag;

        // We pop it either way.
        HTMLNode node = std::move(tag_stack.back());
        tag_stack.pop_back();
        CHECK(!tag_stack.empty());
        tag_stack.back().children.push_back(std::move(node));

        if (tag_matches)
          break;
      }
    } else if (in[0] == '<') {
      AddText("<");
      in.remove_prefix(1);
    }
  }

  if (VERBOSE) {
    Print("Ended with tag stack {}\n", tag_stack.size());
  }

  if (tag_stack.size() != 1) {
    if (parse_error != nullptr) {
      *parse_error = "some tags were never closed";
    }
    return {};
  }

  if (VERBOSE)
    Print("Returning {} children.\n",
          tag_stack.back().children.size());
  return std::move(tag_stack.back().children);
}

void HTML::DebugPrint(const std::vector<HTMLNode> &nodes) {
  std::function<void(const HTMLNode &, int)> Rec =
    [&Rec](const HTMLNode &node, int depth) {
      std::string pad(depth * 2, ' ');
      if (node.is_tag) {
        Print("{}" ABLUE("<") "{} attrs..." ABLUE(">") "\n",
              pad, node.str);
        for (const HTMLNode &child : node.children) {
          Rec(child, depth + 1);
        }
        Print("{}" ABLUE("</") "{}" ABLUE(">") "\n",
              pad, node.str);
      } else {
        Print("{}{}\n", pad, node.str);
        if (!node.attrs.empty() && !node.children.empty()) {
          Print("{}" ARED("(with illegal children)") "\n", pad);
        }
      }
    };
  for (const HTMLNode &node : nodes) {
    Rec(node, 0);
  }
}
