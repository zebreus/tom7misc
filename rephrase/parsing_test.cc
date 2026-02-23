
#include "parsing.h"

#include <cstdio>
#include <format>
#include <string>
#include <utility>
#include <vector>
#include <optional>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "bignum/big-overloads.h"
#include "el.h"
#include "inclusion.h"
#include "lexing.h"

namespace el {

static constexpr bool VERBOSE = false;

// TODO Parse failure tests

static void TestParse() {
  AstPool pool;
  auto Parse = [&](const std::string &s) {
      SourceMap source_map = Inclusion::SimpleSourceMap(__func__, s);
      std::string error;
      std::optional<std::vector<Token>> tokens = Lexing::Lex(s, &error);
      CHECK(tokens.has_value()) << "Did not lex: " << error;
      // print tokens?
      if (VERBOSE) {
        Print("Parse [" AWHITE("{}") "]:\n", s);
      }
      const Exp *parsed = Parsing::Parse(&pool, source_map, s, tokens.value());
      CHECK(parsed != nullptr) << "Parsing failed.";
      return parsed;
    };

  if (VERBOSE) {
    Print("Start parser tests...\n");
    fflush(stdout);
  }

  {
    const Exp *e = Parse("15232");
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::INT);
    CHECK(e->integer == 15232);
  }

