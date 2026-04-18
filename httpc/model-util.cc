
#include "model-util.h"

#include <cctype>
#include <cstdlib>
#include <format>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "base/logging.h"
#include "process-util.h"
#include "util.h"

static void ConsumeWS(std::string_view *s) {
  while (!s->empty() && std::isspace((*s)[0])) {
    s->remove_prefix(1);
  }
}

std::vector<std::string> ModelUtil::SvnList(std::string_view dir) {
  // "svn list" uses the server's version of the files at
  // some revision, which might not be the newest one. This
  // is not what we want.
  //
  // "svn status -vq" will show everything we want, but we
  // have to remove some stuff to find the filename:
  // M             6997     6996 tom7         makefile

  std::string cmd = std::format("svn st -vq \"{}\"", dir);
  std::optional<std::string> out = ProcessUtil::GetOutput(cmd);
  CHECK(out.has_value()) << "Command failed (is svn in your PATH?): " << cmd;

  // Parse.
  std::vector<std::string> ret;
  for (std::string_view line : Util::SplitToLines(out.value())) {
    if (line.empty())
      continue;

    CHECK(line.size() > 8) << "Must have at least the 8 status chars?";
    line.remove_prefix(8);

    ConsumeWS(&line);

    // e.g. "> moved from original.cc"
    if (line.empty() || line[0] == '>')
      continue;

    // Now two columns of revision numbers or "-" or "?".
    auto OKChar = [](char c) {
        return std::isdigit(c) || c == '-' || c == '?';
      };

    while (!line.empty() && OKChar(line[0])) line.remove_prefix(1);
    ConsumeWS(&line);

    while (!line.empty() && OKChar(line[0])) line.remove_prefix(1);
    ConsumeWS(&line);

    // Now looking at username.
    while (!line.empty() && !std::isspace(line[0])) {
      line.remove_prefix(1);
    }
    ConsumeWS(&line);

    CHECK(!line.empty()) << "Line didn't contain filename?";

    // Don't include the directory itself.
    if (line != dir) {
      // svn st will already include the path. But use minimal
      // relative filenames when it's right here.
      if (dir == "." && Util::StartsWith(line, "./")) {
        line.remove_prefix(2);
      }
      ret.emplace_back(line);
    }
  }
  return ret;
}

// Gemini likes to wrap json output in markdown.
std::string ModelUtil::StripMarkup(std::string_view json) {
  Util::RemoveOuterWhitespace(&json);
  if (json.starts_with("```json")) {
    json.remove_prefix(7);
  } else if (json.starts_with("```")) {
    json.remove_prefix(3);
  }

  if (json.ends_with("```")) {
    json.remove_suffix(3);
  }

  return std::string(json);
}

// Return true if the string could be json. Checks:
//  - we have balanced curly braces (skipping over
//    string literals of course) and square brackets.
//  - string literals are closed.
//  - There aren't illegal characters.
//
// Permissive. Allows single-quoted values and unquoted property
// names, for example.
bool ModelUtil::IsBalancedJSON(std::string_view s) {
  std::string stack;

  while (!s.empty()) {
    size_t pos = s.find_first_of("\"'{}[]\\");
    if (pos == std::string_view::npos) {
      break;
    }

    char c = s[pos];
    s.remove_prefix(pos + 1);

    switch (c) {
    case '{':
    case '[':
      stack.push_back(c);
      break;
    case '}':
      if (stack.empty() || stack.back() != '{')
        return false;
      stack.pop_back();
      break;
    case ']':
      if (stack.empty() || stack.back() != '[')
        return false;
      stack.pop_back();
      break;
    case '\\':
      return false;

    case '"':
    case '\'': {
      bool closed = false;
      std::string_view search = (c == '"') ? "\"\\" : "'\\";
      while (!s.empty()) {
        size_t end = s.find_first_of(search);
        if (end == std::string_view::npos) {
          return false;
        }
        if (s[end] == '\\') {
          if (end + 1 >= s.size()) return false;
          s.remove_prefix(end + 2);
        } else {
          s.remove_prefix(end + 1);
          closed = true;
          break;
        }
      }
      if (!closed) return false;
      break;
    }

    default:
      LOG(FATAL) << "Bug";
      break;
    }
  }

  return stack.empty();
}

std::optional<std::string> ModelUtil::FindOneJSONObject(
    std::string_view response) {
  // First, prefer content from the first "```json"
  // block to the last "```".
  size_t json_start = response.find("```json");
  if (json_start != std::string_view::npos) {
    size_t json_end = response.rfind("```");
    if (json_end != std::string_view::npos && json_end > json_start + 7) {
      std::string_view candidate = response.substr(
          json_start + 7, json_end - (json_start + 7));
      Util::RemoveOuterWhitespace(&candidate);
      if (!candidate.empty() && IsBalancedJSON(candidate)) {
        return std::string(candidate);
      }
    }
  }

  // Fallback: extract from the first '{' or '[' to the last
  // '}' or ']'. This effectively skips preamble and postamble text.
  size_t open = response.find_first_of("{[");
  if (open != std::string_view::npos) {
    size_t close = response.find_last_of("}]");
    if (close != std::string_view::npos && close > open) {
      std::string_view candidate = response.substr(
          open, close - open + 1);
      if (!candidate.empty() && IsBalancedJSON(candidate)) {
        return std::string(candidate);
      }
    }
  }

  return std::nullopt;
}

std::string ModelUtil::GetAPIKey() {
  // First, check if the GEMINI_API_KEY environment variable is
  // set, and use that if so.
  if (const char* env_key = std::getenv("GEMINI_API_KEY")) {
    std::string api_key = Util::NormalizeWhitespace(env_key);
    if (!api_key.empty()) {
      return api_key;
    }
  }

  std::string api_key =
    Util::NormalizeWhitespace(Util::ReadFile("d://tom//GEMINI_API_KEY"));
  CHECK(!api_key.empty());
  return api_key;
}

std::set<std::string> ModelUtil::IncludeDirs(
    std::string_view seed_file) {
  std::set<std::string> dirs;
  // Try finding a .clangd file in the same directory as the
  // seed_file.

  std::string current_dir = ".";
  size_t last_slash = seed_file.find_last_of("/\\");
  if (last_slash != std::string::npos) {
    current_dir = seed_file.substr(0, last_slash);
  }

  std::string clangd_contents = Util::ReadFile(
      Util::DirPlus(current_dir, ".clangd"));

  // This typically ends with something like
  // CompileFlags:
  // Add: [-xc++, -Wall, --std=c++23, -I., -I../cc-lib, -I../httpv]
  //
  // For simplicity, we just look for any instance of -Idir in the file.

  std::string_view clangd(clangd_contents);

  for (;;) {
    auto pos = clangd.find("-I");
    if (pos == std::string_view::npos)
      break;

    clangd.remove_prefix(pos + 2);
    // Can be -Idir or -I dir
    while (!clangd.empty() && clangd[0] == ' ') clangd.remove_prefix(1);

    auto end_pos = clangd.find_first_of(", ]\n\r\"");
    std::string_view relative = (end_pos == std::string_view::npos)
      ? clangd
      : clangd.substr(0, end_pos);

    if (!relative.empty()) {
      std::string dd =
        Util::NormalizePath(Util::DirPlus(current_dir, relative));
      // Even on windows use /, since this will be easier for the
      // model to understand.
      dirs.insert(Util::Replace(dd, "\\", "/"));
    }
  }

  return dirs;
}
