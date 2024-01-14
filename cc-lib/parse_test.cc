
#include "parse.h"

#include <span>

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
    CHECK(po.Start() == 0);
    CHECK(po.Length() == 1);
  }

  {
    std::string s("asdf");
    auto po = Succeed<char, int>(5)(CharSpan(s));
    CHECK(po.HasValue());
    CHECK(po.Start() == 0);
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
    CHECK(po.Start() == 0);
    CHECK(po.Length() == 0);
  }

  {
    auto parser = Any<char>() >> Succeed<char, int>(5);
    std::string s("a");
    auto po = parser(CharSpan(s));
    CHECK(po.HasValue());
    CHECK(po.Value() == 5);
    CHECK(po.Start() == 1) << po.Start();
    CHECK(po.Length() == 0);
  }

  {
    auto parser = Any<char>() << Succeed<char, int>(5);
    std::string s("ab");
    auto po = parser(CharSpan(s));
    CHECK(po.HasValue());
    CHECK(po.Value() == 'a');
    CHECK(po.Start() == 0) << po.Start();
    CHECK(po.Length() == 1);
  }

}

int main(int argc, char **argv) {
  TestSimple();

  printf("OK\n");
  return 0;
}
