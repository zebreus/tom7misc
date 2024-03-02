
#include "compiler.h"

#include <string>

#include "frontend.h"
#include "closure-conversion.h"
#include "bytecode.h"

Compiler::Compiler() : closure_conversion(frontend.Pool()) {

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
  il::Program pgm = closure_conversion.Convert(pgm_in);

  LOG(FATAL) << "Not implemented: bytecode output";
  return {};
}