  {
    const Exp *e = Parse("-12345");
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::INT);
    CHECK(e->integer == -12345);
  }

  {
    const Exp *e = Parse("0xCC71B0");
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::INT);
    CHECK(e->integer == 0xCC71B0);
  }

  {
    const Exp *e = Parse("0xCC.71.B0");
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::INT);
    CHECK(e->integer == 0xCC71B0);
  }

  {
    const Exp *e = Parse("0d987654321");
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::INT);
    CHECK(e->integer == 987654321);
  }

  {
    const Exp *e = Parse("0o76543210");
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::INT);
    CHECK(e->integer == 076543210);
  }

  {
    const Exp *e = Parse("0b0000101010");
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::INT);
    CHECK(e->integer == 42);
  }

  {
    const Exp *e = Parse("-1234.5");
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::FLOAT);
    // Represented exactly.
    CHECK(e->d == -1234.5);
  }

  {
    const Exp *e = Parse("0'*'");
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::INT);
    CHECK(e->integer == 0x2A);
  }

  {
    const Exp *e = Parse("0'☁'");
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::INT);
    CHECK(e->integer == 0x2601);
  }

  if (VERBOSE) {
    Print("Trivial parsing OK.\n");
    fflush(stdout);
  }

  {
    const Exp *e = Parse("var");
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::VAR);
    CHECK(e->str == "var");
  }

  {
    const Exp *e = Parse("true");
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::BOOL);
    CHECK(e->boolean == true);
  }

  {
    const Exp *e = Parse("false");
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::BOOL);
    CHECK(e->boolean == false);
  }

  {
    const Exp *e = Parse(" \"a string\" ");
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::STRING);
    CHECK(e->str == "a string");
  }

  {
    const Exp *e = Parse(R"( "now:\nwith \\ \"escapes\"" )");
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::STRING);
    CHECK(e->str == "now:\nwith \\ \"escapes\"");
  }

  {
    const Exp *e = Parse("(123)");
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::INT);
    CHECK(e->integer == 123);
  }

  {
    const Exp *e = Parse("()");
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::TUPLE);
    CHECK(e->children.size() == 0);
  }

  {
    const Exp *e = Parse("(var,123,\"yeah\")");
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::TUPLE);
    CHECK(e->children.size() == 3);
    CHECK(e->children[0]->str == "var");
    CHECK(e->children[1]->integer == 123);
    CHECK(e->children[2]->str == "yeah");
  }

  {
    const Exp *e = Parse("( xar , (333 ,777 ), 888)");
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::TUPLE);
    CHECK(e->children.size() == 3);
    CHECK(e->children[0]->str == "xar");
    CHECK(e->children[1]->type == ExpType::TUPLE);
    CHECK(e->children[1]->children.size() == 2);
    CHECK(e->children[1]->children[0]->integer == 333);
    CHECK(e->children[1]->children[1]->integer == 777);
    CHECK(e->children[2]->integer == 888);
  }


  {
    // Note that this is three tokens: an empty LAYOUT_LIT is
    // tokenized between the bracketse.
    const Exp *e = Parse("[]");
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::LAYOUT);
    CHECK(LayoutString(e->layout) == "");
  }

  {
    const Exp *e = Parse("(xyz, [layout], 888)");
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::TUPLE);
    CHECK(e->children.size() == 3);
    CHECK(e->children[0]->str == "xyz");
    CHECK(e->children[1]->type == ExpType::LAYOUT);
    const Layout *lay = e->children[1]->layout;
    CHECK(lay->type == LayoutType::TEXT);
    CHECK(lay->str == "layout");
    CHECK(e->children[2]->integer == 888);
  }

  {
    const Exp *e = Parse("[layout[b]after]");
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::LAYOUT);
    const std::vector<const Layout *> v =
      FlattenLayout(e->layout);
    CHECK(v.size() == 3) << v.size();
    CHECK(v[0]->type == LayoutType::TEXT);
    CHECK(v[0]->str == "layout");
    CHECK(v[1]->type == LayoutType::EXP);
    CHECK(v[1]->exp->type == ExpType::VAR);
    CHECK(v[1]->exp->str == "b");
    CHECK(v[2]->str == "after");
  }

  {
    const Exp *e = Parse("let do u in 7 end");
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::LET);
    CHECK(e->decs.size() == 1);
    // DO is syntactic sugar for var _
    CHECK(e->decs[0]->type == DecType::VAL);
    CHECK(e->decs[0]->pat->type == PatType::WILD);
    CHECK(e->decs[0]->exp->type == ExpType::VAR);
    CHECK(e->decs[0]->exp->str == "u");
    CHECK(e->a != nullptr);
    CHECK(e->a->type == ExpType::INT);
    CHECK(e->a->integer == 7);
  }

  {
    // Same as previous, but explicit wildcard.
    const Exp *e = Parse("let val _ = u in 7 end");
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::LET);
    CHECK(e->decs.size() == 1);
    CHECK(e->decs[0]->type == DecType::VAL);
    CHECK(e->decs[0]->pat->type == PatType::WILD);
    CHECK(e->decs[0]->exp->type == ExpType::VAR);
    CHECK(e->decs[0]->exp->str == "u");
    CHECK(e->a != nullptr);
    CHECK(e->a->type == ExpType::INT);
    CHECK(e->a->integer == 7);
  }

  {
    const Exp *e = Parse("let val x = u in 7 end");
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::LET);
    CHECK(e->decs.size() == 1);
    CHECK(e->decs[0]->type == DecType::VAL);
    CHECK(e->decs[0]->pat->type == PatType::VAR);
    CHECK(e->decs[0]->pat->str == "x");
    CHECK(e->decs[0]->exp->type == ExpType::VAR);
    CHECK(e->decs[0]->exp->str == "u");
    CHECK(e->a != nullptr);
    CHECK(e->a->type == ExpType::INT);
    CHECK(e->a->integer == 7);
  }

  {
    const Exp *e = Parse("let val ((x, _), (y), zzz) = u in 7 end");
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::LET);
    CHECK(e->decs.size() == 1);
    CHECK(e->decs[0]->type == DecType::VAL);
    const Pat *pat = e->decs[0]->pat;
    CHECK(pat != nullptr);
    CHECK(pat->type == PatType::TUPLE) << PatString(pat);
    CHECK(pat->children.size() == 3);
    const Pat *a = pat->children[0];
    const Pat *b = pat->children[1];
    const Pat *c = pat->children[2];
    CHECK(a->type == PatType::TUPLE);
    CHECK(a->children.size() == 2);
    CHECK(a->children[0]->type == PatType::VAR);
    CHECK(a->children[0]->str == "x");
    CHECK(a->children[1]->type == PatType::WILD);
    CHECK(b->type == PatType::VAR);
    CHECK(b->str == "y");
    CHECK(c->type == PatType::VAR);
    CHECK(c->str == "zzz");

    CHECK(e->decs[0]->exp->type == ExpType::VAR);
    CHECK(e->decs[0]->exp->str == "u");
    CHECK(e->a != nullptr);
    CHECK(e->a->type == ExpType::INT);
    CHECK(e->a->integer == 7);
  }

  {
    const Exp *e = Parse("if 1 then 2 else 3");
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::IF);
    CHECK(e->a->type == ExpType::INT);
    CHECK(e->a->integer == 1);
    CHECK(e->b->type == ExpType::INT);
    CHECK(e->b->integer == 2);
    CHECK(e->c->type == ExpType::INT);
    CHECK(e->c->integer == 3);
  }

  {
    const Exp *e = Parse("let fun f(x) = y in 7 end");
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::LET);
    CHECK(e->decs.size() == 1);
    const Dec *d = e->decs[0];
    CHECK(d->type == DecType::FUN);
    CHECK(d->funs.size() == 1);
    const FunDec &fd = d->funs[0];
    CHECK(fd.name == "f");
    CHECK(fd.clauses.size() == 1);
    CHECK(fd.clauses[0].first.size() == 1);
    CHECK(fd.clauses[0].first[0]->type == PatType::VAR);
    CHECK(fd.clauses[0].first[0]->str == "x");
    CHECK(fd.clauses[0].second != nullptr);
    CHECK(fd.clauses[0].second->type == ExpType::VAR);
    CHECK(fd.clauses[0].second->str == "y");
  }

  {
    const Exp *e = Parse("let fun f 1 = x | f _ = 7 in 8 end");
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::LET);
    CHECK(e->decs.size() == 1);
    const Dec *d = e->decs[0];
    CHECK(d->type == DecType::FUN);
    CHECK(d->funs.size() == 1);
    const FunDec &fd = d->funs[0];
    CHECK(fd.name == "f");
    CHECK(fd.clauses.size() == 2);
    CHECK(fd.clauses[0].first.size() == 1);
    CHECK(fd.clauses[0].first[0]->type == PatType::INT);
    CHECK(fd.clauses[0].second->type == ExpType::VAR);
    CHECK(fd.clauses[1].first.size() == 1);
    CHECK(fd.clauses[1].first[0]->type == PatType::WILD);
    CHECK(fd.clauses[1].second->type == ExpType::INT);
    CHECK(e->a->type == ExpType::INT);
    CHECK(e->a->integer == 8);
  }

  {
    const Exp *e = Parse("let fun f (u : {}) = x\n"
                         "      | f _ = 0\n"
                         "    and g 1 = 2\n"
                         "      | g x = 3\n"
                         "in 7 end");
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::LET);
    CHECK(e->decs.size() == 1);
    const Dec *d = e->decs[0];
    CHECK(d->type == DecType::FUN);
    CHECK(d->funs.size() == 2);
    const FunDec &fd0 = d->funs[0];
    CHECK(fd0.name == "f");
    CHECK(fd0.clauses.size() == 2);
    CHECK(fd0.clauses[0].first.size() == 1);
    CHECK(fd0.clauses[0].first[0]->type == PatType::ANN);
    CHECK(fd0.clauses[1].first.size() == 1);
    CHECK(fd0.clauses[1].first[0]->type == PatType::WILD);
    const FunDec &fd1 = d->funs[1];
    CHECK(fd1.name == "g");
    CHECK(fd1.clauses.size() == 2);
    CHECK(fd1.clauses[0].first.size() == 1);
    CHECK(fd1.clauses[0].first[0]->type == PatType::INT);
    CHECK(fd1.clauses[1].first.size() == 1);
    CHECK(fd1.clauses[1].first[0]->type == PatType::VAR);
  }

  {
    for (const char *s : {"f(x)", "f x", "(f)x"}) {
      const Exp *e = Parse(s);
      CHECK(e != nullptr);
      CHECK(e->type == ExpType::APP);
      CHECK(e->a->type == ExpType::VAR);
      CHECK(e->a->str == "f");
      CHECK(e->b->type == ExpType::VAR);
      CHECK(e->b->str == "x");
    }
  }

  {
    const char *s = "x + y";
    const Exp *e = Parse(s);
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::APP);
    CHECK(e->a->type == ExpType::VAR);
    CHECK(e->a->str == "+");
    CHECK(e->b->type == ExpType::TUPLE);
    CHECK(e->b->children.size() == 2);
    CHECK(e->b->children[0]->type == ExpType::VAR);
    CHECK(e->b->children[0]->str == "x");
    CHECK(e->b->children[1]->type == ExpType::VAR);
    CHECK(e->b->children[1]->str == "y");
  }

  {
    const char *s = "x *. 0.5";
    const Exp *e = Parse(s);
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::APP);
    CHECK(e->a->type == ExpType::VAR);
    CHECK(e->a->str == "*.");
    CHECK(e->b->type == ExpType::TUPLE);
    CHECK(e->b->children.size() == 2);
    CHECK(e->b->children[0]->type == ExpType::VAR);
    CHECK(e->b->children[0]->str == "x");
    CHECK(e->b->children[1]->type == ExpType::FLOAT);
    CHECK(e->b->children[1]->d == 0.5);
  }

  {
    const char *s = "f x + 100";
    const Exp *e = Parse(s);
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::APP);
    CHECK(e->a->type == ExpType::VAR);
    CHECK(e->a->str == "+");
    CHECK(e->b->type == ExpType::TUPLE);
    CHECK(e->b->children.size() == 2);
    const Exp *lhs = e->b->children[0];
    const Exp *rhs = e->b->children[1];
    CHECK(lhs->type == ExpType::APP);
    CHECK(lhs->a->str == "f");
    CHECK(lhs->b->str == "x");
    CHECK(rhs->type == ExpType::INT);
    CHECK(rhs->integer == 100);
  }

  {
    const Exp *e = Parse("#1/2 y");
    CHECK(e->type == ExpType::APP);
    CHECK(e->a->type == ExpType::FN);
    CHECK(e->b->type == ExpType::VAR);
  }

  {
    const Exp *e = Parse("{lab = 5, 2 = \"hi\"}");
    CHECK(e->type == ExpType::RECORD);
    CHECK(e->str_children.size() == 2);
    CHECK(e->str_children[0].first == "lab");
    CHECK(e->str_children[0].second->integer == 5);
    CHECK(e->str_children[1].first == "2");
    CHECK(e->str_children[1].second->str == "hi");
  }

  {
    const Exp *e = Parse("fn _ => 7");
    CHECK(e->type == ExpType::FN);
    CHECK(e->str.empty());
    CHECK(e->clauses.size() == 1);
    const auto &[pat, body] = e->clauses[0];
    CHECK(pat->type == PatType::WILD);
    CHECK(body->integer == 7);
  }

  {
    const Exp *e = Parse("fn as f 0 => 0 | x => f (x - 1)");
    CHECK(e->type == ExpType::FN);
    CHECK(e->str == "f");
    CHECK(e->clauses.size() == 2);
    CHECK(e->clauses[0].first->type == PatType::INT);
    CHECK(e->clauses[1].first->type == PatType::VAR);
  }

  {
    const Exp *e = Parse("case y of (x, z) => 7 | u => 8");
    CHECK(e->type == ExpType::CASE);
    CHECK(e->a->type == ExpType::VAR);
    CHECK(e->a->str == "y");
    CHECK(e->clauses.size() == 2);
    const auto &[pat1, exp1] = e->clauses[0];
    const auto &[pat2, exp2] = e->clauses[1];
    CHECK(pat1->type == PatType::TUPLE);
    CHECK(pat1->children.size() == 2);
    CHECK(pat1->children[0]->str == "x");
    CHECK(pat1->children[1]->str == "z");
    CHECK(exp1->integer == 7);
    CHECK(pat2->type == PatType::VAR);
    CHECK(pat2->str == "u");
    CHECK(exp2->integer == 8);
  }

  {
    // Here we use two floats that can be represented exactly.
    const Exp *e = Parse("1.5 + 1e100");
    CHECK(e->type == ExpType::APP);
    CHECK(e->b->children.size() == 2);
    const Exp *l = e->b->children[0];
    const Exp *r = e->b->children[1];
    CHECK(l->type == ExpType::FLOAT);
    CHECK(l->d == 1.5);
    CHECK(r->type == ExpType::FLOAT);
    CHECK(r->d == 1e100);
  }

  {
    const Exp *e = Parse("fail \"yes\"");
    CHECK(e->type == ExpType::FAIL);
    CHECK(e->a->type == ExpType::STRING);
  }

  {
    const Exp *e = Parse("1 andalso 2 orelse 3 andalso 4");
    CHECK(e->type == ExpType::ORELSE);
    const Exp *l = e->a;
    const Exp *r = e->b;
    CHECK(l->type == ExpType::ANDALSO);
    CHECK(r->type == ExpType::ANDALSO);
    CHECK(l->a->integer == 1);
    CHECK(l->b->integer == 2);
    CHECK(r->a->integer == 3);
    CHECK(r->b->integer == 4);
  }

  {
    const Exp *e = Parse("1 < 2 orelse 3 < 4 orelse 5 < 6");
    CHECK(e->type == ExpType::ORELSE);
    const Exp *l = e->a;
    const Exp *r = e->b;
    CHECK(l->type == ExpType::APP);
    CHECK(r->type == ExpType::ORELSE);
  }

  // XXX Parse these as infix operators. Right now this
  // requires parenthesization.
  if (false) {
    const Exp *e = Parse("1 < 2 andalso 3 < 4 andalso 5 < 6");
    CHECK(e->type == ExpType::ANDALSO);
    const Exp *l = e->a;
    const Exp *r = e->b;
    CHECK(l->type == ExpType::APP);
    CHECK(r->type == ExpType::ANDALSO);
  }

  {
    const Exp *e = Parse("x otherwise fail \"die\"");
    // It's compiled as syntactic sugar.
    CHECK(e->type == ExpType::IF) << ExpString(e);
    const Exp *cond = e->a;
    const Exp *tru = e->b;
    const Exp *fal = e->c;
    CHECK(cond->type == ExpType::VAR);
    CHECK(tru->type == ExpType::TUPLE);
    CHECK(fal->type == ExpType::FAIL);
  }

  {
    const Exp *e = Parse("x andthen fail \"die\"");
    // It's compiled as syntactic sugar.
    CHECK(e->type == ExpType::IF);
    const Exp *cond = e->a;
    const Exp *tru = e->b;
    const Exp *fal = e->c;
    CHECK(cond->type == ExpType::VAR);
    CHECK(tru->type == ExpType::FAIL);
    CHECK(fal->type == ExpType::TUPLE);
  }

  {
    const Exp *e = Parse("{ (Article) }");
    CHECK(e->type == ExpType::OBJECT);
    CHECK(e->str == "Article");
    CHECK(e->str_children.empty());
  }

  {
    const Exp *e = Parse("{(Article) title = \"hi\", year = 1997}");
    CHECK(e->type == ExpType::OBJECT);
    CHECK(e->str == "Article");
    CHECK(e->str_children.size() == 2);
    CHECK(e->str_children[0].first == "title");
    CHECK(e->str_children[1].first == "year");
  }

  {
    const Exp *e = Parse("{(Article) } with title = \"hi\"");
    CHECK(e->type == ExpType::WITH);
    CHECK(e->a->type == ExpType::OBJECT);
  }

  {
    const Exp *e = Parse("{(Article) } with title = \"hi\" with year = 1997");
    CHECK(e->type == ExpType::WITH);
    CHECK(e->a->type == ExpType::WITH);
  }

  {
    const Exp *e = Parse("{(Article) } "
                         "with title = \"hi\" "
                         "with (Article) year = 1997");
    CHECK(e->type == ExpType::WITH);
    CHECK(e->a->type == ExpType::WITH);
  }

  {
    const Exp *e = Parse("{(Article) year = 2001 } "
                         "without (Article) year "
                         "with title = \"hi\" "
                         "with (Article) year = 1997");
    CHECK(e->type == ExpType::WITH);
    CHECK(e->a->type == ExpType::WITH);
  }

  {
    const Exp *e = Parse("let in\n"
                         "3; 4; 5\n"
                         "end");
    CHECK(e->type == ExpType::LET);
    CHECK(e->decs.size() == 2) << ExpString(e);
    CHECK(e->decs[0]->type == DecType::VAL);
    CHECK(e->decs[0]->exp->integer == 3);
    CHECK(e->decs[1]->type == DecType::VAL);
    CHECK(e->decs[1]->exp->integer == 4);
    CHECK(e->a->type == ExpType::INT);
    CHECK(e->a->integer == 5);
  }

  {
    const Exp *e = Parse("let in\n"
                         "3;\n"
                         "end");
    CHECK(e->type == ExpType::LET);
    CHECK(e->a->type == ExpType::INT);
    CHECK(e->a->integer == 3);
  }

  {
    const Exp *e = Parse("{() }");
    CHECK(e->type == ExpType::OBJECT);
    CHECK(e->str_children.empty());
    CHECK(e->str.empty());
  }

  // XXX DEATH_TEST?
  if (false) {
    (void)Parse("let val x = 3\n"
                "    val y\n"
                "in 7 end\n");
  }


  Print("Exp parsing " AGREEN("OK") "\n");
}

