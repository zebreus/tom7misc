
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
  Inst load = inst::Load{.out = "x", .global = "msg"};
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

static void TestEndToEndEasy() {
  using TE = TestExecution;

  Compiler compiler;
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

static std::string RunToString(const std::string &source) {
  using TE = TestExecution;
  Compiler compiler;
  compiler.frontend.SetVerbose(2);
  Program prog = compiler.CompileString("test", source);
  bc::PrintProgram(prog);
  TE execution(prog);
  TE::State state = execution.Start();
  execution.RunToCompletion(&state);
  CHECK(!execution.Failed());
  return std::move(execution.console_output);
}

static void ExecTests() {
  CHECK(RunToString("7") == "") << "No output.";
  CHECK(RunToString("print \"hi\"") == "hi");

  CHECK(RunToString("print (string-concat (\"succ\", \"ess\"))") ==
        "success");

  CHECK(RunToString(R"(
    let
      fun f 0 = print "done."
        | f n =
          let do print "@"
          in f (n - 1)
          end
    in
      f 7
    end
   )") == "@@@@@@@done.");

  CHECK(RunToString(R"(
    let
      fun fact 0 = 1
        | fact n = n * fact (n - 1)
    in
      print (int-to-string (fact 5))
    end
   )") == "120");

  CHECK(RunToString(R"(
  let
    fun o(f, g) = fn x => f(g(x))

    datatype (a) list = :: of a * list | nil

    datatype order = LESS | EQUAL | GREATER

    fun int-compare (a, b) =
      if a < b
      then LESS
      else if a == b
           then EQUAL
           else GREATER

    fun list-sort cmp l =
      let
        fun split l =
          let fun s (a1, a2, nil) = (a1, a2)
                | s (a1, a2, (h::t)) = s (a2, h::a1, t)
          in s (nil, nil, l)
          end

        fun merge (a, nil) = a
          | merge (nil, b) = b
          | merge ((a :: ta) as aa, (b :: tb) as bb) =
          case cmp (a, b) of
            EQUAL => (a :: b :: merge (ta, tb))
          | LESS => (a :: merge (ta, bb))
          | GREATER => (b :: merge (aa, tb))

        fun ms nil = nil
          | ms ((s :: nil) as l) = l
          | ms (a :: b :: nil) = merge (a :: nil, b :: nil)
          | ms ll =
          let val (a,b) = split ll
          in merge (ms a, ms b)
          end
      in
        ms l
      end

    fun list-app f nil = ()
      | list-app f (h :: t) =
      let do f h
      in list-app f t
      end

    val list = 3 :: 9 :: 1 :: 2 :: 4 :: 8 :: 6 :: 7 :: 5 :: nil
    val sorted = list-sort int-compare list
  in
    list-app (print o int-to-string) sorted
  end
  )") == "123456789");

  /*
      CHECK(RunToString(R"(
  let
    datatype exp = Let of dec * exp
                 | Int of int
                 | Var of string
    and dec = Val of string * exp

    fun ^(a, b) = string-concat (a, b)

    fun etos (Let (d, e) : exp) =
      "let " ^ dtos d ^ " in " ^ etos e ^ " end"
      | etos (Int i) = int-to-string i
      | etos (Var v) = v
    and dtos (Val (x, e) : dec) = "val " ^ x ^ " = " ^ etos e

    val expr = Let (Val ("x", Int 7), Var "x")

  in
    print (etos expr)
  end
)") == "let x = 7 in x end");
  */
}

static void NewTests() {
  // Mutually recursive datatypes and functions.
  CHECK(RunToString(R"(
  let
    datatype exp = Let of dec * exp
    and dec = Val of string * exp

    fun ^(a, b) = string-concat (a, b)

    fun etos (Let (d, e) : exp) = string-concat("hi", etos e)
    and  dtos (Val (x, e) : dec) = "val " ^ x ^ " = " ^ etos e

  in
    (* print (etos expr) *)
    etos
  end
)") == "let x = 7 in x end");

  // This also gives an error, perhaps related.
/*
  CHECK(RunToString(R"(
  let
    datatype exp = Let of dec * exp
    and dec = Val of string * exp

    fun ^(a, b) = string-concat (a, b)

    fun etos (Let (d, e) : exp) = "hi" ^ etos e
    and  dtos (Val (x, e) : dec) = "val " (* ^ x ^ " = " ^ etos e *)

    (* val expr = Let (Val ("x", Int 7), Var "x") *)

  in
    (* print (etos expr) *)
    etos
  end
)") == "let x = 7 in x end");
*/

}

}  // namespace bc

int main(int argc, char **argv) {
  ANSI::Init();

  /*
  bc::TestTrivial();
  bc::TestEndToEndEasy();
  bc::ExecTests();
  */
  bc::NewTests();

  printf("OK\n");
  return 0;
}
