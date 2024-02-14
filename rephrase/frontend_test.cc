
#include "frontend.h"

#include <string>
#include <unordered_set>

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
    // Simplifier should be able to make this into
    // a direct primop application.
    const auto &[po, ts, es] = e->Primop();
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
    const auto &[tyvars, x, rhs, body] = e->Let();
    CHECK(tyvars.empty());
    printf("... %s\n", ExpString(e).c_str());
  }

  {
    const Exp *e = RunNoSimplify("let val (x, y) = (7, \"hi\") in x end");
    if (VERBOSE) {
      printf("%s\n", ExpString(e).c_str());
    }
    // Should bind tuple, and the two vars.
    const auto &[tyvars, x, rhs, body] = e->Let();
    CHECK(tyvars.empty());
    CHECK(body->type == ExpType::LET);
    const Exp *body2 = std::get<3>(body->Let());
    CHECK(body2->type == ExpType::LET);
    const Exp *body3 = std::get<3>(body2->Let());
    CHECK(body3->type == ExpType::VAR);
  }

  {
    const Exp *e = RunNoSimplify(
        "let val (x as z, _) = (7, \"hi\") in x end");
    if (VERBOSE) {
      printf("%s\n", ExpString(e).c_str());
    }
    const auto &[tyvars, x, rhs, body] = e->Let();
    CHECK(tyvars.empty()) << "Should not be polymorphic!";

    std::unordered_set<std::string> declared;
    while (e->type == ExpType::LET) {
      const auto &[tyvars_, xx, rhs_, bbody] = e->Let();
      declared.insert(xx);
      e = bbody;
    }
    CHECK(e->type == ExpType::VAR);
    const std::string v = std::get<1>(e->Var());
    CHECK(declared.contains(v)) << v;
  }

  {
    const Exp *e = RunNoSimplify("fn x => x");
    const auto &[self, x, body] = e->Fn();
    if (VERBOSE) {
      printf("%s\n", ExpString(body).c_str());
    }
  }

  {
    const Exp *e = Run("(fn x => x) 7");
    // After simplification, we have
    // let x = 7
    // in x
    // end
    CHECK(e->Integer() == 7) << "Should be able to simplify this to "
      "a let and then just an integer. Tests making a function not "
      "recursive.";
  }

  {
    const Exp *e = Run("case 7 of x => x");
    CHECK(e->Integer() == 7) << "Should be able to simplify this "
      "to just an integer.";
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
    CHECK(e->type == ExpType::ROLL);
    // roll<(μ opt. [NONE: {}, SOME: int])>([SOME = 7])
    const auto &[t, body] = e->Roll();
    CHECK(t->type == TypeType::MU);
    const auto &[lab, bbody] = body->Inject();
    CHECK(bbody->Integer() == 7);
    CHECK(lab == "SOME");
  }

  {
    const Exp *e = Run("let\n"
                       "   datatype dir = Up of {} | Down of {}\n"
                       "   val x = 9\n"
                       "   val f = fn x => x\n"
                       "in f 7\n"
                       "end");
    CHECK(e->type == ExpType::INTEGER) << "Should be able to simplify "
      "this to just 7 because the bindings are trivially "
      "inlinable. Tests polymorphic inlining.";
  }

  {
    const Exp *e = Run("let\n"
                       "  val x = 7\n"
                       "in\n"
                       "  (x, x)\n"
                       "end\n");
    CHECK(e->type == ExpType::RECORD) << "Should be able to simplify "
      "this to the record (7, 7) since integers are small values.";
    const auto &labe = e->Record();
    CHECK(labe[0].first == "1");
    CHECK(labe[0].second->Integer() == 7);
    CHECK(labe[1].first == "2");
    CHECK(labe[1].second->Integer() == 7);
  }

  if (false) {
    const Exp *e = Run("let\n"
                       "  val id = fn x => x\n"
                       "  val x = 7\n"
                       "in\n"
                       "  (fn x, fn x)\n"
                       "end\n");
    CHECK(e->type == ExpType::RECORD) << "Should be able to simplify "
      "this to (7, 7) by inlining into application positions where "
      "the argument and body are small enough. (Unimplemented! And "
      "perhaps this should just be part of a proper optimization pass.)";
    const auto &labe = e->Record();
    CHECK(labe[0].first == "1");
    CHECK(labe[0].second->Integer() == 7);
    CHECK(labe[1].first == "2");
    CHECK(labe[1].second->Integer() == 7);
  }

  {
    const Exp *e = Run("case 7 of x => x | y => 8 | z => 9");
    CHECK(e->Integer() == 7) << "Should be able to simplify this "
      "to just an integer.";
  }

  {
    const Exp *e = Run("case (7, 7) of (x, y) => x");
    CHECK(e->Integer() == 7);
  }

  {
    const Exp *e = Run("case {d = (2, 7), a = 3, c = \"hi\"} of\n"
                       "  {d = (_, x), a = a, c = _} => x\n");
    CHECK(e->Integer() == 7);
  }

  {
    const Exp *e = Run("case 2 of\n"
                       "   1 => 111\n"
                       " | 2 => 222\n"
                       " | 3 => 333\n"
                       " | _ => 666");
    CHECK(e->Integer() == 222);
  }

  {
    const Exp *e = Run("case (1, 2, 3) of\n"
                       "   (1, 2, 3) => 7\n"
                       " | _ => 666\n");
    // TODO: Optimize this so that we know it's just 7?
    printf("%s", ExpString(e).c_str());
  }

  {
    const Exp *e = Run("case \"hello\" of\n"
                       "   \"world\" => 1234\n"
                       " | \"hello\" => 7\n"
                       " | _ => 9\n");
    CHECK(e->Integer() == 7);
  }

}

}  // il

int main(int argc, char **argv) {
  ANSI::Init();

  il::Simple();

  printf("OK\n");
  return 0;
}