static void TestParseType() {
  AstPool pool;
  auto ParseType = [&](const std::string &stype) -> const Type * {
      std::string s = std::format("0 : {}", stype);
      SourceMap source_map = Inclusion::SimpleSourceMap(__func__, s);
      std::string error;
      std::optional<std::vector<Token>> tokens = Lexing::Lex(s, &error);
      CHECK(tokens.has_value()) << "Did not lex: " << error;
      // print tokens?
      if (VERBOSE) {
        Print("Parse [" AWHITE("{}") "]:\n", s);
      }
      const Exp *e = Parsing::Parse(&pool, source_map, s, tokens.value());
      CHECK(e != nullptr) << stype;
      CHECK(e->type == ExpType::ANN) << stype;
      CHECK(e->a->type == ExpType::INT);
      CHECK(e->a->integer == 0);
      return e->t;
    };

  {
    const Type *t = ParseType("int");
    CHECK(t->type == TypeType::VAR);
    CHECK(t->var == "int");
    CHECK(t->children.empty());
  }

  for (const std::string s : {"int list", "(int) list", "(int list)"}) {
    const Type *t = ParseType(s);
    CHECK(t->type == TypeType::VAR);
    CHECK(t->var == "list");
    CHECK(t->children.size() == 1);
    CHECK(t->children[0]->type == TypeType::VAR);
    CHECK(t->children[0]->var == "int");
    CHECK(t->children[0]->children.empty());
  }

  {
    const Type *to = ParseType("int list option");
    CHECK(to->type == TypeType::VAR);
    CHECK(to->var == "option");
    CHECK(to->children.size() == 1);
    const Type *t = to->children[0];
    CHECK(t->type == TypeType::VAR);
    CHECK(t->var == "list");
    CHECK(t->children.size() == 1);
    CHECK(t->children[0]->type == TypeType::VAR);
    CHECK(t->children[0]->var == "int");
    CHECK(t->children[0]->children.empty());
  }

  {
    const Type *t = ParseType("(string, int) map");
    CHECK(t->type == TypeType::VAR);
    CHECK(t->var == "map");
    CHECK(t->children.size() == 2);
    CHECK(t->children[0]->type == TypeType::VAR);
    CHECK(t->children[0]->var == "string");
    CHECK(t->children[0]->children.empty());
    CHECK(t->children[1]->type == TypeType::VAR);
    CHECK(t->children[1]->var == "int");
    CHECK(t->children[1]->children.empty());
  }

  {
    const Type *t = ParseType("(string, int) map");
    CHECK(t->type == TypeType::VAR);
    CHECK(t->var == "map");
    CHECK(t->children.size() == 2);
    CHECK(t->children[0]->type == TypeType::VAR);
    CHECK(t->children[0]->var == "string");
    CHECK(t->children[0]->children.empty());
    CHECK(t->children[1]->type == TypeType::VAR);
    CHECK(t->children[1]->var == "int");
    CHECK(t->children[1]->children.empty());
  }

  auto ParseArrow = [&](const std::string &s) {
      const Type *t = ParseType(s);
      CHECK(t->type == TypeType::ARROW) << s << "\nGot: " <<
        TypeString(t);
      return std::make_pair(t->a, t->b);
    };

  {
    const auto &[dom, cod] = ParseArrow("int -> string");
    CHECK(dom->type == TypeType::VAR);
    CHECK(dom->var == "int");
    CHECK(cod->type == TypeType::VAR);
    CHECK(cod->var == "string");
  }

  {
    const auto &[dom, cod] = ParseArrow("int -> string -> float");
    CHECK(dom->type == TypeType::VAR);
    CHECK(dom->var == "int");

    CHECK(cod->type == TypeType::ARROW);
    CHECK(cod->a->var == "string");
    CHECK(cod->b->var == "float");
  }

  {
    const Type *t = ParseType("{lab1: float, lab2: int -> string}");
    CHECK(t->type == TypeType::RECORD);
    CHECK(t->str_children.size() == 2);
    CHECK(t->str_children[0].first == "lab1");
    CHECK(t->str_children[1].first == "lab2");
    CHECK(t->str_children[0].second->var == "float");
    const Type *f = t->str_children[1].second;
    CHECK(f->type == TypeType::ARROW);
    CHECK(f->a->var == "int");
    CHECK(f->b->var == "string");
  }

  {
    const Type *t = ParseType("int * string * float");
    CHECK(t->type == TypeType::PRODUCT);
    CHECK(t->children.size() == 3);
    for (const Type *tt : t->children) {
      CHECK(tt->type == TypeType::VAR);
    }
    CHECK(t->children[0]->var == "int");
    CHECK(t->children[1]->var == "string");
    CHECK(t->children[2]->var == "float");
  }

  {
    const auto &[dom, cod] = ParseArrow("int * string -> float * bool");
    CHECK(dom->type == TypeType::PRODUCT);
    CHECK(dom->children.size() == 2);
    CHECK(dom->children[1]->var == "string");
    CHECK(cod->type == TypeType::PRODUCT);
    CHECK(cod->children.size() == 2);
    CHECK(cod->children[0]->var == "float");
  }


  Print("Type parsing " AGREEN("OK") "\n");
}

