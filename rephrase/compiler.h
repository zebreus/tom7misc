
#ifndef _REPHRASE_COMPILER_H
#define _REPHRASE_COMPILER_H

#include "frontend.h"
#include "closure-conversion.h"
#include "bytecode.h"

// This is a fairly simple wrapper that pairs together the frontend and
// later passes. It can produce bytecode from source files.
struct Compiler {
  Frontend frontend;

  il::ClosureConversion closure_conversion;

  Compiler();

  struct Options {
    Frontend::Options frontend_options;
  };

  bc::Program Compile(const std::string &filename,
                      Options options = Options());

  // Mainly for testing.
  bc::Program CompileString(const std::string &error_context,
                            const std::string &source,
                            Options options = Options());

 private:
  bc::Program InternalGuts(il::Program &&pgm);

};

#endif
