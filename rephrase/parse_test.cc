
#include "parse.h"

#include <string>
#include <vector>

#include "el.h"
#include "base/logging.h"
#include "ansi.h"
#include "bignum/big-overloads.h"
#include "base/stringprintf.h"

namespace el {

static void TestParse() {
  AstPool pool;
  auto Parse = [&](const std::string &s) {
      std::string error;
      std::optional<std::vector<Token>> tokens = Lexing::Lex(s, &error);
      CHECK(tokens.has_value()) << "Did not lex: " << error;
      // print tokens?
      printf("Parse [" AWHITE("%s") "]:\n", s.c_str());
      return Parsing::Parse(&pool, s, tokens.value());
    };

  printf("Start parser tests...\n");
  fflush(stdout);

  {
    const Exp *e = Parse("15232");
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::INTEGER);
    CHECK(e->integer == 15232);
  }

  printf("Trivial parsing OK.\n");
  fflush(stdout);

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
    CHECK(e->decs[0]->pat->var == "x");
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
    CHECK(a->children[0]->var == "x");
    CHECK(a->children[1]->type == PatType::WILD);
    CHECK(b->type == PatType::VAR);
    CHECK(b->var == "y");
    CHECK(c->type == PatType::VAR);
    CHECK(c->var == "zzz");

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
    const Exp *e = Parse("let fun f(x) = x in 7 end");
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::LET);
    CHECK(e->decs.size() == 1);
    CHECK(e->decs[0]->type == DecType::FUN);
    CHECK(e->decs[0]->str == "f");
    CHECK(e->decs[0]->pat->type == PatType::VAR);
    CHECK(e->decs[0]->pat->var == "x");
    CHECK(e->a != nullptr);
    CHECK(e->a->type == ExpType::INTEGER);
    CHECK(e->a->integer == 7);
  }

  {
    const Exp *e = Parse("let fun f _ = x in 7 end");
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::LET);
    CHECK(e->decs.size() == 1);
    CHECK(e->decs[0]->type == DecType::FUN);
    CHECK(e->decs[0]->str == "f");
    CHECK(e->decs[0]->pat->type == PatType::WILD);
    CHECK(e->a != nullptr);
    CHECK(e->a->type == ExpType::INTEGER);
    CHECK(e->a->integer == 7);
  }

  {
    const Exp *e = Parse("let fun f (u : {}) = x in 7 end");
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::LET);
    CHECK(e->decs.size() == 1);
    CHECK(e->decs[0]->type == DecType::FUN);
    CHECK(e->decs[0]->str == "f");
    const Pat *pat = e->decs[0]->pat;
    CHECK(pat->type == PatType::ANN);
    CHECK(pat->a->type == PatType::VAR);
    CHECK(pat->ann->type == TypeType::RECORD);
    CHECK(pat->ann->str_children.empty());
    CHECK(e->a != nullptr);
    CHECK(e->a->type == ExpType::INTEGER);
    CHECK(e->a->integer == 7);
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
      printf("Parse [" AWHITE("%s") "]:\n", s.c_str());
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

}  // namespace el

int main(int argc, char **argv) {
  ANSI::Init();
  el::TestParse();
  el::TestParseType();

  printf("OK\n");
  return 0;
}
