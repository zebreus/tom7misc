
#include "execution.h"

#include "ansi.h"
#include "base/logging.h"

static void TestTrivial() {

  bc::Program program;
  bc::Inst load = bc::inst::Load{.out = "x", .data_label = "msg"};
  bc::Inst fail = bc::inst::Fail{.arg = "x"};

  program.data = {
    {"msg", {.v = bc::Value::t(std::string("Test"))}}
  };
  program.code = {
    {"main", {load, fail}}
  };

  bc::Execution execution(program);

  // TODO: Start, step, etc.
}

int main(int argc, char **argv) {
  ANSI::Init();

  TestTrivial();

  printf("OK\n");
  return 0;
}
