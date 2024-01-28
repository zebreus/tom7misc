
#ifndef _REPHRASE_FRONTEND_H
#define _REPHRASE_FRONTEND_H

#include <string>
#include <vector>

#include "el.h"
#include "il.h"

struct Frontend {
  Frontend() = default;

  void SetVerbose(int verbose);
  void AddIncludePath(const std::string &s);

  // Load, lex, parse, and elaborate.
  const il::Exp *RunFrontend(const std::string &filename);

  // Mostly for testing: Run the frontend on source code directly.
  // The error context is usually the filename, but it is just
  // used in error messages so it can be anything.
  const il::Exp *RunFrontendOn(const std::string &error_context,
                               const std::string &source);

private:
  int verbose = 0;
  std::vector<std::string> includepaths;
  el::AstPool el_pool;
  il::AstPool il_pool;
};

#endif
