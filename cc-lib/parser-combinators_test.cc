
#include "parser-combinators.h"

#include <span>
#include <string>

#include "base/logging.h"
#include "base/stringprintf.h"

std::span<const char> CharSpan(const std::string &s) {
  return std::span<const char>(s.begin(), s.end());
}

static void TestSimple() {
  {
    std::string empty;
    auto po = Any<char>()(CharSpan(empty));
    CHECK(!po.HasValue());
  }

  {
    std::string s("asdf");
    auto po = Any<char>()(CharSpan(s));
    CHECK(po.HasValue());
    CHECK(po.Value() == 'a') << po.Value();
    CHECK(po.Length() == 1);
  }

  {
    std::string s("asdf");
    auto po = Succeed<char, int>(5)(CharSpan(s));
    CHECK(po.HasValue());
    CHECK(po.Length() == 0);
    CHECK(po.Value() == 5);
  }

  {
    std::string s("asdf");
    auto po = End<char>()(CharSpan(s));
    CHECK(!po.HasValue());
  }

  {
    std::string empty;
    auto po = End<char>()(CharSpan(empty));
    CHECK(po.HasValue());
    CHECK(po.Length() == 0);
  }

  {
    std::string empty;
    auto po = Is<char>('x')(CharSpan(empty));
    CHECK(!po.HasValue());
    CHECK(po.Length() == 0);
  }

  {
    std::string s = "abc";
    auto po = Is<char>('x')(CharSpan(s));
    CHECK(!po.HasValue());
    CHECK(po.Length() == 0);
  }

  {
    std::string s = "abc";
    auto po = Is<char>('a')(CharSpan(s));
    CHECK(po.HasValue());
    CHECK(po.Length() == 1);
  }

  {
    auto parser = Any<char>() >> Succeed<char, int>(5);
    std::string s("a");
    auto po = parser(CharSpan(s));
    CHECK(po.HasValue());
    CHECK(po.Value() == 5);
    CHECK(po.Length() == 1);
  }

  {
    auto parser = Any<char>() << Is('b');
    std::string s("ab");
    auto po = parser(CharSpan(s));
    CHECK(po.HasValue());
    CHECK(po.Value() == 'a');
    CHECK(po.Length() == 2);
  }

  {
    auto parser = Is('a') && Any<char>();
    std::string s("abc");
    auto po = parser(CharSpan(s));
    CHECK(po.HasValue());
    CHECK(po.Value().first == 'a');
    CHECK(po.Value().second == 'b');
    CHECK(po.Length() == 2);
  }

  {
    auto parser = Any<char>() && Any<char>();
    std::string s("abc");
    auto po = parser(CharSpan(s));
    CHECK(po.HasValue());
    CHECK(po.Value().first == 'a');
    CHECK(po.Value().second == 'b');
    CHECK(po.Length() == 2);
  }

  {
    auto parser = Opt(Is('x')) && Is('a');
    std::string s("abc");
    auto po = parser(CharSpan(s));
    CHECK(po.HasValue());
    // doesn't find x, but succeeds with nullopt
    CHECK(!po.Value().first.has_value());
    CHECK(po.Value().second == 'a');
    CHECK(po.Length() == 1);
  }

  {
    auto parser = Opt(Is('a')) && Is('b');
    std::string s("abc");
    auto po = parser(CharSpan(s));
    CHECK(po.HasValue());
    CHECK(po.Value().first.has_value());
    CHECK(po.Value().first.value() == 'a');
    CHECK(po.Value().second == 'b');
    CHECK(po.Length() == 2);
  }

  {
    auto parser = *Is('x') && Is('a');
    std::string s("xxxxxa");
    auto po = parser(CharSpan(s));
    CHECK(po.HasValue());
    // doesn't find x, but succeeds with nullopt
    CHECK(po.Value().first.size() == 5);
    for (int i = 0; i < 5; i++) {
      CHECK(po.Value().first[i] == 'x');
    }
    CHECK(po.Value().second == 'a');
    CHECK(po.Length() == 6);
  }

  {
    auto parser = *Is('x') >[](const std::vector<char> &v) {
        return 2 * v.size();
      };
    std::string s("xxxxx");
    Parsed<size_t> po = parser(CharSpan(s));
    CHECK(po.HasValue());
    CHECK(po.Value() == 10);
    CHECK(po.Length() == 5);
  }

  {
    auto parser = +Is('x') >[](const std::vector<char> &v) {
        return 2 * v.size();
      };
    std::string s("xxxxx");
    Parsed<size_t> po = parser(CharSpan(s));
    CHECK(po.HasValue());
    CHECK(po.Value() == 10);
    CHECK(po.Length() == 5);
  }

  {
    auto parser = +Is('x');
    std::string empty;
    Parsed<std::vector<char>> po = parser(CharSpan(empty));
    CHECK(!po.HasValue());
  }

  {
    auto parser = Fix<char, char>([](const auto &Self) {
        return (Is('(') >> Self << Is(')')) || Is('x');
      });

    for (const char *c : {"x", "(x)", "(((((x)))))"}) {
      std::string s = c;
      Parsed<char> po = parser(CharSpan(s));
      CHECK(po.HasValue());
      CHECK(po.Value() == 'x');
    }
  }

  {
    auto abc = Is('a') || Is('b') || Is('c');
    auto parser = Separate(abc, Is(','));
    std::string s = "a,b,c,x";
    Parsed<std::vector<char>> po = parser(CharSpan(s));
    CHECK(po.HasValue());
    const auto &v = po.Value();
    CHECK(v.size() == 3);
    CHECK(v[0] == 'a');
    CHECK(v[1] == 'b');
    CHECK(v[2] == 'c');
  }

  {
    auto abc = Is('a') || Is('b') || Is('c');
    auto parser = Separate0(abc, Is(','));
    std::string s = "x";
    Parsed<std::vector<char>> po = parser(CharSpan(s));
    CHECK(po.HasValue());
    CHECK(po.Value().empty());
  }

  #if 0
  TupleLike<std::string> like;
  const auto &[x, y] = like;
  CHECK(x == 8);
  CHECK(y == 10);
  #endif

  {
    const auto &[E, D] =
      Fix2<char, char, char>(
        [](const auto &EE, const auto &DD) {
          return Is('x');
        },
        [](const auto &EE, const auto &DD) {
          return Is('y');
        });
  }

  {
    // just using the first characters
    // 'l'et D
    // 'i'n E
    // 'e'nd
    // or 0
    using exp_out = std::string;
    using dec_out = std::pair<std::string, std::string>;
    const auto &[E, D] =
      Fix2<char, exp_out, dec_out>(
        [](const auto &EE, const auto &DD) {
          auto zero = Is<char>('0') >> Succeed<char, std::string>("0");
          auto let =
            (((Is<char>('l') >> DD) && (Is('i') >> EE)) << Is('e')) >
              [](const auto &p) -> std::string {
                  const auto &[var, exp] = p.first;
                  return StringPrintf("let val %s = %s in %s end",
                                      var.c_str(),
                                      exp.c_str(),
                                      p.second.c_str());
              };
          return zero || let;
        },
      // 'v'al x = E
        [](const auto &EE, const auto &DD) {
          return (Is('v') >> Is('x') >> Is('=') >> EE) >
            [](const std::string &e) -> std::pair<std::string, std::string> {
            return std::make_pair("x", e);
            };
        });

    {
      Parsed<std::string> po = E(CharSpan(""));
      CHECK(!po.HasValue());
    }

    {
      Parsed<std::string> po = E(CharSpan("0"));
      CHECK(po.HasValue());
      CHECK(po.Value() == "0");
    }

    {
      Parsed<std::string> po = E(CharSpan("lvx=0i0e"));
      CHECK(po.HasValue());
      CHECK(po.Value() == "let val x = 0 in 0 end");
    }

  }
}

