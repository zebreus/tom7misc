#include "aiger.h"

#include <string>
#include <string_view>
#include <optional>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "interesting-props.h"
#include "prop.h"

static void TestParseEmpty() {
  std::string_view empty_aig = "aag 0 0 0 0 0\n";
  std::optional<Prop> prop = FromAIGER(empty_aig);
  CHECK(prop.has_value()) << "Failed to parse empty circuit";
}

static void TestParseConstants() {
  {
    std::string_view false_aig =
        "aag 0 0 0 1 0\n"
        "0\n";
    std::optional<Prop> prop = FromAIGER(false_aig);
    CHECK(prop.has_value()) << "Failed to parse constant FALSE";
  }

  {
    std::string_view true_aig =
        "aag 0 0 0 1 0\n"
        "1\n";
    std::optional<Prop> prop = FromAIGER(true_aig);
    CHECK(prop.has_value()) << "Failed to parse constant TRUE";
  }
}

static void TestParseGates() {
  {
    std::string_view buf_aig =
        "aag 1 1 0 1 0\n"
        "2\n"
        "2\n";
    std::optional<Prop> prop = FromAIGER(buf_aig);
    CHECK(prop.has_value()) << "Failed to parse buffer";
  }

  {
    std::string_view inv_aig =
        "aag 1 1 0 1 0\n"
        "2\n"
        "3\n";
    std::optional<Prop> prop = FromAIGER(inv_aig);
    CHECK(prop.has_value()) << "Failed to parse inverter";
  }

  {
    std::string_view and_aig =
        "aag 3 2 0 1 1\n"
        "2\n"
        "4\n"
        "6\n"
        "6 2 4\n";
    std::optional<Prop> prop = FromAIGER(and_aig);
    CHECK(prop.has_value()) << "Failed to parse AND gate";
  }
}

static void TestRoundTrip() {
  std::string_view half_adder =
      "aag 7 2 0 1 3\n"
      "2\n"
      "4\n"
      "6\n"
      "6 13 15\n"
      "12 2 4\n"
      "14 3 5\n"
      "i0 x\n"
      "i1 y\n"
      "o0 s\n"
      "c\n"
      "half adder\n";
  std::optional<Prop> p = FromAIGER(half_adder);
  CHECK(p.has_value()) << "Failed to parse half adder";

  std::string out = ToAIGER(p.value());

  std::optional<Prop> p2 = FromAIGER(out);
  CHECK(p2.has_value()) << "Failed to parse round-tripped half adder";
}

static void TestInterestingPropsRoundTrip() {
  for (const Prop &p : SmallInterestingProps()) {
    std::string out = ToAIGER(p);
    std::optional<Prop> p2 = FromAIGER(out);
    CHECK(p2.has_value()) << "Failed to parse round-tripped small prop";
    CHECK(PropEq(p, p2.value())) << "Round-tripped small prop not equal";
  }

  for (const Prop &p : MediumInterestingProps()) {
    std::string out = ToAIGER(p);
    std::optional<Prop> p2 = FromAIGER(out);
    CHECK(p2.has_value()) << "Failed to parse round-tripped medium prop";
    CHECK(PropEq(p, p2.value())) << "Round-tripped medium prop not equal";
  }
}

static void TestInvalidInput() {
  {
    std::optional<Prop> empty = FromAIGER("");
    CHECK(!empty.has_value()) << "Empty string should fail";
  }

  {
    std::optional<Prop> bad_header = FromAIGER("aag 0 0 0\n");
    CHECK(!bad_header.has_value()) << "Bad header should fail";
  }

  {
    std::optional<Prop> missing_output = FromAIGER(
        "aag 1 1 0 1 0\n"
        "2\n");
    CHECK(!missing_output.has_value()) << "Missing output should fail";
  }

  {
    std::optional<Prop> bad_gate = FromAIGER(
        "aag 3 2 0 1 1\n"
        "2\n"
        "4\n"
        "6\n"
        "6 2\n");
    CHECK(!bad_gate.has_value()) << "Bad gate definition should fail";
  }
}

int main(int argc, char **argv) {
  ANSI::Init();

  TestParseEmpty();
  TestParseConstants();
  TestParseGates();
  TestRoundTrip();
  TestInterestingPropsRoundTrip();
  TestInvalidInput();

  Print("OK\n");
  return 0;
}
