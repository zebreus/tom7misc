
#include "closure-conversion.h"

#include <cstdio>
#include <string>

#include "il.h"
#include "frontend.h"
#include "simplification.h"

#include "base/stringprintf.h"
#include "base/logging.h"
#include "ansi.h"
#include "bignum/big-overloads.h"

static constexpr bool VERBOSE = false;

#define RunInternal(src, post_simplify)                 \
  ([&compiler](const std::string source) -> Program {   \
      if (VERBOSE) {                                    \
        printf(                                         \
            ABGCOLOR(200, 0, 200,                       \
                     AFGCOLOR(0, 0, 0, "TEST:"))        \
            "\n%s\n", source.c_str());                  \
      }                                                 \
      Frontend::Options options;                        \
      options.simplify = true;                          \
      const Program pgm =                               \
        compiler.frontend.RunFrontendOn(                \
            StringPrintf("Test %s (%s:%d)",             \
                         __func__, __FILE__, __LINE__), \
            source,                                     \
            options);                                   \
      CHECK(pgm.body != nullptr) <<                     \
        "Frontend Rejected: " << source;                \
      Program cc_pgm = compiler.closure_conversion.     \
        Convert(pgm);                                   \
      if (post_simplify) {                              \
        cc_pgm = compiler.simplification.Simplify(      \
            cc_pgm,                                     \
            ClosureConversion::SimplificationOpts());   \
      }                                                 \
      return cc_pgm;                                    \
    }(src))

#define Run(src) RunInternal(src, true)

namespace il {

struct Compiler {
  Frontend frontend;
  ClosureConversion closure_conversion;
  Simplification simplification;

  Compiler() : closure_conversion(frontend.Pool()),
               simplification(frontend.Pool()) {
    if (VERBOSE) {
      closure_conversion.SetVerbose(1);
    }
  }
};

static void TestCC() {
  {
    Compiler compiler;
    const Program pgm = Run("7");
    CHECK(pgm.globals.empty());
    CHECK(pgm.body->Int() == 7);
  }

  {
    Compiler compiler;
    const Program pgm = Run("(fn x => 7)");
    if (VERBOSE) {
      printf("%s", ProgramString(pgm).c_str());
    }
  }

}

}  // namespace il

int main(int argc, char **argv) {
  ANSI::Init();

  il::TestCC();

  printf("CC tests OK\n");
  return 0;
}