static void TestFixity() {
  // Parse a bunch of operators and expressions in a sequence.
  using Item = FixityItem<std::string>;
  Item plus;
  plus.fixity = Fixity::Infix;
  plus.assoc = Associativity::Left;
  plus.item = "UNUSED";
  plus.precedence = 3;
  plus.binop = [](const std::string &a, const std::string &b) {
      return StringPrintf("Plus(%s, %s)", a.c_str(), b.c_str());
    };

  Item minus;
  minus.fixity = Fixity::Infix;
  minus.assoc = Associativity::Left;
  minus.item = "UNUSED";
  minus.precedence = 3;
  minus.binop = [](const std::string &a, const std::string &b) {
      return StringPrintf("Minus(%s, %s)", a.c_str(), b.c_str());
    };

  Item times;
  times.fixity = Fixity::Infix;
  times.assoc = Associativity::Left;
  times.item = "UNUSED";
  times.precedence = 5;
  times.binop = [](const std::string &a, const std::string &b) {
      return StringPrintf("Times(%s, %s)", a.c_str(), b.c_str());
    };

  Item bang;
  bang.fixity = Fixity::Prefix;
  bang.item = "UNUSED";
  bang.precedence = 7;
  bang.unop = [](const std::string &a) {
      return StringPrintf("Bang(%s)", a.c_str());
    };

  Item huh;
  huh.fixity = Fixity::Postfix;
  huh.item = "UNUSED";
  huh.precedence = 7;
  huh.unop = [](const std::string &a) {
      return StringPrintf("Huh(%s)", a.c_str());
    };

  auto Atom = [](const std::string &var) {
      Item atom;
      atom.fixity = Fixity::Atom;
      atom.item = var;
      return atom;
    };

  /*
#define CHECK_FIXITY(expected_, ...) do { \
    const std::string expected = expected_; \
    const std::vector<Item> items = {__VA_ARGS__}; \
    std::string parse_error;                        \
    const std::optional<std::string> got =          \
      ResolveFixity<std::string>(items, &parse_error);  \
    const char *err = # __VA_ARGS__ ; \
  if (expected == "FAIL") {                            \
    CHECK(!got.has_value()) << "For:\n" << err         \
                            << "\nExpected failure!\n"   \
                            << got.value();              \
    } else {                                           \
    CHECK(got.has_value()) << "For:\n" << err \
                           << "\nExpected success:\n"         \
                           << expected                        \
                           << "\nBut it did not parse:\n"     \
                           << parse_error << "\n";            \
    CHECK(got.value() == expected) << "For:\n" << err            \
                                   << "\nExpected:\n"            \
                                   << expected                \
                                   << "\nBut got:\n"          \
                                   << got.value();            \
    }                                                         \
 } while (0)
  */

  auto NoAdj = [](const std::string &a, const std::string &b) ->
    std::string {
      LOG(FATAL) << "Adj should not be called";
    };

#define CHECK_FIXITY_ADJ(expected_, adj_assoc_, adj_op, ...) do { \
    const Associativity adj_assoc = (adj_assoc_);                 \
  const std::string expected = (expected_);                      \
    const std::vector<Item> items = {__VA_ARGS__};               \
    std::string parse_error;                                     \
    const std::optional<std::string> got =                       \
      (adj_assoc == Associativity::Non) ?                        \
      ResolveFixity<std::string>(items, &parse_error) :          \
      ResolveFixityAdj<std::string>(items, adj_assoc,            \
                                    adj_op, &parse_error);       \
    const char *err = # __VA_ARGS__ ;                            \
    if (expected == "FAIL") {                                    \
      CHECK(!got.has_value()) << "For:\n" << err                 \
                              << "\nExpected failure!\n"         \
                              << got.value();                    \
    } else {                                                     \
      CHECK(got.has_value()) << "For:\n" << err                  \
                             << "\nExpected success:\n"          \
                             << expected                         \
                             << "\nBut it did not parse:\n"      \
                             << parse_error << "\n";             \
      CHECK(got.value() == expected) << "For:\n" << err          \
                                     << "\nExpected:\n"          \
                                     << expected                 \
                                     << "\nBut got:\n"           \
                                     << got.value();             \
    }                                                            \
  } while (0)

