
#include "compiler.h"

#include <cstdio>
#include <variant>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "bc.h"

static constexpr bool VERBOSE = false;

static void TrivialTest() {
  Compiler compiler;
  // There's pretty much only one way to compile this program,
  // although now that we have optimizations on basic blocks
  // and stuff like NOTE, it may be hopeless to insist on a
  // specific program.
  bc::Program prog = compiler.CompileString("test", "7");
  if (VERBOSE) {
    bc::PrintProgram(prog);
  }
  CHECK(!prog.data.empty());
  CHECK(prog.data.size() == 1);
  const auto &[data_lab, data_value] = *prog.data.begin();
  CHECK(prog.code.contains("main"));
  const auto &[arg, insts] = prog.code.find("main")->second;

  // There could be NOTEs, useless forward JUMPs, etc...
  CHECK(insts.size() >= 2);

  int pos = (int)insts.size() - 2;
  for (int i = 0; i < pos; i++) {
    CHECK(std::holds_alternative<bc::inst::Note>(insts[i]) ||
          std::holds_alternative<bc::inst::Jump>(insts[i]));
  }
  const bc::inst::Load *load = std::get_if<bc::inst::Load>(&insts[pos]);
  CHECK(load != nullptr);
  CHECK(load->global == data_lab);
  const bc::inst::Ret *ret = std::get_if<bc::inst::Ret>(&insts[pos + 1]);
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

static void Regression5668() {
  Compiler compiler;
  if (VERBOSE) {
    compiler.SetVerbose(2);
  }
  bc::Program prog = compiler.CompileString(
      "test",
      R"(
      let

      datatype (a) option = SOME of a | NONE

      fun option-app f (SOME h) = f h

      fun main-text (lay : int) =
        let
          fun insert-paragraph (_ : int) : unit =
            option-app (fn _ => ()) NONE
        in
          option-app insert-paragraph NONE
        end

      in
        (main-text 0)
      end
      )");
  if (VERBOSE) {
    bc::PrintProgram(prog);
  }
}

// This used to cause an internal type error (attempt
// to unroll a type variable, which was a free "gen"
// variable from a polymorphic value that got substituted),
// due to a bug in substitution.
static void Regression5743() {
  Compiler compiler;
  if (VERBOSE) {
    compiler.SetVerbose(2);
  }
  bc::Program prog = compiler.CompileString(
      "test", R"(
          let
            datatype (a) option = NONE

            fun option-get de NONE = de

            datatype justification = FULLY | LEFT

            fun pack-boxes-horiz (just : justification) : int =
              (case just of
                 FULLY => 999)

            fun consume f =
              case (7, "yo") of
                (7, children) =>
                  (case f 0 of
                     NONE => NONE
                   | outer => outer)

            val just = consume (fn _ => NONE)
            val justy = option-get FULLY just
            val _ = pack-boxes-horiz justy

          in
            777
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
  Regression5668();
  Regression5743();

  Print("OK\n");
  return 0;
}
