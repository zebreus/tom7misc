
// Portions are derived from CTPG examples; MIT license. See ctpg.h.

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

static void TestSimpleParse() {
  // Application-specific parsing context. Here we have
  // an arena for allocating the AST.
  using ctx = ExpPool;

  using namespace ctpg;
  using namespace ctpg::buffers;

  static constexpr nterm<const Exp *> exp("exp");
  static constexpr nterm<std::vector<const Exp *>>
    comma_separated_exp("comma_separated_exp");

  static constexpr char digits_pattern[] = "[1-9][0-9]*";
  static constexpr regex_term<digits_pattern> digits("digits");

  static constexpr char id_pattern[] = "[A-Za-z_][A-Za-z0-9_]*";
  static constexpr regex_term<id_pattern> id("id");

  static constexpr parser p(
      // This is the grammar root. We're parsing an expression.
      exp,
      // All the terminal symbols. Characters and strings implicitly
      // stand for themselves.
      terms(',', '[', ']', digits, id),
      nterms(exp, comma_separated_exp),

      rules(
          comma_separated_exp(exp) >= [](const Exp *e) {
              return std::vector<const Exp *>({e});
            },
          comma_separated_exp(comma_separated_exp, ',', exp)
          >= [](std::vector<const Exp *> &&v, auto, const Exp *e) {
              v.push_back(e);
              return std::move(v);
            },

          exp(digits) >>= [](auto &ctx, std::string_view d) {
              return ctx.Int(std::stoll(std::string(d)));
            },
          exp(id) >>= [](auto &ctx, std::string_view d) {
              return ctx.Var(std::string(d));
            },
          exp('[', comma_separated_exp, ']')
          >>= [](auto &ctx, auto _l, std::vector<const Exp *> &&v, auto _r) {
              return ctx.List(std::move(v));
            }
      )
  );

  ExpPool pool;
  auto Parse = [&](std::string s) {
      printf("Parsing %s...\n", s.c_str());
      auto res = p.context_parse(pool,
                                 parse_options{}.set_skip_whitespace(false),
                                 string_buffer(std::move(s)),
                                 std::cerr);
      CHECK(res.has_value());
      printf("  Got: %s\n", ExpString(res.value()).c_str());
      return res.value();
    };

  {
    const Exp *e = Parse("15232");
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::INTEGER);
    CHECK(e->integer == 15232);
  }

  {
    const Exp *e = Parse("var");
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::VAR);
    CHECK(e->str == "var");
  }

  {
    const Exp *e = Parse("[var,123,456]");
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::LIST);
    CHECK(e->children.size() == 3);
    CHECK(e->children[0]->str == "var");
    CHECK(e->children[1]->integer == 123);
    CHECK(e->children[2]->integer == 456);
  }

  {
    const Exp *e = Parse("[xar,[333,777],888]");
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::LIST);
    CHECK(e->children.size() == 3);
    CHECK(e->children[0]->str == "xar");
    CHECK(e->children[1]->type == ExpType::LIST);
    CHECK(e->children[1]->children.size() == 2);
    CHECK(e->children[1]->children[0]->integer == 333);
    CHECK(e->children[1]->children[1]->integer == 777);
    CHECK(e->children[2]->integer == 888);
  }

}


static void TestReadme() {
  using namespace ctpg;
  using namespace ctpg::buffers;

  // TODO: Why does this parser skip whitespace?
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

  // Beware: Skipping whitespace is the default!
  constexpr parse_options options = parse_options{}.set_skip_whitespace(true);

  // Parse at compile time!
  constexpr char example_text[] = "1, 2, 3";
  // XXX how to pass options to constexpr version?
  constexpr auto cres = p.parse(cstring_buffer(example_text));
  CHECK(cres == 6);

  // Beware: This is

  auto Parse = [&](std::string s) {
      printf("Parsing %s:\n", s.c_str());
      auto res = p.parse(options, string_buffer(std::move(s)), std::cerr);
      CHECK(res.has_value());
      printf("  Got %d.\n", res.value());
      return res.value();
    };

  CHECK(Parse("1") == 1);
  CHECK(Parse("10") == 10);
  CHECK(Parse("70, 700, 7") == 777);
}

int main(int argc, char **argv) {
  TestReadme();
  TestSimpleParse();

  printf("OK\n");
  return 0;
}