static void TestParsePat() {
  AstPool pool;
  auto ParsePat = [&](const std::string &spat) -> const Pat * {
      std::string s = std::format("let val {} = 0 in 1 end", spat);
      SourceMap source_map = Inclusion::SimpleSourceMap(__func__, s);
      std::string error;
      std::optional<std::vector<Token>> tokens = Lexing::Lex(s, &error);
      CHECK(tokens.has_value()) << "Did not lex: " << error;
      // print tokens?
      if (VERBOSE) {
        Print("Parse [" AWHITE("%s") "]:\n", s);
      }
      const Exp *e = Parsing::Parse(&pool, source_map, s, tokens.value());
      CHECK(e != nullptr) << spat;
      CHECK(e->type == ExpType::LET) << spat;
      CHECK(e->decs.size() == 1) << spat;
      CHECK(e->decs[0]->type == DecType::VAL) << spat;
      CHECK(e->a->integer == 1);
      return e->decs[0]->pat;
    };

  {
    const Pat *pat = ParsePat("x");
    CHECK(pat->type == PatType::VAR);
    CHECK(pat->str == "x");
  }

  {
    const Pat *pat = ParsePat("(_, y)");
    CHECK(pat->type == PatType::TUPLE);
    CHECK(pat->children.size() == 2);
    CHECK(pat->children[0]->type == PatType::WILD);
    CHECK(pat->children[1]->type == PatType::VAR);
    CHECK(pat->children[1]->str == "y");
  }

  {
    const Pat *pat = ParsePat("x as y : int");
    CHECK(pat->type == PatType::ANN);
    CHECK(pat->a->type == PatType::AS);
    const Pat *a = pat->a->a;
    const Pat *b = pat->a->b;
    CHECK(a->type == PatType::VAR);
    CHECK(a->str == "x");
    CHECK(b->type == PatType::VAR);
    CHECK(b->str == "y");
  }

  {
    const Pat *pat = ParsePat("_ as _");
    CHECK(pat->type == PatType::AS);
    const Pat *a = pat->a;
    const Pat *b = pat->b;
    CHECK(a->type == PatType::WILD);
    CHECK(b->type == PatType::WILD);
  }

  {
    const Pat *pat = ParsePat("1 as 2");
    CHECK(pat->type == PatType::AS);
    const Pat *a = pat->a;
    const Pat *b = pat->b;
    CHECK(a->type == PatType::INT);
    CHECK(a->integer == 1);
    CHECK(b->type == PatType::INT);
    CHECK(b->integer == 2);
  }

  {
    const Pat *pat = ParsePat("(777 as y) as ((_ : int) as 444)");
    CHECK(pat->type == PatType::AS);
    const Pat *a = pat->a;
    const Pat *b = pat->b;
    CHECK(a->type == PatType::AS);
    CHECK(b->type == PatType::AS);
    const Pat *c = a->a;
    const Pat *d = a->b;
    CHECK(c->type == PatType::INT);
    CHECK(d->type == PatType::VAR);
    const Pat *e = b->a;
    const Pat *f = b->b;
    CHECK(e->type == PatType::ANN);
    CHECK(f->type == PatType::INT);
  }


  {
    const Pat *pat = ParsePat("{ lab = x, 2 = _ }");
    CHECK(pat->type == PatType::RECORD);
    CHECK(pat->str_children.size() == 2);
    // Might want to do unordered compare here?
    CHECK(pat->str_children[0].first == "lab");
    CHECK(pat->str_children[0].second->str == "x");
    CHECK(pat->str_children[1].first == "2");
    CHECK(pat->str_children[1].second->type == PatType::WILD);
  }

  {
    const Pat *pat = ParsePat("{ a, b = _ }");
    CHECK(pat->type == PatType::RECORD);
    CHECK(pat->str_children.size() == 2);
    // Might want to do unordered compare here?
    CHECK(pat->str_children[0].first == "a");
    CHECK(pat->str_children[0].second->type == PatType::VAR);
    CHECK(pat->str_children[0].second->str == "a");
    CHECK(pat->str_children[1].first == "b");
    CHECK(pat->str_children[1].second->type == PatType::WILD);
  }

  {
    const Pat *pat = ParsePat("{ a : int, b : string option }");
    CHECK(pat->type == PatType::RECORD);
    CHECK(pat->str_children.size() == 2);
    // Might want to do unordered compare here?
    CHECK(pat->str_children[0].first == "a");
    CHECK(pat->str_children[0].second->type == PatType::ANN);
    CHECK(pat->str_children[0].second->ann->type == TypeType::VAR);
    CHECK(pat->str_children[0].second->ann->var == "int");
    CHECK(pat->str_children[1].first == "b");
    CHECK(pat->str_children[1].second->type == PatType::ANN);
  }


  {
    const Pat *pat = ParsePat("(_, 7)");
    CHECK(pat->type == PatType::TUPLE);
    CHECK(pat->children.size() == 2);
    CHECK(pat->children[1]->type == PatType::INT);
    CHECK(pat->children[1]->integer == 7);
  }

  {
    const Pat *pat = ParsePat("SOME x");
    CHECK(pat->type == PatType::APP) << PatTypeString(pat->type);
    CHECK(pat->a->type == PatType::VAR);

    CHECK(pat->str == "SOME");
    CHECK(pat->a->str == "x");
  }

  {
    const Pat *pat = ParsePat("SOME 7");
    CHECK(pat->type == PatType::APP);
    CHECK(pat->a->type == PatType::INT);

    CHECK(pat->str == "SOME");
    CHECK(pat->a->integer == 7);
  }

  {
    const Pat *pat = ParsePat("true");
    CHECK(pat->type == PatType::BOOL);
    CHECK(pat->boolean == true);
  }

  {
    const Pat *pat = ParsePat("HYPER SOME x");
    CHECK(pat->type == PatType::APP);
    CHECK(pat->a->type == PatType::APP);
    CHECK(pat->a->a->type == PatType::VAR);
    CHECK(pat->str == "HYPER");
    CHECK(pat->a->str == "SOME");
    CHECK(pat->a->a->str == "x");
  }

  {
    const Pat *pat = ParsePat("x :: y");
    CHECK(pat->type == PatType::APP);
    CHECK(pat->a->type == PatType::TUPLE);
    CHECK(pat->a->children.size() == 2);
    CHECK(pat->a->children[0]->type == PatType::VAR);
    CHECK(pat->a->children[1]->type == PatType::VAR);
    CHECK(pat->str == "::");
    CHECK(pat->a->children[0]->str == "x");
    CHECK(pat->a->children[1]->str == "y");
  }

  {
    const Pat *pat = ParsePat("{(Article) title, author = _}");
    CHECK(pat->type == PatType::OBJECT);
    CHECK(pat->str == "Article");
    CHECK(pat->str_children.size() == 2);
  }

  {
    const Pat *pat = ParsePat("{() }");
    CHECK(pat->type == PatType::OBJECT);
    CHECK(pat->str.empty());
    CHECK(pat->str_children.empty());
  }

  Print("Pattern parsing " AGREEN("OK") "\n");
}

