
#include "compiler.h"

#include <string>

#include "frontend.h"
#include "closure-conversion.h"
#include "bytecode.h"
#include "simplification.h"
#include "base/logging.h"

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

bc::Program Compiler::InternalGuts(il::Program &&pgm_in) {
  il::Program il_pgm = closure_conversion.Convert(pgm_in);
  // TODO: More simplification here.

  il::Simplification simplification(frontend.Pool());
  // Need to remove some constructs before converting to bytecode.
  constexpr uint64_t DECOMPOSE = il::Simplification::O_DECOMPOSE_INTCASE |
    il::Simplification::O_DECOMPOSE_STRINGCASE;
  il_pgm = simplification.Simplify(il_pgm, DECOMPOSE);

  // XXX probably should run simplification again after decompose!
  il_pgm = simplification.Simplify(il_pgm,
                                   il::Simplification::O_CONSERVATIVE &
                                   il::ClosureConversion::SimplificationOpts());

  if (verbose > 2) {
    printf("\n\nFlatten this:\n"
           "%s\n\n", il::ProgramString(il_pgm).c_str());
  }

  il_pgm = flatten_globals.Flatten(il_pgm);

  if (verbose > 1) {
    printf("\n\nConvert this:\n"
           "%s\n\n", il::ProgramString(il_pgm).c_str());
  }

  bc::Program bc_pgm = to_bytecode.Convert(il_pgm);
  return bc_pgm;
}
