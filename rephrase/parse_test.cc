
#include "parse.h"

#include <string>
#include <vector>

#include "el.h"
#include "base/logging.h"
#include "ansi.h"
#include "bignum/big-overloads.h"
#include "base/stringprintf.h"

namespace el {

static constexpr bool VERBOSE = true;

static void TestParse() {
  AstPool pool;
  auto Parse = [&](const std::string &s) {
      std::string error;
      std::optional<std::vector<Token>> tokens = Lexing::Lex(s, &error);
      CHECK(tokens.has_value()) << "Did not lex: " << error;
      // print tokens?
      if (VERBOSE) {
        printf("Parse [" AWHITE("%s") "]:\n", s.c_str());
      }
      return Parsing::Parse(&pool, s, tokens.value());
    };

  if (VERBOSE) {
    printf("Start parser tests...\n");
    fflush(stdout);
  }

  {
    const Exp *e = Parse("15232");
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::INTEGER);
    CHECK(e->integer == 15232);
  }

  if (VERBOSE) {
    printf("Trivial parsing OK.\n");
    fflush(stdout);
  }

  {
    const Exp *e = Parse("var");
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::VAR);
    CHECK(e->str == "var");
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
    CHECK(e->type == ExpType::INTEGER);
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
    CHECK(e->a->type == ExpType::INTEGER);
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
    CHECK(e->a->type == ExpType::INTEGER);
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
    CHECK(e->a->type == ExpType::INTEGER);
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
    CHECK(e->a->type == ExpType::INTEGER);
    CHECK(e->a->integer == 7);
  }

  {
    const Exp *e = Parse("if 1 then 2 else 3");
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::IF);
    CHECK(e->a->type == ExpType::INTEGER);
    CHECK(e->a->integer == 1);
    CHECK(e->b->type == ExpType::INTEGER);
    CHECK(e->b->integer == 2);
    CHECK(e->c->type == ExpType::INTEGER);
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
    CHECK(fd.clauses[1].second->type == ExpType::INTEGER);
    CHECK(e->a->type == ExpType::INTEGER);
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
    CHECK(rhs->type == ExpType::INTEGER);
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

  printf("Exp parsing " AGREEN("OK") "\n");
}

static void TestParseType() {
  AstPool pool;
  auto ParseType = [&](const std::string &stype) -> const Type * {
      std::string s = StringPrintf("0 : %s", stype.c_str());
      std::string error;
      std::optional<std::vector<Token>> tokens = Lexing::Lex(s, &error);
      CHECK(tokens.has_value()) << "Did not lex: " << error;
      // print tokens?
      if (VERBOSE) {
        printf("Parse [" AWHITE("%s") "]:\n", s.c_str());
      }
      const Exp *e = Parsing::Parse(&pool, s, tokens.value());
      CHECK(e != nullptr) << stype;
      CHECK(e->type == ExpType::ANN) << stype;
      CHECK(e->a->type == ExpType::INTEGER);
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


  printf("Type parsing " AGREEN("OK") "\n");
}

static void TestParsePat() {
  AstPool pool;
  auto ParsePat = [&](const std::string &spat) -> const Pat * {
      std::string s = StringPrintf("let val %s = 0 in 1 end",
                                   spat.c_str());
      std::string error;
      std::optional<std::vector<Token>> tokens = Lexing::Lex(s, &error);
      CHECK(tokens.has_value()) << "Did not lex: " << error;
      // print tokens?
      if (VERBOSE) {
        printf("Parse [" AWHITE("%s") "]:\n", s.c_str());
      }
      const Exp *e = Parsing::Parse(&pool, s, tokens.value());
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
    CHECK(pat->a->str == "y");
    CHECK(pat->a->a->type == PatType::VAR);
    CHECK(pat->a->a->str == "x");
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

}

static void TestParseDec() {
  AstPool pool;
  auto ParseDec = [&](const std::string &sdec) -> const Dec * {
      std::string s = StringPrintf("let %s in 7 end",
                                   sdec.c_str());
      std::string error;
      std::optional<std::vector<Token>> tokens = Lexing::Lex(s, &error);
      CHECK(tokens.has_value()) << "Did not lex: " << error;
      // print tokens?
      if (VERBOSE) {
        printf("Parse [" AWHITE("%s") "]:\n", s.c_str());
      }
      const Exp *e = Parsing::Parse(&pool, s, tokens.value());
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

}

}  // namespace el

int main(int argc, char **argv) {
  ANSI::Init();
  el::TestParse();
  el::TestParseType();
  el::TestParsePat();
  el::TestParseDec();

  printf("OK\n");
  return 0;
}
