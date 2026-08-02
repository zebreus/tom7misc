#include "verilog.h"

#include <optional>
#include <string>

#include "ansi.h"
#include "base/print.h"
#include "base/logging.h"
#include "prop.h"

static void TestParseSimple() {
  std::string content = R"(
module simple(a, b, c, out);
  input a, b, c;
  output out;
  wire w1;
  and2 g0(.a(a), .b(b), .O(w1));
  or2 g1(.a(w1), .b(c), .O(out));
endmodule
  )";
  std::optional<Prop> p = FromVerilog(content);
  CHECK(p.has_value());

  // Variables are assigned IDs in order of first appearance as a gate port.
  // g0: w1 (0), a (1), b (2)
  // g1: out (3), w1 (0), c (4)
  Prop a = Prop{.p = Var{.id = 1}};
  Prop b = Prop{.p = Var{.id = 2}};
  Prop c = Prop{.p = Var{.id = 4}};
  Prop expected = (a & b) | c;

  CHECK(PropEq(*p, expected));
}

static void TestParseConstants() {
  std::string content = R"(
module constants(out);
  output out;
  wire w1;
  not g0(.a(1'b0), .O(w1));
  and2 g1(.a(w1), .b(1'b1), .O(out));
endmodule
  )";
  std::optional<Prop> p = FromVerilog(content);
  CHECK(p.has_value());
  CHECK(PropEq(*p, -False() & True()));
}

static void TestParseFailure() {
  std::string content = "garbage that is not verilog";
  std::optional<Prop> p = FromVerilog(content);
  CHECK(!p.has_value());
}

static void TestParseABC() {
  // Here's an example output of ABC.
  std::string content = R"(
module chess (
    b2_Q, b2_R, b4_n, b4_p, b4_r, b4_b, b4_q, b4_o, b2_P, b3_o,
    out  );
  input  b2_Q, b2_R, b4_n, b4_p, b4_r, b4_b, b4_q, b4_o, b2_P, b3_o;
  output out;
  wire new_n834, new_n835, new_n836, new_n837, new_n838, new_n839, new_n840,
    new_n841, new_n842;
  or2  g0(.a(b2_Q), .b(b2_R), .O(new_n834));
  or2  g1(.a(b4_n), .b(b4_p), .O(new_n835));
  or2  g2(.a(b4_r), .b(b4_b), .O(new_n836));
  or2  g3(.a(new_n836), .b(b4_q), .O(new_n837));
  or2  g4(.a(new_n837), .b(new_n835), .O(new_n838));
  and2 g5(.a(new_n838), .b(new_n834), .O(new_n839));
  or2  g6(.a(new_n839), .b(b4_o), .O(new_n840));
  or2  g7(.a(new_n834), .b(b2_P), .O(new_n841));
  and2 g8(.a(new_n841), .b(b3_o), .O(new_n842));
  and2 g9(.a(new_n842), .b(new_n840), .O(out));
endmodule
  )";

  std::optional<Prop> p = FromVerilog(content);
  CHECK(p.has_value());
  CHECK(PropSize(*p) > 10);
}

int main(int argc, char **argv) {
  ANSI::Init();

  TestParseSimple();
  TestParseConstants();
  TestParseFailure();
  TestParseABC();

  Print("OK\n");
  return 0;
}
