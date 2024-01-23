
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

private:
  int verbose = 0;
  std::vector<std::string> includepaths;
  el::AstPool el_pool;
  il::AstPool il_pool;
};

#endif
