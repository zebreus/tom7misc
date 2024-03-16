
#include "compiler.h"

#include <string>
#include <utility>

#include "frontend.h"
#include "closure-conversion.h"
#include "bytecode.h"
#include "simplification.h"
#include "base/logging.h"
#include "ansi.h"

Compiler::Compiler() : closure_conversion(frontend.Pool()),
                       flatten_globals(frontend.Pool()) {

}

bc::Program Compiler::Compile(const std::string &filename,
                              Options options) {
  il::Program pgm = frontend.RunFrontend(filename, options.frontend_options);
  return InternalGuts(std::move(pgm));
}

bc::Program Compiler::CompileString(const std::string &error_context,
                                    const std::string &source,
                                    Options options) {
  il::Program pgm = frontend.RunFrontendOn(error_context, source,
                                           options.frontend_options);
  return InternalGuts(std::move(pgm));
}

bc::Program Compiler::InternalGuts(il::Program pgm_in) {
  il::Program il_pgm = std::move(pgm_in);

  if (verbose > 1) {
    printf("\n\n" AWHITE("Closure convert this") ":\n"
           "%s\n\n", il::ProgramString(il_pgm).c_str());
    fflush(stdout);
  }

  il_pgm = closure_conversion.Convert(il_pgm);

  if (verbose > 1) {
    printf("\n\n" AWHITE("Simplify (post closure-conversion) this") ":\n"
           "%s\n\n", il::ProgramString(il_pgm).c_str());
  }

  il::Simplification simplification(frontend.Pool());
  // Need to remove some constructs before converting to bytecode.
  constexpr uint64_t DECOMPOSE = il::Simplification::O_DECOMPOSE_INTCASE |
    il::Simplification::O_DECOMPOSE_STRINGCASE;
  il_pgm = simplification.Simplify(il_pgm, DECOMPOSE);

  il_pgm = simplification.Simplify(il_pgm,
                                   il::Simplification::O_CONSERVATIVE &
                                   il::ClosureConversion::SimplificationOpts());

  if (verbose > 2) {
    printf("\n\n" AWHITE("Flatten this") ":\n"
           "%s\n\n", il::ProgramString(il_pgm).c_str());
    fflush(stdout);
  }

  il_pgm = flatten_globals.Flatten(il_pgm);

  if (verbose > 1) {
    printf("\n\n" AWHITE("Convert this to bytecode") ":\n"
           "%s\n\n", il::ProgramString(il_pgm).c_str());
    fflush(stdout);
  }

  bc::Program bc_pgm = to_bytecode.Convert(il_pgm);
  return bc_pgm;
}