static void TestParseDec() {
  AstPool pool;
  auto ParseDec = [&](std::string_view sdec) -> const Dec * {
      std::string s = std::format("let {} in 7 end", sdec);
      SourceMap source_map = Inclusion::SimpleSourceMap(__func__, s);
      std::string error;
      std::optional<std::vector<Token>> tokens = Lexing::Lex(s, &error);
      CHECK(tokens.has_value()) << "Did not lex: " << error;
      // print tokens?
      if (VERBOSE) {
        Print("Parse [" AWHITE("{}") "]:\n", s);
      }
      const Exp *e = Parsing::Parse(&pool, source_map, s, tokens.value());
      CHECK(e != nullptr) << sdec;
      CHECK(e->type == ExpType::LET) << sdec;
      CHECK(e->decs.size() == 1) << sdec;
      return e->decs[0];
    };

  {
    const auto *dec = ParseDec("datatype dir = Left | Right");
    CHECK(dec->type == DecType::DATATYPE);
    CHECK(dec->tyvars.empty());
    CHECK(dec->datatypes.size() == 1);
    const DatatypeDec &dd = dec->datatypes[0];
    CHECK(dd.name == "dir");
    CHECK(dd.arms.size() == 2);
    CHECK(dd.arms[0].first == "Left");
    CHECK(dd.arms[0].second == nullptr);
    CHECK(dd.arms[1].first == "Right");
    CHECK(dd.arms[1].second == nullptr);
  }

  {
    const auto *dec = ParseDec(
        "object Nothing of { }");
    CHECK(dec->type == DecType::OBJECT);
    const ObjectDec &od = dec->object;
    CHECK(od.name == "Nothing");
    CHECK(od.fields.empty());
  }

  {
    const auto *dec = ParseDec(
        "object Article of { title : string, year : int }");
    CHECK(dec->type == DecType::OBJECT);
    const ObjectDec &od = dec->object;
    CHECK(od.name == "Article");
    CHECK(od.fields.size() == 2);
    CHECK(od.fields[0].first == "title");
    CHECK(od.fields[0].second->var == "string");
    CHECK(od.fields[1].first == "year");
    CHECK(od.fields[1].second->var == "int");
  }

  {
    const auto *dec = ParseDec(
        "type t = { }");
    CHECK(dec->type == DecType::TYPE);
    CHECK(dec->str == "t");
    CHECK(dec->tyvars.empty());
    CHECK(dec->t->type == TypeType::RECORD);
  }

  {
    const auto *dec = ParseDec(
        "open { }");
    CHECK(dec->type == DecType::OPEN);
    CHECK(dec->exp->type == ExpType::RECORD);
  }

  {
    const auto *dec = ParseDec("local  in  end");
    CHECK(dec->type == DecType::LOCAL);
    CHECK(dec->decs1.empty());
    CHECK(dec->decs2.empty());
  }

  {
    const auto *dec = ParseDec(
        "local val x = 3 in object Nothing of { } end");
    CHECK(dec->type == DecType::LOCAL);
    CHECK(dec->decs1.size() == 1);
    CHECK(dec->decs1[0]->type == DecType::VAL);
    CHECK(dec->decs2.size() == 1);
    CHECK(dec->decs2[0]->type == DecType::OBJECT);
  }

  Print("Dec parsing " AGREEN("OK") "\n");
}

