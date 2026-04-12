
#ifndef _HTTPC_MODEL_UTIL_H
#define _HTTPC_MODEL_UTIL_H

#include <optional>
#include <string>
#include <string_view>
#include <set>
#include <vector>

// C++ Utilities for writing little LLM-based utilities.
struct ModelUtil {
  // List the files that are tracked in the given directory.
  // Returns a vector of relative paths to those files.
  static std::vector<std::string> SvnList(std::string_view dir);

  // Heuristically find directories that may have relevant files,
  // e.g. by looking in .clangd or makefiles.
  // Works best if seed_file is in the current directory.
  static std::set<std::string> IncludeDirs(std::string_view seed_file);

  // Gemini likes to wrap json output in markdown. Strip it if
  // present (heuristic).
  static std::string StripMarkup(std::string_view json);

  // Get the API key. This should be evolved to check environment
  // variables, command-lines, etc.
  static std::string GetAPIKey();

  // Try to find one JSON object in the response. Some models
  // just love to put a preamble even if you ask only for JSON.
  static std::optional<std::string> FindOneJSONObject(
      std::string_view response);

  // Exposed mostly for testing:
  //
  // Return true if the string could be json. Checks:
  //  - we have balanced curly braces (skipping over
  //    string literals of course) and square brackets.
  //  - string literals are closed.
  //  - There aren't illegal characters.
  //
  // Permissive. Allows single-quoted values and unquoted property
  // names, for example.
  static bool IsBalancedJSON(std::string_view s);

};

#endif
