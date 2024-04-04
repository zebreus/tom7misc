
#include "frontend.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_set>

#include "ansi.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "bignum/big-overloads.h"
#include "il.h"
#include "primop.h"

// Make all tests verbose.
static constexpr bool VERBOSE = false;

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

#define RunInternal(src, simp)                      \
  ([&front](const std::string source) -> Program {  \
    if (VERBOSE) {                                  \
      printf(                                       \
          ABGCOLOR(200, 0, 200,                     \
                   AFGCOLOR(0, 0, 0, "TEST:"))      \
          "\n%s\n", source.c_str());                \
    }                                               \
    Frontend::Options options;                      \
    options.simplify = (simp);                      \
    const Program pgm = front.RunFrontendOn(        \
        StringPrintf("Test %s (%s:%d)",             \
                     __func__, __FILE__, __LINE__), \
        source,                                     \
        options);                                   \
    CHECK(pgm.body != nullptr) << "Rejected: "      \
                               << source;           \
    return pgm;                                     \
  }(src))

#define Run(src) RunInternal(src, true)
#define RunNoSimplify(src) RunInternal(src, false)

static void TestLiterals() {
  Frontend front;
  if (VERBOSE) {
    front.SetVerbose(1);
  }

  {
    const Program pgm = Run("42");
    CHECK(pgm.body->Int() == 42);
  }

  {
    const Program pgm = Run("42 : int");
    CHECK(pgm.body->Int() == 42);
  }

  {
    const Program pgm = Run("\"hi\" : string");
    CHECK(pgm.body->String() == "hi");
  }

  {
    const Program pgm = RunNoSimplify("{lab = 0, 2=\"hi\"}");
    const auto &str_children = pgm.body->Record();
    CHECK(str_children.size() == 2);
    CHECK(str_children[0].first == "lab");
    CHECK(str_children[0].second->Int() == 0);
    CHECK(str_children[1].first == "2");
    CHECK(str_children[1].second->String() == "hi");
    if (VERBOSE) {
      printf("... %s\n", ExpString(pgm.body).c_str());
    }
  }

  {
    const Program pgm = Run("1e100");
    double d = pgm.body->Float();
    CHECK(d == 1e100);
  }

  {
    const Program pgm = Run("2.25");
    double d = pgm.body->Float();
    CHECK(d == 2.25);
  }

  {
    const Program pgm = Run("true");
    CHECK(pgm.body->Bool() == true);
  }

  {
    const Program pgm = Run("false");
    CHECK(pgm.body->Bool() == false);
  }

}

