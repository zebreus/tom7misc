
#include "compiler.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>

#include "ansi.h"
#include "assembling.h"
#include "base/logging.h"
#include "base/print.h"
#include "bc.h"
#include "closure-conversion.h"
#include "frontend.h"
#include "il.h"
#include "optimization.h"
#include "simplification.h"
#include "timer.h"

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
    Print("\n\n" AWHITE("Closure convert this") ":\n"
          "{}\n\n", il::ProgramString(il_pgm));
    fflush(stdout);
  }

  il_pgm = closure_conversion.Convert(il_pgm);

  if (verbose > 1) {
    Print("\n\n" AWHITE("Simplify (post closure-conversion) this") ":\n"
          "{}\n\n", il::ProgramString(il_pgm));
  }

  il::Simplification simplification(frontend.Pool());
  // Need to remove some constructs before converting to bytecode.
  constexpr uint64_t DECOMPOSE = il::Simplification::O_DECOMPOSE_INTCASE |
    il::Simplification::O_DECOMPOSE_WORDCASE |
    il::Simplification::O_DECOMPOSE_STRINGCASE;
  il_pgm = simplification.Simplify(il_pgm, DECOMPOSE);

  il_pgm = simplification.Simplify(il_pgm,
                                   il::Simplification::O_CONSERVATIVE &
                                   il::ClosureConversion::SimplificationOpts());

  if (verbose > 2) {
    Print("\n\n" AWHITE("Flatten this") ":\n"
          "{}\n\n", il::ProgramString(il_pgm));
    fflush(stdout);
  }

  il_pgm = flatten_globals.Flatten(il_pgm);

  if (verbose > 1) {
    Print("\n\n" AWHITE("Convert this to bytecode") ":\n"
          "{}\n\n", il::ProgramString(il_pgm));
    fflush(stdout);
  }

  bc::SymbolicProgram bc_symbolic_pgm = to_bytecode.Convert(il_pgm);

  if (verbose > 0) {
    Print("\n" AWHITE("Optimizing...") "\n");

    if (verbose > 1) {
      Print("\n\n" AWHITE("Optimize this") ":\n");
      bc::PrintSymbolicProgram(bc_symbolic_pgm);
      Print("\n");
      fflush(stdout);
    }
  }

  Timer optimize_timer;
  bc::Optimization optimization;
  optimization.SetVerbose(verbose);
  bc_symbolic_pgm = optimization.Optimize(bc_symbolic_pgm);
  const double optimize_sec = optimize_timer.Seconds();

  if (verbose > 0) {
    Print(AWHITE("Optimized") " in {}:\n",
          ANSI::Time(optimize_sec));
  }

  if (verbose > 1) {
    Print("\n\n" AWHITE("Assemble this") ":\n");
    bc::PrintSymbolicProgram(bc_symbolic_pgm);
    Print("\n");
    fflush(stdout);
  }

  bc::Program bc_pgm = assembling.Assemble(bc_symbolic_pgm);

  if (verbose > 0) {
    if (verbose > 1) {
      Print("\n\n" AWHITE("Bytecode") ":\n");
      bc::PrintProgram(bc_pgm);
      Print("\n");
    }
    const auto &[data_bytes, total_insts] = bc::ProgramSize(bc_pgm);
    Print("Program size: " ABLUE("{}") " bytes data, "
          APURPLE("{}") " insts.\n", data_bytes, total_insts);
    fflush(stdout);
  }

  return bc_pgm;
}
