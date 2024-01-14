
#include "ctpg.h"

#include <string>
#include <vector>
#include <deque>
#include <cstdint>

#include "ast.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "ansi.h"

static const Exp *Parse(AstPool *pool, const std::string &input) {
  using namespace ctpg;
  using namespace ctpg::buffers;
  using namespace ctpg::ftors;

  // Parsing ignores whitespace. But we care about whitespace inside
  // string literals and layout. So these need to be parsed as
  // terminals.

  static constexpr char strlit_pattern[] =
    // Starting with double quote
    "\""
    // Then some number of characters.
    "("
    // Any character other than " or newline or backslash.
    R"([^"\\\r\n])"
    "|"
    // Or an escaped character. This is permissive; we parse
    // the string literals and interpret escape characters
    // separately.
    R"(\\.)"
    ")*"
    "\"";

  static constexpr regex_term<strlit_pattern> strlit("strlit");

  //   #define ANY_BRACKET_RE "[\\\[\\\]]"
  // Match any raw character except [], or any escaped character.
#if 0
#define LAYOUT_CHARS "("  \
      R"([^\[\]\\])" "|"  \
      R"(\\.)"             \
      ")*"
#else
  #define LAYOUT_CHARS "[a-z]*"
#endif

  // Similarly, we care about whitespace inside layout. This is the
  // inside of the layout. Annoyingly, we need to include the
  // delimiter at the beginning at least, or else we have recognize
  // this "token" everywhere (as it can be blank). But the delimeters
  // can be any combination of []. So we have two different terminals
  // here.
  static constexpr char leading_layout_pattern[] =
    "\\[" LAYOUT_CHARS;
  static constexpr char trailing_layout_pattern[] =
    "\\]" LAYOUT_CHARS;

  static constexpr regex_term<leading_layout_pattern>
    leading_layout("leading_layout");
  static constexpr regex_term<trailing_layout_pattern>
    trailing_layout("trailing_layout");

  static constexpr nterm<const Exp *> exp("exp");
  static constexpr nterm<std::deque<const Exp *>>
    comma_separated_exp("comma_separated_exp");
  static constexpr nterm<std::deque<const Exp *>>
    comma_continued_exp("comma_continued_exp");

  static constexpr nterm<const Layout *> layout("layout");
  static constexpr nterm<const Layout *> after_layout("after_layout");

  static constexpr char digits_pattern[] = "[1-9][0-9]*";
  static constexpr regex_term<digits_pattern> digits("digits");

  static constexpr char id_pattern[] = "[A-Za-z_][A-Za-z0-9_]*";
  static constexpr regex_term<id_pattern> id("id");

  static constexpr parser p(
      // This is the grammar root. We're parsing an expression.
      exp,
      // All the terminal symbols. Characters and strings implicitly
      // stand for themselves.
      terms(',', '(', ')', leading_layout, trailing_layout,
            // must be lower precedence than the layout tokens
            '[', ']',
            digits, id, strlit),
      nterms(exp, comma_separated_exp, comma_continued_exp,
             layout, after_layout),

      rules(
          comma_continued_exp() >= []() {
              return std::deque<const Exp *>({});
            },
          comma_continued_exp(',', exp, comma_continued_exp) >=
          [](auto, const Exp *e, std::deque<const Exp *> &&v) {
            v.push_front(e);
            return std::move(v);
          },

          comma_separated_exp() >= []() {
              return std::deque<const Exp *>({});
            },
          comma_separated_exp(exp, comma_continued_exp) >=
          [](const Exp *e, std::deque<const Exp *> &&v) {
              v.push_front(e);
              return std::move(v);
            },

          exp(digits) >>= [](auto &ctx, std::string_view d) {
              return ctx->Int(std::stoll(std::string(d)));
            },
          exp(id) >>= [](auto &ctx, std::string_view d) {
              return ctx->Var(std::string(d));
            },
          exp(strlit) >>= [](auto &ctx,
                             std::string_view s) {
              // XXX interpret escapes here?
              CHECK(s.size() >= 2);
              return ctx->Str((std::string)s.substr(1, s.size() - 2));
            },

          exp('(', comma_separated_exp, ')')
          >>= [](auto &ctx, auto _l,
                 std::deque<const Exp *> &&d,
                 auto _r) {
              std::vector<const Exp *> v(d.begin(), d.end());
              return ctx->Tuple(std::move(v));
            },

          // [layoutAFTER
          // where AFTER is defined below...
          exp(leading_layout, after_layout)
          >>= [](auto &ctx, std::string_view d, const Layout *lay1) {
              // Skip leading [.
              const std::string s = (std::string)d.substr(1);
              printf("Leading layout: __%s__\n", s.c_str());
              const Layout *lay2 = ctx->TextLayout(std::move(s));
              return ctx->LayoutExp(ctx->JoinLayout({lay1, lay2}));
            },

          // AFTER can either just end the layout,
          after_layout(']') >>= [](auto &ctx, auto) {
              return ctx->TextLayout("");
            }

          #if 0
          ,
          // or enter another nested expression, and continue
          after_layout('[', exp, trailing_layout, after_layout)
          >>= [](auto &ctx, auto, const Exp *exp,
                 std::string_view d, const Layout *lay2) {
              // skip the trailing ]
              std::string s = (std::string)d.substr(1);
              return ctx->JoinLayout(
                  {ctx->ExpLayout(exp),
                   ctx->TextLayout(std::move(s)),
                   lay2});
            }
          #endif
     )
  );

  printf(ABGCOLOR(60, 60, 200, "==== GRAMMAR ====") "\n");
  p.write_diag_str(std::cout);
  printf(ABGCOLOR(60, 60, 200, "==== END ====") "\n");

  auto Parse = [&](std::string s) {
      printf("Parsing %s...\n", s.c_str());
      auto res = p.context_parse(pool,
                                 parse_options{}.set_skip_whitespace(true).
                                 set_verbose(),
                                 string_buffer(std::move(s)),
                                 std::cerr);
      CHECK(res.has_value());
      printf("  Got: %s\n", ExpString(res.value()).c_str());
      return res.value();
    };

  return Parse(input);
}

static void Test() {
  AstPool pool;
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
    const Exp *e = Parse(&pool, "()");
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::TUPLE);
    CHECK(e->children.size() == 0);
  }

  {
    const Exp *e = Parse(&pool, "(var,123,456)");
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::TUPLE);
    CHECK(e->children.size() == 3);
    CHECK(e->children[0]->str == "var");
    CHECK(e->children[1]->integer == 123);
    CHECK(e->children[2]->integer == 456);
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
    const Layout *lay =e->children[1]->layout;
    CHECK(lay->type == LayoutType::TEXT);
    CHECK(lay->str == "layout");
    CHECK(e->children[2]->integer == 888);
  }


}


// XXX to test
int main(int argc, char **argv) {
  Test();

  printf("OK");
  return 0;
}