static void TestPrimops() {
  Frontend front;
  if (VERBOSE) {
    front.SetVerbose(1);
  }

  {
    const Program pgm = RunNoSimplify("ref 7 : int ref");
    const auto &[f, arg] = pgm.body->App();
    CHECK(arg->Int() == 7);
    const auto &[self, x, t, body] = f->Fn();
    const auto &[dom, cod] = t->Arrow();
    CHECK_TYPETYPE(dom, TypeType::INT);
    CHECK_TYPETYPE(cod, TypeType::REF);
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
    const Program pgm = Run("ref 7 : int ref");
    // Simplifier should be able to make this into
    // a direct primop application.
    const auto &[po, ts, es] = pgm.body->Primop();
    CHECK(po == Primop::REF);
    CHECK(ts.size() == 1);
    CHECK_TYPETYPE(ts[0], TypeType::INT);
    CHECK(es.size() == 1);
  }

  {
    const Program pgm = RunNoSimplify("3 + 4");
    const auto &[f, arg] = pgm.body->App();
    const auto &str_children = arg->Record();
    CHECK(str_children.size() == 2);
    // A record with labels "1" and "2".
    CHECK(str_children[0].first == "1");
    CHECK(str_children[1].first == "2");
    CHECK(str_children[0].second->Int() == 3);
    CHECK(str_children[1].second->Int() == 4);

    // The function should apply a primop to projections
    // from the tuple.
    const auto &[self, x, arrow_type, body] = f->Fn();
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

}

static void TestSimplify() {
  Frontend front;
  if (VERBOSE) {
    front.SetVerbose(1);
  }

  {
    const Program pgm = Run("(fn x => x) 7");
    // After simplification, we have
    // let x = 7
    // in x
    // end
    CHECK(pgm.body->Int() == 7) << "Should be able to simplify "
      "this to a let and then just an integer. Tests making a function "
      "not recursive.";
  }

  {
    const Program pgm = Run("case 7 of x => x");
    CHECK(pgm.body->Int() == 7) << "Should be able to simplify this "
      "to just an integer.";
  }

  {
    const Program pgm = Run("let datatype (a) option = SOME of a | NONE\n"
                            "in SOME 7\n"
                            "end");
    CHECK(pgm.body->type == ExpType::ROLL);
    // roll<(μ opt. [NONE: {}, SOME: int])>([SOME = 7])
    const auto &[t, body] = pgm.body->Roll();
    CHECK(t->type == TypeType::MU);
    const auto &[lab, sum_type, bbody] = body->Inject();
    CHECK(sum_type->type == TypeType::SUM);
    CHECK(bbody->Int() == 7);
    CHECK(lab == "SOME");
  }

  {
    const Program pgm = Run("let\n"
                            "   datatype dir = Up | Down\n"
                            "   val x = 9\n"
                            "   val f = fn x => x\n"
                            "in f 7\n"
                            "end");
    CHECK(pgm.body->type == ExpType::INT) << "Should be able "
      "to simplify this to just 7 because the bindings are trivially "
      "inlinable. Tests polymorphic inlining.";
  }

  {
    const Program pgm = Run("let\n"
                            "  val x = 7\n"
                            "in\n"
                            "  (x, x)\n"
                            "end\n");
    CHECK(pgm.body->type == ExpType::RECORD) << "Should be able to "
      "simplify this to the record (7, 7) since integers are small "
      "values.";
    const auto &labe = pgm.body->Record();
    CHECK(labe[0].first == "1");
    CHECK(labe[0].second->Int() == 7);
    CHECK(labe[1].first == "2");
    CHECK(labe[1].second->Int() == 7);
  }

  if (false) {
    const Program pgm = Run("let\n"
                            "  val id = fn x => x\n"
                            "  val x = 7\n"
                            "in\n"
                            "  (fn x, fn x)\n"
                            "end\n");
    CHECK(pgm.body->type == ExpType::RECORD) << "Should be able to simplify "
      "this to (7, 7) by inlining into application positions where "
      "the argument and body are small enough. (Unimplemented! And "
      "perhaps this should just be part of a proper optimization pass.)";
    const auto &labe = pgm.body->Record();
    CHECK(labe[0].first == "1");
    CHECK(labe[0].second->Int() == 7);
    CHECK(labe[1].first == "2");
    CHECK(labe[1].second->Int() == 7);
  }

  {
    const Program pgm = Run("case 7 of x => x | y => 8 | z => 9");
    CHECK(pgm.body->Int() == 7) << "Should be able to simplify this "
      "to just an integer.";
  }

  {
    const Program pgm = Run("case (7, 7) of (x, y) => x");
    CHECK(pgm.body->Int() == 7);
  }

  {
    const Program pgm = Run("case {d = (2, 7), a = 3, c = \"hi\"} of\n"
                            "  {d = (_, x), a = a, c = _} => x\n");
    CHECK(pgm.body->Int() == 7);
  }

  {
    const Program pgm = Run("case 2 of\n"
                            "   1 => 111\n"
                            " | 2 => 222\n"
                            " | 3 => 333\n"
                            " | _ => 666");
    CHECK(pgm.body->Int() == 222);
  }

  {
    const Program pgm = Run("case (1, 2, 3) of\n"
                            "   (1, 2, 3) => 7\n"
                            " | _ => 666\n");
    // This exercises known value optimizations.
    CHECK(pgm.body->Int() == 7);
  }

  {
    const Program pgm = Run("case \"hello\" of\n"
                            "   \"world\" => 1234\n"
                            " | \"hello\" => 7\n"
                            " | _ => 9\n");
    CHECK(pgm.body->Int() == 7);
  }

  {
    const Program pgm = Run("let\n"
                            "  datatype sss = AAA of int | BBB of string\n"
                            "in\n"
                            "  case AAA 7 of\n"
                            "    AAA x => x\n"
                            "  | BBB s => 666\n"
                            "end\n");
    CHECK(pgm.body->Int() == 7);
  }

  {
    const Program pgm = Run("let\n"
                            "  val loop = fn as loop x =>\n"
                            "                    loop 0\n"
                            "in\n"
                            "  7\n"
                            "end\n");
    CHECK(pgm.body->Int() == 7) << "Tests whether we can drop an "
      "unused polymorphic value (val binding).";
  }

  {
    const Program pgm = Run("let\n"
                            "  val fact = fn as fact x =>\n"
                            "     (case x of\n"
                            "       0 => 1\n"
                            "     | n => n * fact (n - 1))\n"
                            "  do (6, fact 7)\n"
                            "in\n"
                            "  7\n"
                            "end\n");
    // Should be able to inline 'fact', and remove the unnecessary tuple
    // despite having an effectful element.
    CHECK(pgm.body->type == ExpType::SEQ);
    const auto &[es, body] = pgm.body->Seq();
    CHECK(es.size() == 1);
    CHECK(es[0]->type == ExpType::APP);
    CHECK(body->Int() == 7);
  }

  {
    const Program pgm = Run("let\n"
                            " val rec = fn as rec x => "
                            "   case x of\n"
                            "     0 => 0\n"
                            "   | n => rec (n - 1)\n"
                            " do case rec 9 of\n"
                            "      0 => 1 div 0\n"
                            "    | 1 => 666\n"
                            "    | _ => 444 + 111\n"
                            "in\n"
                            " 7\n"
                            "end");
    // Inline the function, but we have to keep the intcase
    // because we would enter the 1 div 0 arm, which has an effect.
    CHECK(pgm.body->type == ExpType::SEQ);
    const auto &[es, body] = pgm.body->Seq();
    CHECK(es.size() == 1);
    // TODO: This doesn't work yet because we need to simplify something
    // like
    // let x = ..effectful expression..
    // in intcase x of ...
    // end
    // Which is a bit tricky; need to understand what effects intervene.
    // CHECK(es[0]->type == ExpType::INTCASE) << ExpString(es[0]);
    CHECK(body->Int() == 7);
  }

  {
    const Program pgm =
      Run("case true of\n"
          "   false => 666\n"
          " | true => 777\n");
    CHECK(pgm.body->Int() == 777) << "Should simplify.";
  }

  {
    const Program pgm = Run("if false then 666 else 777");
    CHECK(pgm.body->Int() == 777) << "Should simplify.";
  }

  {
    for (uint8_t bits = 0; bits < 8; bits++) {
      bool a = !!(bits & 4);
      bool b = !!(bits & 2);
      bool c = !!(bits & 1);
      bool expected_result = (a && b) || c;
      const std::string source_string =
        StringPrintf("%s andalso %s orelse %s\n",
                     a ? "true" : "false",
                     b ? "true" : "false",
                     c ? "true" : "false");

      const Program pgm = Run(source_string);
      CHECK(pgm.body->Bool() == expected_result) << source_string;
    }
  }

  {
    const Program pgm = Run("print (\"all you have to do\" ^ \" is do it\")");
    CHECK(pgm.body->type == ExpType::PRIMOP) << ProgramString(pgm);
    const auto &[f, targs, eargs] = pgm.body->Primop();
    CHECK(eargs.size() == 1);
    CHECK(eargs[0]->type == ExpType::STRING);
    CHECK(eargs[0]->String() == "all you have to do is do it");
  }

}

static void Simple() {
  Frontend front;
  if (VERBOSE) {
    front.SetVerbose(1);
  }

  {
    const Program pgm = RunNoSimplify("let val x = 3 in x end");
    const auto &[tyvars, x, rhs, body] = pgm.body->Let();
    CHECK(tyvars.empty());
    if (VERBOSE) {
      printf("... %s\n", ProgramString(pgm).c_str());
    }
  }

  {
    const Program pgm = RunNoSimplify("let val (x, y) = (7, \"hi\") in x end");
    if (VERBOSE) {
      printf("%s\n", ProgramString(pgm).c_str());
    }
    // Should bind tuple, and the two vars.
    const auto &[tyvars, x, rhs, body] = pgm.body->Let();
    CHECK(tyvars.empty());
    CHECK(body->type == ExpType::LET);
    const Exp *body2 = std::get<3>(body->Let());
    CHECK(body2->type == ExpType::LET);
    const Exp *body3 = std::get<3>(body2->Let());
    CHECK(body3->type == ExpType::VAR);
  }

  {
    const Program pgm = RunNoSimplify(
        // TODO: We no longer support AS patterns in irrefutable
        // positions like this. But we could add support back.
        "let val (x (* as z *), _) = (7, \"hi\") in x end");
    if (VERBOSE) {
      printf("%s\n", ProgramString(pgm).c_str());
    }
    const auto &[tyvars, x, rhs, body] = pgm.body->Let();
    CHECK(tyvars.empty()) << "Should not be polymorphic!";

    std::unordered_set<std::string> declared;
    const Exp *e = pgm.body;
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
    const Program pgm = Run("fn x => x");
    const auto &[self, x, arrow_type, body] = pgm.body->Fn();
    CHECK(self.empty()) << "Not recursive.";
    (void)arrow_type->Arrow();
    CHECK(x == std::get<1>(body->Var())) << "Should be able to "
      "simplify this to just a variable.";
  }

}

static void TestPatternCompilation() {
  Frontend front;
  if (VERBOSE) {
    front.SetVerbose(1);
  }

  {
    const Program pgm =
      Run("case 777 of\n"
          "   (777 as y) as ((_ : int) as 444) => 666\n"
          " | (777 as y) as ((z : int) as 777) => 7\n"
          " | (777 as y) as ((_ : int) as 555) => 666\n"
          " | _ => 666\n");
    CHECK(pgm.body->Int() == 7) << "Should simplify.";
  }

}

static void TestDatatypes() {
  Frontend front;
  if (VERBOSE) {
    front.SetVerbose(2);
  }

  {
    const Program pgm = Run("let datatype (a) option = SOME of a | NONE\n"
                            "in 7\n"
                            "end");
    // Datatype declarations are transparent, so this should just be the
    // body.
    CHECK(pgm.body->Int() == 7);
  }

  {
    // Test nullary rewrite.
    const Program pgm = Run("let\n"
                            "  datatype sss = AAA | BBB of int\n"
                            "in\n"
                            "  case AAA of\n"
                            "    BBB x => x\n"
                            "  | AAA => 7\n"
                            "end\n");
    CHECK(pgm.body->Int() == 7);
  }

  {
    // Tests that we can compile polymorphic constructors and patterns.
    const Program pgm = Run("let datatype (a) option = SOME of a | NONE\n"
                            "  fun option-map f (SOME x) = SOME (f x)\n"
                            "    | option-map f NONE = NONE\n"
                            "in SOME 7\n"
                            "end");
    // Should be a constructor application.
    CHECK(pgm.body->type == ExpType::ROLL);
  }

  {
    // Tests that we can compile polymorphic constructors and patterns.
    const Program pgm = Run("let datatype (a) option = SOME of a | NONE\n"
                            "  fun option-map f (SOME x) = SOME (f x)\n"
                            "    | option-map f NONE = NONE\n"
                            "in SOME 7\n"
                            "end");
    // Should be a constructor application.
    CHECK(pgm.body->type == ExpType::ROLL);
  }

  {
    // Polymorphic, recursive datatype with infix constructor.
    const Program pgm = Run(
        "let\n"
        "datatype (a) list = :: of a * list | nil\n"
        "\n"
        "fun id (h :: t) = id(t)\n"
        "  | id (nil) = nil\n"
        "in 7 end\n");
    CHECK(pgm.body->type == ExpType::INT);
  }

  {
    // Polymorphic, recursive datatype with infix constructor.
    const Program pgm = Run(
        "let\n"
        "datatype (a) list = :: of a * list | nil\n"
        "\n"
        "fun list-append (h :: t, l2) = h :: list-append(t, l2)\n"
        "  | list-append (nil, l2) = l2\n"
        "in 7 end\n");
    CHECK(pgm.body->type == ExpType::INT);
  }

  {
    // Nontrivial mutually-recursive datatype.
    // Tests that the mu varaibles are bound in all arms!
    const Program pgm = Run(R"(
        let
          datatype exp = AAA of dec
          and dec = BBB of exp

          fun dtos (BBB e) = e : exp

        in
          7
        end)");
  }

}

static void TestFun() {
  Frontend front;
  if (VERBOSE) {
    front.SetVerbose(2);
  }

  // No mutual recursion.
  {
    const Program pgm = Run("let\n"
                            "  fun seven() = 7\n"
                            "  fun call (f, _) = f (seven ())\n"
                            "    | call _ = fail \"no\"\n"
                            "  fun f y = y\n"
                            "in\n"
                            "  call (f, 3)\n"
                            "end\n");
    CHECK(pgm.body->Int() == 7);
  }

  {
    const Program pgm = Run("let\n"
                            "  fun f 0 = 1\n"
                            "    | f x = g (x - 1)\n"
                            "  and g y = f y\n"
                            "in\n"
                            "  f 7\n"
                            "end\n");
    // CHECK(pgm.body->Integer() == 7);
    if (VERBOSE) {
      printf("%s\n", ProgramString(pgm).c_str());
    }
  }

  {
    const Program pgm = Run("let\n"
                            "  fun one x = x\n"
                            "  and two z = z + 1\n"
                            "  and three (x, y) = (y, x)\n"
                            "in\n"
                            "  two 6\n"
                            "end\n");
    CHECK(pgm.globals.empty()) << "Should be able to drop or inline "
      "all the globals in this case.";
  }

  // Simple polymorphic function.
  (void)RunNoSimplify(
      "let\n"
      "  fun id x = x\n"
      "  do id \"hello\"\n"
      "in\n"
      "  id 7\n"
      "end\n");

  // Same with fn.
  (void)RunNoSimplify(
      "let\n"
      "  val id = fn x => x\n"
      "  do id \"hello\"\n"
      "in\n"
      "  id 7\n"
      "end\n");

  // Mutually-recursive, polymorphic.
  (void)Run(
      "let\n"
      "  fun id0 x = x\n"
      "  and id1 y = y\n"
      "  do id0 \"hi\"\n"
      "in\n"
      "  id0 7\n"
      "end\n");

  // Nontrivial environment.
  (void)Run(
      "let\n"
      "  val seven = 7\n"
      "  fun id0 x = seven\n"
      "  and id1 y = seven\n"
      "  do id0 \"hi\"\n"
      "in\n"
      "  id0 5\n"
      "end\n");

  // Currying, simple.
  {
    const Program pgm =
      Run("let fun K x y = x\n"
          "in K 7 \"hi\"\n"
          "end");
    CHECK(pgm.globals.empty());
    CHECK(pgm.body->Int() == 7);
  }

  (void)Run("let\n"
            "  fun mult 0 x = 0\n"
            "    | mult x 0 = 0\n"
            "    | mult 1 x = x\n"
            "    | mult x 1 = x\n"
            "    | mult x y = x + mult (y - 1) x\n"
            "in\n"
            "  mult 3 3 - 2\n"
            "end\n");


  // Trailing return type annotation
  (void)Run("let fun f (s : string) : int = string-size s\n"
            "in f \"hi\"\n"
            "end\n");
}


static void TestObjects() {
  Frontend front;
  if (VERBOSE) {
    front.SetVerbose(2);
  }

  {
    const Program pgm = Run(
        "let object Article of { title : string, year : int }\n"
        "in 7\n"
        "end");
    // Object declarations are transparent.
    CHECK(pgm.body->Int() == 7);
  }

  {
    [[maybe_unused]]
    const Program pgm = Run(
        "let object Article of { title : string, year : int }\n"
        "in case {(Article) title = \"hi\"} of\n"
        "      {(Article) year = 1997 } => 555\n"
        "    | {(Article) year = 2024 } => 666\n"
        "    | {(Article) title } => 7\n"
        "    | {(Article) year = 42 } => 888\n"
        "end");
    // Can't really test this without further simplifications.
    // But it should elaborate!
    // printf("%s", ProgramString(pgm).c_str());
  }

  {
    const Program pgm = Run(
        "let object Article of { title : string, year : int }\n"
        "in\n"
        "  {(Article) } with title = \"hi\"\n"
        "end\n");
    CHECK(pgm.body->type == ExpType::WITH);
  }

  {
    const Program pgm = Run(
        "let object Article of { title : string, year : int }\n"
        "in\n"
        "  {(Article) } without (Article) title\n"
        "end\n");
    CHECK(pgm.body->type == ExpType::WITHOUT);
  }
}

static void TestLayout() {
  constexpr bool VERBOSE = false;
  Frontend front;
  if (VERBOSE) {
    front.SetVerbose(2);
  }

  {
    const Program pgm = Run(
        "let object ABC of { x : int }\n"
        "in node {(ABC) } [hi] end");
    CHECK(pgm.body->type == ExpType::PRIMOP) << ProgramString(pgm);
    const auto &[po, ts, es] = pgm.body->Primop();
    CHECK(po == Primop::STRING_TO_LAYOUT);
    CHECK(ts.empty());
    CHECK(es.size() == 1);
    CHECK(es[0]->String() == "hi");
  }

}

// Former bugs.
static void Regression() {
  static constexpr int VERBOSE = 0;
  Frontend front;
  if (VERBOSE) {
    front.SetVerbose(VERBOSE);
  }

  // r5668 generalization bug:
  // Failure to find free evars inside bound evars.
  {
  const Program pgm = Run(R"(
        let
          datatype (a) list = :: of a * list | nil
          fun list-map f nil = nil
            | list-map f (h :: t) = (f h) :: (list-map f t)
          val x : string list = list-map int-to-string (2 :: nil)
          val y : string list = list-map (fn s => s ^ ",") x
          fun list-app (g, zz :: _) =
            let in
              g zz; ()
            end
        in
          3
        end)");
  }

  // r5668 same kind of issue: The occurs check wasn't looking
  // inside bound evars.
  {
  const Program pgm = Run(R"(
    let
      datatype (a) option = SOME of a | NONE

      fun ms (r as (SOME _)) = r : int option
        | ms ll = ms ll

    in 0
    end
    )");
  }

}

static void NewTests() {
  static constexpr int VERBOSE = 2;
  Frontend front;
  if (VERBOSE) {
    front.SetVerbose(VERBOSE);
  }

}

}  // il

int main(int argc, char **argv) {
  ANSI::Init();

  il::TestLiterals();
  il::TestPrimops();
  il::TestSimplify();
  il::TestPatternCompilation();
  il::Simple();
  il::TestDatatypes();
  il::TestFun();
  il::TestObjects();
  il::TestLayout();
  il::Regression();
  il::NewTests();

  printf("OK\n");
  return 0;
}
