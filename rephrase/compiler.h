
#ifndef _REPHRASE_COMPILER_H
#define _REPHRASE_COMPILER_H

#include <string>

#include "assembling.h"
#include "bc.h"
#include "closure-conversion.h"
#include "flatten-globals.h"
#include "frontend.h"
#include "il.h"
#include "to-bytecode.h"

// This is a fairly simple wrapper that pairs together the frontend and
// later passes. It can produce bytecode from source files.
struct Compiler {
  Frontend frontend;

  il::ClosureConversion closure_conversion;
  il::FlattenGlobals flatten_globals;
  bc::ToBytecode to_bytecode;
  bc::Assembling assembling;

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

  // Compiler itself. Can enable verbosity for phases by calling
  // e.g. frontend.SetVerbose().
  void SetVerbose(int v) { verbose = v; }

 private:
  int verbose = 0;
  bc::Program InternalGuts(il::Program pgm);
};

#endif