static void TestParseLayout() {
  AstPool pool;

  auto Parse = [&](const std::string &s) -> const Layout * {
      SourceMap source_map = Inclusion::SimpleSourceMap(__func__, s);
      std::string error;
      std::optional<std::vector<Token>> tokens = Lexing::Lex(s, &error);
      CHECK(tokens.has_value()) << "Did not lex: " << error;
      // print tokens?
      if (VERBOSE) {
        Print("Parse [" AWHITE("{}") "]:\n", s);
      }
      const Exp *e = Parsing::Parse(&pool, source_map, s, tokens.value());
      CHECK(e->type == ExpType::LAYOUT);
      CHECK(e->layout != nullptr);
      return e->layout;
    };

  {
    const Layout *lay = Parse("[easy]");
    CHECK(lay->type == LayoutType::TEXT);
    CHECK(lay->str == "easy");
  }

  {
    const Layout *lay = Parse("[easy [x] it]");
    CHECK(lay->type == LayoutType::JOIN);
    CHECK(lay->children.size() == 3);
    const Layout *a = lay->children[0];
    const Layout *b = lay->children[1];
    const Layout *c = lay->children[2];
    if (VERBOSE) {
      Print("Layout: [{}]\n", LayoutString(lay));
    }
    CHECK(a->type == LayoutType::TEXT);
    CHECK(a->str == "easy ");
    CHECK(b->type == LayoutType::EXP);
    CHECK(b->exp->type == ExpType::VAR);
    CHECK(b->exp->str == "x");
    CHECK(c->type == LayoutType::TEXT) << LayoutTypeString(c->type);
    CHECK(c->str == " it");
  }

  {
    const Layout *lay = Parse("[easy [* does *] it]");
    CHECK(lay->type == LayoutType::JOIN);
    CHECK(lay->children.size() == 2);
    const Layout *a = lay->children[0];
    const Layout *b = lay->children[1];
    CHECK(a->type == LayoutType::TEXT);
    CHECK(a->str == "easy ");
    CHECK(b->type == LayoutType::TEXT);
    CHECK(b->str == " it");
  }

  Print("Layout parsing " AGREEN("OK") "\n");
}

