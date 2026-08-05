
#include "z3.h"

#include <string_view>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"

static void TestParseSExp() {
  {
    auto result = Z3::ParseSExp("hello");
    CHECK(result.has_value());
    CHECK(result->type == Z3::SExp::Type::ATOM);
    CHECK(result->atom == "hello");
  }

  {
    auto result = Z3::ParseSExp("()");
    CHECK(result.has_value());
    CHECK(result->type == Z3::SExp::Type::LIST);
    CHECK(result->list.empty());
  }

  {
    auto result = Z3::ParseSExp("(hello world)");
    CHECK(result.has_value());
    CHECK(result->type == Z3::SExp::Type::LIST);
    CHECK(result->list.size() == 2);
    CHECK(result->list[0].type == Z3::SExp::Type::ATOM);
    CHECK(result->list[0].atom == "hello");
    CHECK(result->list[1].type == Z3::SExp::Type::ATOM);
    CHECK(result->list[1].atom == "world");
  }

  {
    auto result = Z3::ParseSExp("(a (b c))");
    CHECK(result.has_value());
    CHECK(result->type == Z3::SExp::Type::LIST);
    CHECK(result->list.size() == 2);
    CHECK(result->list[0].type == Z3::SExp::Type::ATOM);
    CHECK(result->list[0].atom == "a");
    CHECK(result->list[1].type == Z3::SExp::Type::LIST);
    CHECK(result->list[1].list.size() == 2);
    CHECK(result->list[1].list[0].type == Z3::SExp::Type::ATOM);
    CHECK(result->list[1].list[0].atom == "b");
    CHECK(result->list[1].list[1].type == Z3::SExp::Type::ATOM);
    CHECK(result->list[1].list[1].atom == "c");
  }

  {
    auto result = Z3::ParseSExp("(! x :named y :flag)");
    CHECK(result.has_value());
    CHECK(result->type == Z3::SExp::Type::LIST);
    CHECK(result->list.size() == 2);
    CHECK(result->list[0].atom == "!");
    CHECK(result->list[1].atom == "x");
    CHECK(result->attrs.size() == 2);
    CHECK(result->attrs[0].first == ":named");
    CHECK(result->attrs[0].second.get() != nullptr);
    CHECK(result->attrs[0].second->type == Z3::SExp::Type::ATOM);
    CHECK(result->attrs[0].second->atom == "y");
    CHECK(result->attrs[1].first == ":flag");
    CHECK(result->attrs[1].second.get() == nullptr);

    CHECK(Z3::ToString(*result) == "(! x :named y :flag)");
  }
}

static void TestToString() {
  {
    auto result = Z3::ParseSExp("(a (b c))");
    CHECK(result.has_value());
    CHECK(Z3::ToString(*result) == "(a (b c))");
  }
  {
    auto result = Z3::ParseSExp("(:just-flag)");
    CHECK(result.has_value());
    CHECK(Z3::ToString(*result) == "(:just-flag)");
  }
  {
    auto result = Z3::ParseSExp("(:key value)");
    CHECK(result.has_value());
    CHECK(Z3::ToString(*result) == "(:key value)");
  }
  {
    auto result = Z3::ParseSExp("(a :key (nested value) b :flag)");
    CHECK(result.has_value());
    // Note that attributes are moved to the end by ToString because they
    // are stored in attrs field. So the round trip might reorder them.
    CHECK(Z3::ToString(*result) == "(a b :key (nested value) :flag)");
  }
}

static void TestConsumeSExps() {
  {
    std::string_view content = "  (hello) yes  (and hi) ";
    auto results = Z3::ConsumeSExps(&content);
    CHECK(results.size() == 3);
    CHECK(content.empty());

    CHECK(results[0].type == Z3::SExp::Type::LIST);
    CHECK(results[0].list.size() == 1);
    CHECK(results[0].list[0].atom == "hello");

    CHECK(results[1].type == Z3::SExp::Type::ATOM);
    CHECK(results[1].atom == "yes");

    CHECK(results[2].type == Z3::SExp::Type::LIST);
    CHECK(results[2].list.size() == 2);
    CHECK(results[2].list[0].atom == "and");
    CHECK(results[2].list[1].atom == "hi");
  }

  {
    std::string_view content = "()";
    auto results = Z3::ConsumeSExps(&content);
    CHECK(results.size() == 1);
    CHECK(content.empty());
    CHECK(results[0].type == Z3::SExp::Type::LIST);
    CHECK(results[0].list.empty());
  }

  {
    std::string_view content = "(a b) (c d";
    auto results = Z3::ConsumeSExps(&content);
    CHECK(results.size() == 1);
    CHECK(content == "(c d");

    CHECK(results[0].type == Z3::SExp::Type::LIST);
    CHECK(results[0].list.size() == 2);
    CHECK(results[0].list[0].atom == "a");
    CHECK(results[0].list[1].atom == "b");
  }

  {
    std::string_view content = "atom ) invalid";
    auto results = Z3::ConsumeSExps(&content);
    CHECK(results.size() == 1);
    CHECK(content == ") invalid");

    CHECK(results[0].type == Z3::SExp::Type::ATOM);
    CHECK(results[0].atom == "atom");
  }
}

int main() {
  ANSI::Init();
  TestParseSExp();
  TestToString();
  TestConsumeSExps();
  Print("OK\n");
  return 0;
}

