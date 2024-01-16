
#include "parse.h"

#include <string>
#include <vector>
#include <deque>
#include <cstdint>

#include "ast.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "ansi.h"

static void TestParse() {
  AstPool pool;

  #if 0
  Parse(&pool,
        "the   cat fn() went to 1234 the \"string\" store\n"
        "where he \"\\\\slashed\\n\" t-i-r-e-s\n"
        "Here is a [nested [123] expression].\n"
        );
#endif


  {
    const Exp *e = Parse(&pool, "15232");
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::INTEGER);
    CHECK(e->integer == 15232);
  }

  {
    const Exp *e = Parse(&pool, "var");
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::VAR);
    CHECK(e->str == "var");
  }

  {
    const Exp *e = Parse(&pool, " \"a string\" ");
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::STRING);
    CHECK(e->str == "a string");
  }

  {
    const Exp *e = Parse(&pool, R"( "now:\nwith \\ \"escapes\"" )");
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::STRING);
    CHECK(e->str == "now:\nwith \\ \"escapes\"");
  }

  {
    const Exp *e = Parse(&pool, "(123)");
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::INTEGER);
    CHECK(e->integer == 123);
  }

  {
    const Exp *e = Parse(&pool, "()");
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::TUPLE);
    CHECK(e->children.size() == 0);
  }

  {
    const Exp *e = Parse(&pool, "(var,123,\"yeah\")");
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::TUPLE);
    CHECK(e->children.size() == 3);
    CHECK(e->children[0]->str == "var");
    CHECK(e->children[1]->integer == 123);
    CHECK(e->children[2]->str == "yeah");
  }

  {
    const Exp *e = Parse(&pool, "( xar , (333 ,777 ), 888)");
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
    const Exp *e = Parse(&pool, "[]");
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::LAYOUT);
    CHECK(LayoutString(e->layout) == "");
  }

  {
    const Exp *e = Parse(&pool, "(xyz, [layout], 888)");
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
    const Exp *e = Parse(&pool, "[layout[b]after]");
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
    const Exp *e = Parse(&pool, "let do u in 7 end");
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
}


int main(int argc, char **argv) {
  ANSI::Init();
  TestParse();

  printf("OK");
  return 0;
}