static void TestParsePos() {
  AstPool pool;
  auto Parse = [&](const std::string &s) {
      SourceMap source_map = Inclusion::SimpleSourceMap(__func__, s);
      std::string error;
      std::optional<std::vector<Token>> tokens = Lexing::Lex(s, &error);
      CHECK(tokens.has_value()) << "Did not lex: " << error;
      // print tokens?
      if (VERBOSE) {
        Print("Parse [" AWHITE("{}") "]:\n", s);
      }
      return Parsing::Parse(&pool, source_map, s, tokens.value());
    };


  {
    const Exp *e = Parse("identifier");
    CHECK(e->pos == 0);
  }

  {
    const Exp *e = Parse("if 0 then \"hi\" else 7");
    CHECK(e->type == ExpType::IF);
    CHECK(e->b->pos == 10) << e->b->pos;
  }

  {
    const Exp *e = Parse(" identifier");
    CHECK(e->pos == 1) << e->pos;
  }

  {
    const Exp *e = Parse("  identifier");
    CHECK(e->pos == 2) << e->pos;
  }


  // TODO: Test more with positions!
}

// Larger examples that did something weird in the past.
static void TestRegressions() {
  AstPool pool;
  auto ParseDec = [&](std::string_view sdec) -> const Dec * {
      std::string s = std::format("let {} in 7 end", sdec);
      SourceMap source_map = Inclusion::SimpleSourceMap(__func__, s);
      std::string error;
      std::optional<std::vector<Token>> tokens = Lexing::Lex(s, &error);
      CHECK(tokens.has_value()) << "Did not lex: " << error;
      // print tokens?
      if (VERBOSE) {
        Print("Parse [" AWHITE("{}") "]:\n", s);
      }
      const Exp *e = Parsing::Parse(&pool, source_map, s, tokens.value());
      CHECK(e != nullptr) << sdec;
      CHECK(e->type == ExpType::LET) << sdec;
      CHECK(e->decs.size() == 1) << sdec;
      return e->decs[0];
    };

  if (false) {
    // This should not parse because of the missing =, and
    // it should give a helpful error message.
    // We would need EXPECT_DEATH to test it, though.
    static constexpr std::string_view syntax = R"(
fun bib-website (obj as {(Website) author}) =
  let
    val authors = bib-parse-authors author
    val key-rest = case obj of {(Website) url} => url | _ => ""
  in
    Source { t = Website, authors = authors,
             sort-key = author-sort-key authors key-rest, fields = obj }
  end
  | bib-website (obj as {(Website) url, organization}) (* = *)
  Source { t = Website, sort-key = url-sort-key url, fields = obj, authors = organization :: nil }
)";

    const Dec *dec = ParseDec(syntax);
    CHECK(dec == nullptr);
  }
}


}  // namespace el

int main(int argc, char **argv) {
  ANSI::Init();

  el::TestParse();
  el::TestParseType();
  el::TestParsePat();
  el::TestParseDec();
  el::TestParseLayout();
  el::TestParsePos();
  el::TestRegressions();

  Print("OK\n");
  return 0;
}
