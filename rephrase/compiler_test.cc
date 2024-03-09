
#include "compiler.h"

#include "ansi.h"
#include "base/logging.h"

static constexpr bool VERBOSE = false;

static void TrivialTest() {
  Compiler compiler;
  // There's pretty much only one way to compile this program.
  bc::Program prog = compiler.CompileString("test", "7");
  if (VERBOSE) {
    bc::PrintProgram(prog);
  }
  CHECK(!prog.data.empty());
  CHECK(prog.data.size() == 1);
  const auto &[data_lab, data_value] = *prog.data.begin();
  CHECK(prog.code.contains("main"));
  const auto &[arg, insts] = prog.code.find("main")->second;
  CHECK(insts.size() == 3);
  CHECK(std::holds_alternative<bc::inst::Note>(insts[0]));
  const bc::inst::Load *load = std::get_if<bc::inst::Load>(&insts[1]);
  CHECK(load != nullptr);
  CHECK(load->global == data_lab);
  const bc::inst::Ret *ret = std::get_if<bc::inst::Ret>(&insts[2]);
  CHECK(ret->arg == load->out);
}

static void SimpleTest() {
  Compiler compiler;
  bc::Program prog = compiler.CompileString(
      "test",
      R"(
let
  fun fact 0 = 1
    | fact n = n * fact (n - 1)
in
  fact 6
end
)");

  if (VERBOSE) {
    bc::PrintProgram(prog);
  }
}

// Former bug in closure conversion (internal type error)
// at r5580 due to double-translating types in sumcases.
static void Regression5580() {
  Compiler compiler;
  bc::Program prog = compiler.CompileString(
      "test",
      R"(
        let
          datatype set = Set of int * int -> string
          fun set-empty cmp = fail "NO"
          fun set-insert (Set cmp) = cmp (1, 2)

          fun bibsource-compare _ = 999
          val sss = ref (set-empty bibsource-compare)
        in
          set-insert (!sss)
        end)");

  if (VERBOSE) {
    bc::PrintProgram(prog);
  }
}


int main(int argc, char **argv) {
  ANSI::Init();

  TrivialTest();
  SimpleTest();
  Regression5580();

  printf("OK\n");
  return 0;
}
