
#ifndef _HTTPC_MODEL_UTIL_H
#define _HTTPC_MODEL_UTIL_H

#include <string_view>
#include <string>
#include <vector>

// C++ Utilities for writing little LLM-based utilities.
struct ModelUtil {
  // List the files that are tracked in the given directory.
  // Returns a vector of relative paths to those files.
  static std::vector<std::string> SvnList(std::string_view dir);


  // Gemini likes to wrap json output in markdown. Strip it if
  // present (heuristic).
  static std::string StripMarkup(std::string_view json);

  // Get the API key. This should be evolved to check environment
  // variables, command-lines, etc.
  static std::string GetAPIKey();

};

#endif
