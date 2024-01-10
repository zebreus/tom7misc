
// Some tests are from lug itself. See lug/LICENSE.
// lug - Embedded DSL for PE grammar parser combinators in C++
// Copyright (c) 2017 Jesse W. Towner

#include <cassert>
#include <vector>
#include <cstdint>
#include <string>
#include <string_view>

#include "lug/lug.h"
#include "base/logging.h"

LUG_DIAGNOSTIC_PUSH_AND_IGNORE;

namespace {


enum class ExpType {
  STRING,
  LIST,
  INTEGER,
  VAR,
};

struct Exp {
  ExpType type;
  std::string str;
  int64_t integer = 0;
  std::vector<Exp> children;
};

struct ExpPool {
  ExpPool() = default;

  const Exp *Str(const std::string &s) {
    Exp *ret = New(ExpType::STRING);
    ret->str = s;
    return ret;
  }

  const Exp *Var(const std::string &v) {
    Exp *ret = New(ExpType::VAR);
    ret->str = v;
    return ret;
  }

  const Exp *Int(int64_t i) {
    Exp *ret = New(ExpType::INTEGER);
    ret->integer = i;
    return ret;
  }

  const Exp *List(std::vector<Exp> v) {
    Exp *ret = New(ExpType::LIST);
    ret->children = std::move(v);
    return ret;
  }

private:
  Exp *New(ExpType t) {
    Exp *exp = new Exp;
    exp->type = t;
    return exp;
  }
  std::vector<Exp *> arena;
};

// Looks like the grammar should be defined at the top-level.
// Most of this stuff works inside a function, but:
//   - Need to explicitly capture various stuff (use default capture).
//   - Don't know how to do mutually recursive rules ("extern" trick
//     doesn't work inside a function scope).

static void TestSimpleParse() {
  using namespace lug::language;

  ExpPool pool;
  environment Env;

  // Something like "m" is a metavariable used to name a parsed
  // item. Here, for lexing, a substring of the input.
  variable<std::string_view> m{Env};

  // These metavariables name nonterminals in rules.
  variable<const Exp *> e1{Env}, e2{Env};
  variable<int64_t> n{Env};
  variable<std::string> s{Env};
  // variable<double> e{Env}, l{Env}, n{Env}, r{Env}, s{Env};
  // variable<int> i{Env};

  // These are just variables used for the example.
  // double v[26];

  // I think this is a forward declaration since the rules below
  // are mutually recursive.
  rule Expr;

  // First we have lexing.
  // I think that implicit_space_rule means whitespace that is
  // ignored outside of lexing. TODO: Find documentation for this?
  implicit_space_rule BLANK = lexeme[ *"[ \t\r\n]"_rx ];

  // leading -/+, then non-empty digits.
  // ~ is zero or one, like ? in regex.
  rule NUMBER = lexeme[ capture(m)[ ~"[-+]"_rx > +"[0-9]"_rx  ]
               <[&m]{
                   // Inside the action you need to "dereference"
                   // a variable, here to get a string_view.
                   std::string s(*m);
                   return std::stoll(s);
                 } ];

  rule ID = lexeme[ capture(m)[ "[A-Za-z_]"_rx > *"[A-Za-z0-9_]"_rx ]
                    <[&m]() -> std::string { return std::string(*m); } ];

  // TODO: String literals.

  rule Var = s%ID  <[&](environment &env) { return pool.Var(*s); };
  rule Int = n%NUMBER  <[&]{ return pool.Int(*n); };
  rule List = "[" > Expr > *(", " > Expr) > "]";

  /*rule*/ Expr = Var | Int; // | List;

  #if 0
  rule Value  = n%NUMBER         <[]{ return *n; }
        | i%ID > !"="_sx         <[]{ return v[*i]; }
        | "(" > e%Expr > ")"     <[]{ return *e; };
  rule Prod = l%Value > *(
              "*" > r%Value      <[]{ *l *= *r; }
            | "/" > r%Value      <[]{ *l /= *r; }
        )                        <[]{ return *l; };
  rule Sum  = l%Prod > *(
              "+" > r%Prod       <[]{ *l += *r; }
            | "-" > r%Prod       <[]{ *l -= *r; }
        )                        <[]{ return *l; };
  rule Expr = i%ID > "=" > s%Sum     <[]{ return v[*i] = *s; }
        | s%Sum                  <[]{ return *s; };
  rule Stmt = (   "quit"_isx         <[]{ std::exit(EXIT_SUCCESS); }
            | e%Expr             <[]{ std::cout << *e << std::endl; }
        ) > EOL
        | *( !EOL > any ) > EOL  <[]{ std::cerr << "syntax error" << std::endl; };
  #endif

  const Exp *out = nullptr;
  rule Program = e1%Expr > eoi   <[&out, &e1]{ out = *e1; };

  grammar Grammar = start(Program);

  /*
    If you get this:

    terminate called after throwing an instance of 'lug::accept_context_error'
      what():  operation valid only inside calling context of parser::accept

    It's probably because you tried calling the parser without an
    environment. lug::parse(s, G) doesn't pass one.
  */
  auto Parse = [&](const std::string &s) -> const Exp * {
      lug::parser p{Grammar, Env};
      if (!p.parse(std::begin(s), std::end(s))) return nullptr;
      return out;
    };

  const Exp *e = Parse("15232");
  CHECK(e != nullptr);
  CHECK(e->type == ExpType::INTEGER);
  CHECK(e->integer == 15232);
}

// Tests below are from lug itself.


// leftrecursion.cpp

void test_direct_left_recursion() {
  using namespace lug::language;
  grammar::implicit_space = nop;
  rule R = R > chr('a') | chr('a');
  rule S = R > !chr('a');
  grammar G = start(S);
  assert(lug::parse("a", G));
  assert(lug::parse("aa", G));
  assert(lug::parse("aab", G));
  assert(lug::parse("aaa", G));
  assert(lug::parse("aaa2", G));
  assert(lug::parse("aaaa", G));
  assert(lug::parse("aaaak", G));
  assert(!lug::parse("", G));
  assert(!lug::parse("b", G));
}

void test_indirect_left_recursion() {
  using namespace lug::language;
  grammar::implicit_space = nop;
  rule Q, R, S;
  Q = R > chr('a');
  R = Q | chr('a');
  S = R > !chr('a');
  grammar G = start(S);
  assert(lug::parse("a", G));
  assert(lug::parse("aa", G));
  assert(lug::parse("aab", G));
  assert(lug::parse("aaa", G));
  assert(lug::parse("aaa2", G));
  assert(lug::parse("aaaa", G));
  assert(lug::parse("aaaak", G));
  assert(!lug::parse("", G));
  assert(!lug::parse("b", G));
}

void test_association_and_precedence() {
  using namespace lug::language;
  grammar::implicit_space = nop;
  std::string out;
  rule N  = chr('1') | chr('2') | chr('3');
  rule E  = E[1] > chr('+') > E[2] <[&out]{ out += '+'; }
          | E[2] > chr('*') > E[3] <[&out]{ out += '*'; }
          | N <[&out](csyntax& x){ out += x.capture(); };
  rule S = E > eoi;
  grammar G = start(S);
  out.clear();
  assert(lug::parse("1", G) && out == "1");
  out.clear();
  assert(lug::parse("1+2", G) && out == "12+");
  out.clear();
  assert(lug::parse("3*1", G) && out == "31*");
  out.clear();
  assert(lug::parse("1*2+3*2", G) && out == "12*32*+");
  out.clear();
  assert(lug::parse("2+2*3+1", G) && out == "223*+1+");
  out.clear();
  assert(lug::parse("2+2*3+1*2*3+1", G) && out == "223*+12*3*+1+");
  assert(!lug::parse("", G));
  assert(!lug::parse("a", G));
  assert(!lug::parse("1+", G));
}

static void TestLeftRecursion() {
  try {
    test_direct_left_recursion();
    test_indirect_left_recursion();
    test_association_and_precedence();
  } catch (std::exception& e) {
    LOG(FATAL) << "Error: " << e.what() << std::endl;
  }
}

// nonterminals.cpp

void test_sequence()
{
  using namespace lug::language;
  rule S = noskip[ chr('a') > any > chr('b') > eoi ];
  grammar G = start(S);
  assert(lug::parse("a2b", G));
  assert(lug::parse("azb", G));
  assert(!lug::parse("", G));
  assert(!lug::parse("a", G));
  assert(!lug::parse("aza", G));
  assert(!lug::parse("azb3", G));
  assert(!lug::parse("a z b", G));
  assert(!lug::parse(" a z b", G));
}

void test_choice()
{
  using namespace lug::language;
  rule S = noskip[ (chr('a') | chr('b')) > eoi ];
  grammar G = start(S);
  assert(lug::parse("a", G));
  assert(lug::parse("b", G));
  assert(!lug::parse("", G));
  assert(!lug::parse("ab", G));
  assert(!lug::parse("ba", G));
  assert(!lug::parse(" a", G));
}

void test_zero_or_one()
{
  using namespace lug::language;
  rule S = noskip[ ~chr('x') > eoi ];
  grammar G = start(S);
  assert(lug::parse("", G));
  assert(lug::parse("x", G));
  assert(!lug::parse("xx", G));
  assert(!lug::parse("xxxxxxx", G));
  assert(!lug::parse("y", G));
  assert(!lug::parse("xy", G));
  assert(!lug::parse("xxxxxy", G));
}

void test_zero_or_many()
{
  using namespace lug::language;
  rule S = noskip[ *chr('x') > eoi ];
  grammar G = start(S);
  assert(lug::parse("", G));
  assert(lug::parse("x", G));
  assert(lug::parse("xx", G));
  assert(lug::parse("xxxxxxx", G));
  assert(!lug::parse("y", G));
  assert(!lug::parse("xy", G));
  assert(!lug::parse("xxxxxy", G));
}

void test_one_or_many()
{
  using namespace lug::language;
  rule S = noskip[ +chr('x') > eoi ];
  grammar G = start(S);
  assert(lug::parse("x", G));
  assert(lug::parse("xx", G));
  assert(lug::parse("xxxxxxx", G));
  assert(!lug::parse("", G));
  assert(!lug::parse("y", G));
  assert(!lug::parse("xy", G));
  assert(!lug::parse("xxxxxy", G));
  assert(!lug::parse(" xx", G));
  assert(!lug::parse("x x", G));
  assert(!lug::parse("xx ", G));
}

void test_not()
{
  using namespace lug::language;
  rule S = noskip[ !chr('x') > any > eoi ];
  grammar G = start(S);
  assert(lug::parse("y", G));
  assert(lug::parse("Z", G));
  assert(lug::parse("2", G));
  assert(!lug::parse("", G));
  assert(!lug::parse("x", G));
  assert(!lug::parse("xx", G));
  assert(!lug::parse("y2", G));
  assert(!lug::parse("yx", G));
  assert(!lug::parse(" 2", G));
}

void test_predicate()
{
  using namespace lug::language;
  rule S = noskip[ &chr('x') > any > any > eoi ];
  grammar G = start(S);
  assert(lug::parse("xx", G));
  assert(lug::parse("xy", G));
  assert(lug::parse("xZ", G));
  assert(lug::parse("x2", G));
  assert(!lug::parse("", G));
  assert(!lug::parse("y", G));
  assert(!lug::parse("Z", G));
  assert(!lug::parse("2", G));
  assert(!lug::parse("x", G));
  assert(!lug::parse("y2", G));
  assert(!lug::parse(" xx", G));
  assert(!lug::parse("x  x", G));
}

static void TestNonterminals() {
  try {
    test_sequence();
    test_choice();
    test_zero_or_one();
    test_zero_or_many();
    test_one_or_many();
    test_not();
    test_predicate();
  } catch (std::exception& e) {
    LOG(FATAL) << "Error: " << e.what() << std::endl;
  }
}

// parser.cpp

using namespace std::string_view_literals;

// Port note: These tried to create utf-8 string views with
// u8R"(xyz)"sv but I think something changed in c++20 and
// it wasn't compiling. Instead I'm using regular string
// literals with explicit newline/tab escapes, and converting
// to string_view below.

static constexpr const char *sentences1_text =
  "The stranger officiates the meal.\n"
  "She was too short to see over the fence.\n"
  "This is the last random sentence I will be writing and I am going to stop mid-sent\n"
  "It was getting dark, and we weren't there yet.\n"
  "There were white out conditions in the town; subsequently, the roads were impassable.\n"
  "I really want to go to work, but I am too sick to drive.\n"
  "I am counting my calories, yet I really want dessert.\n"
  "I checked to make sure that he was still alive.\n"
  "I love eating toasted cheese and tuna sandwiches.\n"
  "Everyone was busy, so I went to the movie alone.\n"
  "He got a small piece of pie.\n"
  "What was the person thinking when they discovered cow's milk was fine for human consumption... and why did they do it in the first place!?\n"
  "Where do random thoughts come from?\n"
  "If Purple People Eaters are real... where do they find purple people to eat?\n"
  "He turned in the research paper on Friday; otherwise, he would have not passed the class.\n"
  "How was the math test?\n"
  "The mysterious diary records the voice.\n"
  "She works two jobs to make ends meet; at least, that was her reason for not having time to join us.\n"
  "I want more detailed information.\n"
  "\tHe told us a very exciting adventure story.";

static constexpr const char *sentences2_text =
  "\n"
  "There were white out conditions in the town; subsequently, the roads were impassable.\n"
  "He told us a very exciting adventure story.\n"
  "The sky is clear; the stars are twinkling.\n"
  "Let me help you with your baggage.\n"
  "The stranger officiates the meal.\n"
  "The rocks are approaching at high velocity.\n"
  "Abstraction is often one floor above you.\n";

void test_line_column_tracking() {
  std::array<lug::syntax_position, 4> startpos, endpos;
  lug::grammar G;

  {
    using namespace lug::language;

    rule Word = lexeme[
          str("officiates") < [&](csyntax& x) { startpos[0] = x.start(); endpos[0] = x.end(); }
        | str("Everyone") < [&](csyntax& x) { endpos[1] = x.end(); startpos[1] = x.start();  }
        | str("Friday") < [&](csyntax& x) { startpos[2] = x.start(); endpos[2] = x.end(); }
        | str("story") < [&](csyntax& x) { endpos[3] = x.end(); startpos[3] = x.start(); }
        | +alpha
      ];

    G = start(*(Word | punct) > eoi);
  }

  lug::environment E;
  lug::parser p{G, E};

  std::string_view sentences1(sentences1_text);
  std::string_view sentences2(sentences2_text);

  bool success = p.parse(std::begin(sentences1), std::end(sentences1));
  assert(success);
  assert(p.match() == sentences1);

  assert(startpos[0].line == 1 && startpos[0].column == 14);
  CHECK(startpos[1].line == 10) << startpos[1].line;
  CHECK(startpos[1].column == 1) << startpos[1].column;
  assert(startpos[2].line == 15 && startpos[2].column == 36);
  CHECK(startpos[3].line == 20) << startpos[3].line;
  CHECK(startpos[3].column == 46) << startpos[3].column;

  assert(endpos[0].line == 1 && endpos[0].column == 24);
  assert(endpos[1].line == 10 && endpos[1].column == 9);
  assert(endpos[2].line == 15 && endpos[2].column == 42);
  assert(endpos[3].line == 20 && endpos[3].column == 51);

  assert(p.max_subject_index() == sentences1.size());
  assert(p.max_subject_position().line == 20 && p.max_subject_position().column == 52);

  success = p.parse(std::begin(sentences2), std::end(sentences2));
  assert(success);
  assert(p.match() == sentences2);

  CHECK(startpos[0].line == 25) << startpos[0].line;
  CHECK(startpos[0].column == 14) << startpos[0].column;
  assert(startpos[3].line == 22 && startpos[3].column == 38);
  assert(endpos[0].line == 25 && endpos[0].column == 24);
  assert(endpos[3].line == 22 && endpos[3].column == 43);

  assert(p.max_subject_index() == sentences2.size());
  assert(p.max_subject_position().line == 28 && p.max_subject_position().column == 1);
}

static void TestParser() {
  try {
    test_line_column_tracking();
  } catch (std::exception& e) {
    LOG(FATAL) << "Error: " << e.what() << std::endl;
  }
}

// predicates.cpp

void test_simple_predicates() {
  using namespace lug::language;
  rule S = "a"_sx > []{ return false; } | []{ return true; } > "ab";
  grammar G = start(S > eoi);
  assert(!lug::parse("a", G));
  assert(lug::parse("ab", G));
}

void test_subject_index_predicate() {
  using namespace lug::language;
  rule S = +("a"_sx > [](parser& p){ return p.subject_index() <= 4; });
  grammar G = start(S > eoi);
  assert(!lug::parse("", G));
  assert(!lug::parse("b", G));
  assert(lug::parse("a", G));
  assert(lug::parse("aa", G));
  assert(lug::parse("aaa", G));
  assert(lug::parse("aaaa", G));
  assert(!lug::parse("aaaaa", G));
}

static void TestPredicates() {
  try {
    test_simple_predicates();
    test_subject_index_predicate();
  } catch (std::exception& e) {
    LOG(FATAL) << e.what() << std::endl;
  }
}

// terminals.hpp

void test_empty() {
  using namespace lug::language;
  rule S = noskip[ eps > eoi ];
  grammar G = start(S);
  assert(lug::parse("", G));
  assert(!lug::parse("a", G));
  assert(!lug::parse("2", G));
  assert(!lug::parse("z", G));
  assert(!lug::parse("aa", G));
}

void test_any() {
  using namespace lug::language;
  rule S = noskip[ any > eoi ];
  grammar G = start(S);
  assert(lug::parse("a", G));
  assert(lug::parse("2", G));
  assert(lug::parse("z", G));
  assert(!lug::parse("aa", G));
  assert(!lug::parse("", G));
  assert(!lug::parse(" a", G));
}

void test_char() {
  using namespace lug::language;
  rule S = noskip[ chr('a') > eoi ];
  grammar G = start(S);
  assert(lug::parse("a", G));
  assert(!lug::parse("", G));
  assert(!lug::parse("2", G));
  assert(!lug::parse("aa", G));
  assert(!lug::parse("b", G));
  assert(!lug::parse(" a", G));
}

void test_char_range() {
  using namespace lug::language;

  rule S1 = noskip[ chr('d', 'g') > eoi ];
  grammar G1 = start(S1);
  assert(lug::parse("d", G1));
  assert(lug::parse("e", G1));
  assert(lug::parse("f", G1));
  assert(lug::parse("g", G1));
  assert(!lug::parse("", G1));
  assert(!lug::parse("c", G1));
  assert(!lug::parse("h", G1));
  assert(!lug::parse("dd", G1));
  assert(!lug::parse(" e", G1));
  assert(!lug::parse("f ", G1));
  assert(!lug::parse(" g ", G1));

  rule S2 = noskip[ chr('b', 'b') > eoi ];
  grammar G2 = start(S2);
  assert(lug::parse("b", G2));
  assert(!lug::parse("", G2));
  assert(!lug::parse("a", G2));
  assert(!lug::parse("c", G2));
  assert(!lug::parse("bb", G2));
  assert(!lug::parse(" b", G2));
  assert(!lug::parse("b ", G2));
}

void test_string() {
  using namespace lug::language;
  rule S = noskip[ str("hello world") > eoi ];
  grammar G = start(S);
  assert(lug::parse("hello world", G));
  assert(!lug::parse("hello world!", G));
  assert(!lug::parse("hello", G));
  assert(!lug::parse("h", G));
  assert(!lug::parse("", G));
}

void test_regular_expression() {
  using namespace lug::language;
  rule S = noskip[ bre("hello.w[oO]rld[[:digit:]]") > eoi ];
  grammar G = start(S);
  assert(lug::parse("hello world4", G));
  assert(lug::parse("hello_wOrld8", G));
  assert(!lug::parse("hello world!", G));
  assert(!lug::parse("hello", G));
  assert(!lug::parse("h", G));
  assert(!lug::parse("", G));
}

static void TestTerminals() {
  try {
    test_empty();
    test_any();
    test_char();
    test_char_range();
    test_string();
    test_regular_expression();
  } catch (std::exception& e) {
    LOG(FATAL) << e.what() << std::endl;
  }
}

}  // namespace

int main(int argc, char **argv) {
  TestLeftRecursion();
  TestNonterminals();
  TestParser();
  TestPredicates();
  TestTerminals();

  TestSimpleParse();

  printf("OK\n");
}
