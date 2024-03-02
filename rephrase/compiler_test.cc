
#include "compiler.h"

#include "ansi.h"
#include "base/logging.h"

static void TrivialTest() {
  Compiler compiler;
  // There's pretty much only one way to compile this program.
  bc::Program prog = compiler.CompileString("test", "7");
  // bc::PrintProgram(prog);
  CHECK(!prog.data.empty());
  CHECK(prog.data.size() == 1);
  const auto &[data_lab, data_value] = *prog.data.begin();
  CHECK(prog.code.contains("main"));
  const auto &[arg, insts] = prog.code.find("main")->second;
  CHECK(insts.size() == 2);
  const bc::inst::Load *load = std::get_if<bc::inst::Load>(&insts[0]);
  CHECK(load != nullptr);
  CHECK(load->data_label == data_lab);
  const bc::inst::Ret *ret = std::get_if<bc::inst::Ret>(&insts[1]);
  CHECK(ret->arg == load->out);
}

static void SimpleTest() {
  Compiler compiler;
  bc::Program prog = compiler.CompileString(
      "test",
      R"(
7 + 8
)");

  bc::PrintProgram(prog);
}

int main(int argc, char **argv) {
  ANSI::Init();

  TrivialTest();
  SimpleTest();

  printf("OK\n");
  return 0;
}
