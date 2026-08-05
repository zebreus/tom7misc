#include "z3.h"

#include <cctype>
#include <cmath>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/logging.h"
#include "crypt/sha256.h"
#include "process-util.h"
#include "util.h"

// Return the content (if successful) and input filename.
static std::pair<std::optional<std::string>, std::string> RunZ3Process(
    std::string_view content, std::optional<double> timeout_seconds) {
  std::string filename =
    std::format("runz3-{}.z3",
                SHA256::Ascii(SHA256::HashStringView(content)));
  Util::WriteFile(filename, content);
  std::string targ;
  if (timeout_seconds.has_value()) {
    // Milliseconds
    targ = std::format("-t:{}", (int)std::round(
                           timeout_seconds.value() * 1000.0));
  }

  std::optional<std::string> z3result =
    ProcessUtil::GetOutput(std::format("d:\\z3\\bin\\z3.exe {} {}",
                                       targ,
                                       filename));
  return {std::move(z3result), std::move(filename)};
}

std::vector<Z3::SExp> Z3::Run(std::string_view content,
                              std::optional<double> timeout_seconds) {
  auto [z3result, filename] = RunZ3Process(content, timeout_seconds);
  CHECK(z3result.has_value()) << "Couldn't run z3?";

  (void)Util::RemoveFile(filename);

  std::string_view result_view = z3result.value();
  std::vector<Z3::SExp> results = ConsumeSExps(&result_view);
  Util::RemoveOuterWhitespace(&result_view);
  CHECK(result_view.empty()) << "Unparsed z3 output:\n" << result_view;

  return results;
}

Z3::SatResult Z3::RunSat(std::string_view content,
                         std::optional<double> timeout_seconds) {
  auto [z3result, filename] = RunZ3Process(content, timeout_seconds);
  // Could return UNKNOWN here, but that would probably be surprising.
  CHECK(z3result.has_value()) << "Couldn't run z3?";

  // Just remember that "sat" is in "unsat"!
  if (Util::StrContains(z3result.value(), "unknown")) {
    (void)Util::RemoveFile(filename);
    return SatResult::UNKNOWN;
  }
  if (Util::StrContains(z3result.value(), "unsat")) {
    (void)Util::RemoveFile(filename);
    return SatResult::UNSAT;
  }
  if (Util::StrContains(z3result.value(), "sat")) {
    (void)Util::RemoveFile(filename);
    return SatResult::SAT;
  }
  LOG(FATAL) << "Unparseable z3 result:\n" << z3result.value() <<
    "On input file: " << filename;
}

static std::string ConsumeAtom(std::string_view *content) {
  size_t len = 0;
  if (content->front() == '"') {
    len = 1;
    while (len < content->size() && (*content)[len] != '"') {
      len++;
    }
    if (len < content->size()) {
      len++;
    }

  } else if (content->front() == '|') {
    len = 1;
    while (len < content->size() && (*content)[len] != '|') {
      len++;
    }
    if (len < content->size()) {
      len++;
    }

  } else {
    while (len < content->size() && !std::isspace((unsigned char)(*content)[len]) &&
           (*content)[len] != '(' && (*content)[len] != ')') {
      len++;
    }
  }

  std::string s{content->substr(0, len)};
  content->remove_prefix(len);
  return s;
}

std::vector<Z3::SExp> Z3::ConsumeSExps(std::string_view *content) {
  std::vector<SExp> stack;
  std::vector<SExp> results;
  std::string_view saved_content = *content;

  while (!content->empty()) {
    while (!content->empty() && std::isspace((unsigned char)content->front())) {
      content->remove_prefix(1);
    }
    if (stack.empty()) {
      saved_content = *content;
    }
    if (content->empty()) {
      break;
    }

    if (content->front() == '(') {
      SExp list_exp;
      list_exp.type = SExp::Type::LIST;
      stack.push_back(std::move(list_exp));
      content->remove_prefix(1);

    } else if (content->front() == ')') {
      if (stack.empty()) {
        break;
      }
      SExp finished = std::move(stack.back());
      stack.pop_back();
      content->remove_prefix(1);

      std::vector<SExp> new_list;
      for (size_t i = 0; i < finished.list.size(); i++) {
        if (finished.list[i].type == SExp::Type::ATOM &&
            finished.list[i].atom.starts_with(":")) {
          std::string key = std::move(finished.list[i].atom);
          std::unique_ptr<SExp> value;
          if (i + 1 < finished.list.size() &&
              !(finished.list[i + 1].type == SExp::Type::ATOM &&
                finished.list[i + 1].atom.starts_with(":"))) {
            value = std::make_unique<SExp>(std::move(finished.list[i + 1]));
            i++;
          }
          finished.attrs.push_back({std::move(key), std::move(value)});
        } else {
          new_list.push_back(std::move(finished.list[i]));
        }
      }
      finished.list = std::move(new_list);

      if (stack.empty()) {
        results.push_back(std::move(finished));
        saved_content = *content;
      } else {
        stack.back().list.push_back(std::move(finished));
      }

    } else {
      SExp atom_exp;
      atom_exp.type = SExp::Type::ATOM;
      atom_exp.atom = ConsumeAtom(content);

      if (stack.empty()) {
        results.push_back(std::move(atom_exp));
        saved_content = *content;
      } else {
        stack.back().list.push_back(std::move(atom_exp));
      }
    }
  }

  *content = saved_content;
  return results;
}

std::optional<Z3::SExp> Z3::ParseSExp(std::string_view content) {
  std::vector<SExp> results = ConsumeSExps(&content);
  Util::RemoveOuterWhitespace(&content);
  if (results.size() != 1 || !content.empty()) {
    return std::nullopt;
  }
  return std::move(results.front());
}

std::string Z3::ToString(const SExp &exp) {
  std::string result;

  struct StackItem {
    const SExp *exp = nullptr;
    size_t index = 0;
  };

  std::vector<StackItem> stack;
  stack.push_back({&exp, 0});

  while (!stack.empty()) {
    const SExp *current_exp = stack.back().exp;
    size_t index = stack.back().index;

    if (current_exp->type == SExp::Type::ATOM) {
      result += current_exp->atom;
      stack.pop_back();
    } else {
      if (index == 0) {
        result.push_back('(');
      }

      if (index < current_exp->list.size()) {
        if (index > 0) {
          result.push_back(' ');
        }
        stack.back().index++;
        stack.push_back({&current_exp->list[index], 0});
      } else {
        size_t attr_idx = index - current_exp->list.size();
        if (attr_idx < current_exp->attrs.size()) {
          if (index > 0) {
            result.push_back(' ');
          }
          result += current_exp->attrs[attr_idx].first;
          stack.back().index++;
          if (current_exp->attrs[attr_idx].second.get() != nullptr) {
            result.push_back(' ');
            stack.push_back({current_exp->attrs[attr_idx].second.get(), 0});
          }
        } else {
          result.push_back(')');
          stack.pop_back();
        }
      }
    }
  }

  return result;
}
