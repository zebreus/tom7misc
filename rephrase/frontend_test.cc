
#include "frontend.h"

#include <string>

#include "ansi.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "il.h"
#include "bignum/big-overloads.h"


static constexpr bool VERBOSE = true;

namespace il {

static void Simple() {
  Frontend front;
  if (VERBOSE) {
    front.SetVerbose(1);
  }

#define Run(pgm) ([&front]() {                      \
    const std::string source = (pgm);               \
    const Exp *e = front.RunFrontendOn(         \
        StringPrintf("Test %s (%s:%d)",             \
                     __func__, __FILE__, __LINE__), \
        source);                                    \
    CHECK(e != nullptr) << "Rejected: " << source;  \
    return e;                                       \
  }())

  {
    const Exp *e = Run("42");
    CHECK(e->Integer() == 42);
  }

  {
    const Exp *e = Run("42 : int");
    CHECK(e->Integer() == 42);
  }

  {
    const Exp *e = Run("\"hi\" : string");
    CHECK(e->String() == "hi");
  }

  {
    const Exp *e = Run("ref 7 : int ref");
    CHECK(e != nullptr);
    // ...
  }

}

}  // il

int main(int argc, char **argv) {
  ANSI::Init();

  il::Simple();

  printf("OK\n");
  return 0;
}
