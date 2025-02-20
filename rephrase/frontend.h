
#ifndef _REPHRASE_FRONTEND_H
#define _REPHRASE_FRONTEND_H

#include <string>
#include <vector>

#include "el.h"
#include "il.h"

struct SourceMap;
namespace el { struct Token; }

struct Frontend {
  Frontend() = default;

  void SetVerbose(int verbose);
  void AddIncludePath(const std::string &s);

  struct Options {
    Options() {}
    bool simplify = true;
  };

  // Load, lex, parse, and elaborate.
  il::Program RunFrontend(const std::string &filename,
                          Options options = Options());

  // Mostly for testing: Run the frontend on source code directly.
  // The error context is usually the filename, but it is just
  // used in error messages so it can be anything.
  il::Program RunFrontendOn(const std::string &error_context,
                            const std::string &source,
                            Options options = Options());

  il::AstPool *Pool() { return &il_pool; }

  const std::vector<std::string> &GetIncludePaths() const {
    return includepaths;
  }

 private:

  il::Program RunFrontendInternal(
    const std::string &contents,
    const std::vector<el::Token> &tokens,
    const SourceMap &source_map,
    Options options);

  int verbose = 0;
  std::vector<std::string> includepaths;
  el::AstPool el_pool;
  il::AstPool il_pool;
};

#endif
