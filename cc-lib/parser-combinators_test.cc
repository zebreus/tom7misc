
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

}

int main(int argc, char **argv) {
  TestSimple();

  printf("OK\n");
  return 0;
}