#define CHECK_FIXITY(expected_, ...) do { \
    CHECK_FIXITY_ADJ(expected_, Associativity::Non, NoAdj, __VA_ARGS__); \
  } while (0)

  CHECK_FIXITY("x", Atom("x"));
  CHECK_FIXITY("FAIL", Atom("x"), Atom("y"));
  CHECK_FIXITY("FAIL", plus, plus);
  CHECK_FIXITY("FAIL", plus);
  CHECK_FIXITY("FAIL", huh);
  CHECK_FIXITY("FAIL", huh, Atom("x"));
  CHECK_FIXITY("FAIL", bang);
  CHECK_FIXITY("FAIL", Atom("x"), bang);
  CHECK_FIXITY("Bang(x)", bang, Atom("x"));
  CHECK_FIXITY("Huh(x)", Atom("x"), huh);
  CHECK_FIXITY("Plus(x, y)", Atom("x"), plus, Atom("y"));
  CHECK_FIXITY("Minus(x, y)", Atom("x"), minus, Atom("y"));
  CHECK_FIXITY("Times(x, y)", Atom("x"), times, Atom("y"));
  CHECK_FIXITY("Times(x, Bang(y))", Atom("x"), times, bang, Atom("y"));
  CHECK_FIXITY("Times(Huh(x), Bang(y))",
               Atom("x"), huh, times, bang, Atom("y"));
  CHECK_FIXITY("Plus(Times(x, y), z)",
               Atom("x"), times, Atom("y"), plus, Atom("z"));
  CHECK_FIXITY("Plus(x, Times(y, z))",
               Atom("x"), plus, Atom("y"), times, Atom("z"));
  CHECK_FIXITY("Plus(Plus(x, y), z)",
               Atom("x"), plus, Atom("y"), plus, Atom("z"));

  auto Adj = [&](const std::string &a, const std::string &b) {
      return StringPrintf("App(%s, %s)", a.c_str(), b.c_str());
    };

  CHECK_FIXITY_ADJ("App(f, x)", Associativity::Left, Adj,
                   Atom("f"), Atom("x"));
  CHECK_FIXITY_ADJ("App(f, x)", Associativity::Right, Adj,
                   Atom("f"), Atom("x"));
  CHECK_FIXITY_ADJ("App(App(map, f), l)", Associativity::Left, Adj,
                   Atom("map"), Atom("f"), Atom("l"));
  CHECK_FIXITY_ADJ("App(map, App(f, l))", Associativity::Right, Adj,
                   Atom("map"), Atom("f"), Atom("l"));
  CHECK_FIXITY_ADJ("Plus(App(f, x), App(g, y))", Associativity::Left, Adj,
                   Atom("f"), Atom("x"), plus, Atom("g"), Atom("y"));


}

int main(int argc, char **argv) {
  TestSimple();
  TestFixity();

  printf("OK\n");
  return 0;
}
