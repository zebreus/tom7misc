
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
      const Type *u = t->EVar().GetBound();       \
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

#define RunInternal(pgm, simp) ([&front]() {        \
    const std::string source = (pgm);               \
    Frontend::Options options;                      \
    options.simplify = (simp);                      \
    const Exp *e = front.RunFrontendOn(             \
        StringPrintf("Test %s (%s:%d)",             \
                     __func__, __FILE__, __LINE__), \
        source,                                     \
        options);                                   \
    CHECK(e != nullptr) << "Rejected: " << source;  \
    return e;                                       \
  }())

#define Run(pgm) RunInternal(pgm, true)
#define RunNoSimplify(pgm) RunInternal(pgm, false)

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
    const Exp *e = RunNoSimplify("ref 7 : int ref");
    const auto &[f, arg] = e->App();
    CHECK(arg->Integer() == 7);
    const auto &[self, x, body] = f->Fn();
    CHECK(self.empty());
    const auto &[po, ts, es] = body->Primop();
    CHECK(po == Primop::REF);
    CHECK(ts.size() == 1);
    CHECK_TYPETYPE(ts[0], TypeType::INT);
    CHECK(es.size() == 1);

    // Since the primop has arity 1, the argument is used
    // directly (not a tuple).
    const auto &[tv, xx] = es[0]->Var();
    CHECK(tv.empty());
    CHECK(xx == x);
  }

  {
    // With simplification.
    const Exp *e = Run("ref 7 : int ref");
    const auto &[valdecs, body] = e->Let();
    // This will simplify further soon..
    CHECK(valdecs.size() == 1);
    (void)valdecs[0]->Val();
    const auto &[po, ts, es] = body->Primop();
    CHECK(po == Primop::REF);
    CHECK(ts.size() == 1);
    CHECK_TYPETYPE(ts[0], TypeType::INT);
    CHECK(es.size() == 1);
  }

  {
    const Exp *e = RunNoSimplify("3 + 4");
    const auto &[f, arg] = e->App();
    const auto &str_children = arg->Record();
    CHECK(str_children.size() == 2);
    // A record with labels "1" and "2".
    CHECK(str_children[0].first == "1");
    CHECK(str_children[1].first == "2");
    CHECK(str_children[0].second->Integer() == 3);
    CHECK(str_children[1].second->Integer() == 4);

    // The function should apply a primop to projections
    // from the tuple.
    const auto &[self, x, body] = f->Fn();
    CHECK(self.empty()) << "Not recursive.";
    const auto &[po, ts, es] = body->Primop();
    CHECK(ts.empty()) << "Plus should take no type args.";
    CHECK(es.size() == 2);
    const auto &[l1, e1] = es[0]->Project();
    const auto &[l2, e2] = es[1]->Project();
    CHECK(l1 == "1");
    CHECK(l2 == "2");
    CHECK(std::get<1>(e1->Var()) == x);
    CHECK(std::get<1>(e2->Var()) == x);
  }

  {
    const Exp *e = RunNoSimplify("{lab = 0, 2=\"hi\"}");
    const auto &str_children = e->Record();
    CHECK(str_children.size() == 2);
    CHECK(str_children[0].first == "lab");
    CHECK(str_children[0].second->Integer() == 0);
    CHECK(str_children[1].first == "2");
    CHECK(str_children[1].second->String() == "hi");
    printf("... %s\n", ExpString(e).c_str());
  }


  {
    const Exp *e = RunNoSimplify("let val x = 3 in x end");
    const auto &[decs, body] = e->Let();
    CHECK(decs.size() == 1);
    printf("... %s\n", ExpString(e).c_str());
  }

  {
    const Exp *e = RunNoSimplify("let val (x, y) = (7, \"hi\") in x end");
    const auto &[decs, body] = e->Let();
    // Should bind tuple, and the two vars.
    CHECK(decs.size() == 3);
    printf("%s\n", ExpString(e).c_str());
  }

  {
    const Exp *e = RunNoSimplify(
        "let val (x as z, _) = (7, \"hi\") in x end");
    const auto &[decs, body] = e->Let();
    printf("%s\n", ExpString(e).c_str());
    const auto &[tv, body_var] = body->Var();
    CHECK(tv.empty()) << "Should not be polymorphic!";
    bool is_declared = false;
    for (const Dec *dec : decs) {
      if (dec->type == DecType::VAL &&
          std::get<1>(dec->Val()) == body_var)
        is_declared = true;
    }
    CHECK(is_declared) << body_var;
  }

  {
    const Exp *e = RunNoSimplify("fn x => x");
    const auto &[self, x, body] = e->Fn();
    printf("%s\n", ExpString(body).c_str());
  }

  // TODO: Doesn't work yet because fn is translated as recursive
  if (false)
  {
    const Exp *e = Run("(fn x => x) 0");
    // After simplification, we have
    // let x = 0
    // in x
    // end
    // (Although this will simplify further!)
    (void)e->Let();
  }

  {
    const Exp *e = Run("case 7 of x => x");
    printf("%s\n", ExpString(e).c_str());
  }

  {
    const Exp *e = Run("1e100");
    double d = e->Float();
    CHECK(d == 1e100);
  }

  {
    const Exp *e = Run("2.25");
    double d = e->Float();
    CHECK(d == 2.25);
  }

  {
    const Exp *e = Run("let datatype (a) option = SOME of a | NONE of {}\n"
                       "in 7\n"
                       "end");
    // Datatype declarations are transparent, so this should just be the
    // body.
    CHECK(e->Integer() == 7);
  }

  {
    const Exp *e = Run("let datatype (a) option = SOME of a | NONE of {}\n"
                       "in SOME 7\n"
                       "end");
    printf("%s\n", ExpString(e).c_str());
  }

  {
    const Exp *e = Run("let\n"
                       "   datatype dir = Up of {} | Down of {}\n"
                       "   val x = 9\n"
                       "   val f = fn x => x\n"
                       "in f 7\n"
                       "end");
    printf("%s\n", ExpString(e).c_str());
  }

}

}  // il

int main(int argc, char **argv) {
  ANSI::Init();

  il::Simple();

  printf("OK\n");
  return 0;
}
