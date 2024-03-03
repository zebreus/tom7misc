
#include "execution.h"

#include <string>
#include <optional>

#include "compiler.h"

#include "ansi.h"
#include "base/logging.h"

namespace bc {

struct TestExecution : public Execution {
  using Execution::Execution;

  void FailHook(const std::string &msg) override {
    CHECK(!Failed());
    fail_message = {msg};
  }

  void ConsoleHook(const std::string &out) override {
    console_output += out;
  }

  bool Failed() const { return fail_message.has_value(); }

  std::optional<std::string> fail_message;
  std::string console_output;
};

static void TestTrivial() {
  using TE = TestExecution;

  Program program;
  Inst load = inst::Load{.out = "x", .data_label = "msg"};
  Inst fail = inst::Fail{.arg = "x"};

  program.data = {
    {"msg", {.v = Value::t(std::string("Test"))}}
  };
  program.code = {
    {"main", std::make_pair("unused", std::vector<Inst>{load, fail})}
  };

  TE execution(program);

  TE::State state = execution.Start();
  {
    const TE::StackFrame &frame = state.stack.back();
    CHECK(frame.ip == 0) << "Starts at the first instruction.";
    CHECK(!frame.locals.contains("x"));
    CHECK(program.code.contains("main"));
    const auto &m = program.code["main"];
    CHECK(frame.insts == &m.second);
  }

  CHECK(!execution.Failed());
  execution.Step(&state);
  CHECK(!state.stack.empty());
  {
    const TE::StackFrame &frame = state.stack.back();
    CHECK(frame.ip == 1) << "Executed one instruction.";
    CHECK(frame.locals.contains("x"));
  }

  execution.Step(&state);
  CHECK(execution.Failed()) << "Should have executed the fail "
    "instruction.";

  CHECK(execution.fail_message.value() == "Test");
}

static void TestEndToEnd() {
  using TE = TestExecution;

  Compiler compiler;
  // There's pretty much only one way to compile this program.
  Program prog = compiler.CompileString(
      "test",
      R"(
         let
           fun dont-fail 0 = fail "wrong"
             | dont-fail 1 = 777
             | dont-fail _ = fail "also wrong"

           do dont-fail 1
         in

           dont-fail 1
         end
        )");
  PrintProgram(prog);

  TE execution(prog);
  TE::State state = execution.Start();
  execution.RunToCompletion(&state);
  CHECK(!execution.Failed());
}

}

int main(int argc, char **argv) {
  ANSI::Init();

  bc::TestTrivial();
  bc::TestEndToEnd();

  printf("OK\n");
  return 0;
}
