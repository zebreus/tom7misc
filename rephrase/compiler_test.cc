
#include "compiler.h"

#include "ansi.h"
#include "base/logging.h"

static void TestCompiler() {
  Compiler compiler;
  bc::Program prog = compiler.CompileString("test", "7");
  bc::PrintProgram(prog);
}


int main(int argc, char **argv) {
  ANSI::Init();

  TestCompiler();

  printf("OK\n");
  return 0;
}
