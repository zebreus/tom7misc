
#include "ctpg.h"

#include <string>
#include <vector>
#include <cstdint>

#include "base/stringprintf.h"
#include "base/logging.h"

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
  std::vector<const Exp *> children;
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

  const Exp *List(std::vector<const Exp *> v) {
    Exp *ret = New(ExpType::LIST);
    ret->children = std::move(v);
    return ret;
  }

  ~ExpPool() {
    for (const Exp *e : arena) delete e;
    arena.clear();
  }

private:
  Exp *New(ExpType t) {
    Exp *exp = new Exp;
    exp->type = t;
    arena.push_back(exp);
    return exp;
  }
  std::vector<Exp *> arena;
};

static std::string ExpString(const Exp *e) {
  switch (e->type) {
  case ExpType::STRING:
    // XXX escaping
    return StringPrintf("\"%s\"", e->str.c_str());
  case ExpType::VAR:
    return e->str;
  case ExpType::INTEGER:
    return StringPrintf("%lld", e->integer);
  case ExpType::LIST: {
    std::string ret = "[";
    for (int i = 0; i < (int)e->children.size(); i++) {
      if (i != 0) StringAppendF(&ret, ", ");
      ret += ExpString(e->children[i]);
    }
    ret += "]";
    return ret;
  }
  default:
    return "ILLEGAL EXPRESSION";
  }
}
}  // namespace

/*
constexpr int to_int(std::string_view sv) {
  int sum = 0;
  for (auto c : sv) { sum *= 10; sum += c - '0'; }
  return sum;
}
*/

static void TestReadme() {
  using namespace ctpg;
  using namespace ctpg::buffers;

  static constexpr nterm<int> list("list");
  static constexpr char number_pattern[] = "[1-9][0-9]*";
  static constexpr regex_term<number_pattern> number("number");

  static constexpr auto to_int = [](std::string_view sv) {
      int sum = 0;
      for (auto c : sv) { sum *= 10; sum += c - '0'; }
      return sum;
    };

  static constexpr parser p(
      list,
      terms(',', number),
      nterms(list),
      rules(
          list(number)
              >= to_int,
          list(list, ',', number)
              >= [](int sum, char, const auto& n){ return sum + to_int(n); }
      )
  );

  // Parse at compile time!
  constexpr char example_text[] = "1, 2, 3";
  constexpr auto cres = p.parse(cstring_buffer(example_text));
  CHECK(cres == 6);

  auto Parse = [&](std::string s) {
      auto res = p.parse(string_buffer(std::move(s)), std::cerr);
      CHECK(res.has_value());
      return res.value();
    };

  CHECK(Parse("1") == 1);
  CHECK(Parse("10") == 10);
  CHECK(Parse("70, 700, 7") == 777);
}

int main(int argc, char **argv) {
  TestReadme();

  printf("OK\n");
  return 0;
}
