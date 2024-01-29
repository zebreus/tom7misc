
#include "frontend.h"

#include <string>

#include "ansi.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "il.h"
#include "bignum/big-overloads.h"


static constexpr bool VERBOSE = true;

namespace il {


#define CHECK_TYPETYPE(t_, tt_) do {              \
    const TypeType tt = (tt_);                    \
    const Type *orig_t = (t_);                    \
    const Type *t = orig_t;                       \
    while (t->type == TypeType::EVAR) {           \
      const Type *u = t->evar.GetBound();         \
      CHECK(u != nullptr) << "Unbound evar: "     \
                          << TypeString(orig_t)   \
                          << "\nWanted: "         \
                          << TypeTypeString(tt);  \
      t = u;                                      \
    }                                             \
    CHECK(t->type == tt) <<                       \
      TypeString(orig_t) << "\nBut wanted: " <<   \
      TypeTypeString(t->type);                    \
  } while (0)

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
    const auto &[f, arg] = e->App();
    CHECK(arg->Integer() == 7);
    const auto &[self, x, body] = f->Fn();
    CHECK(self.empty());
    const auto &[po, ts, es] = body->Primop();
    CHECK(po == Primop::REF);
    CHECK(ts.size() == 1);
    CHECK_TYPETYPE(ts[0], TypeType::INT);
    // CHECK(es.size() == 1);
  }

}

}  // il

int main(int argc, char **argv) {
  ANSI::Init();

  il::Simple();

  printf("OK\n");
  return 0;
}
