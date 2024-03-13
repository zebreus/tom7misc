
#include "execution.h"

#include <string>
#include <optional>

#include "compiler.h"

#include "ansi.h"
#include "base/logging.h"

namespace bc {

static constexpr int COMPILER_VERBOSE = 0;
static constexpr int FRONTEND_VERBOSE = 0;
static constexpr bool BYTECODE_VERBOSE = 1;

#undef CHECK_EQ
#define CHECK_EQ(s1, s2) do { \
  const auto ss1 = s1;        \
  const auto ss2 = s2;        \
  CHECK(ss1 == ss2) << "s1:\n" << ss1 << "\nvs s2:\n" << ss2 << "\n"; \
} while (0)

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
  if (BYTECODE_VERBOSE) {
    PrintProgram(prog);
  }

  TE execution(prog);
  TE::State state = execution.Start();
  execution.RunToCompletion(&state);
  CHECK(!execution.Failed()) << "end to end easy";
}

static std::string RunToString(const std::string &source) {
  using TE = TestExecution;
  Compiler compiler;
  compiler.SetVerbose(COMPILER_VERBOSE);
  compiler.frontend.SetVerbose(FRONTEND_VERBOSE);
  Program prog = compiler.CompileString("test", source);
  if (BYTECODE_VERBOSE) {
    PrintProgram(prog);
  }

  TE execution(prog);
  TE::State state = execution.Start();
  execution.RunToCompletion(&state);
  if (execution.Failed()) {
    printf(ARED("FAILED") "\n");
    if (execution.fail_message.has_value()) {
      printf(AWHITE("message") ": %s\n", execution.fail_message.value().c_str());
    } else {
      printf(AWHITE("(no message?)") "\n");
    }
    printf(AWHITE("output") ":\n" "%s\n",
           execution.console_output.c_str());
    LOG(FATAL) << "Unexpectedly failed";
  }
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

  CHECK_EQ(RunToString(R"(
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
    )"), "let val x = 7 in x end");

  CHECK_EQ(RunToString(R"(
      let
         val r = ref 5
         do r := !r + 1
         do r := !r + 1
      in
         print (int-to-string (!r))
      end
      )"), "7");

  CHECK_EQ(RunToString(R"(
      let
        datatype (a) list = :: of a * list | nil
        fun list-app f nil = ()
          | list-app f (h :: t) =
          let do f h
          in list-app f t
          end
        fun pint n = print (int-to-string n)
      in
        list-app pint (1 :: 5 :: 2 :: 3 :: 2 :: nil)
      end
      )"), "15232");
}

static void ObjTests() {
  CHECK_EQ(RunToString(R"(
     let object Article of { title : string, year : int }
         val n =
           case {(Article) title = "hi"} of
                {(Article) year = 1997 } => 555
              | {(Article) year = 2024 } => 666
              | {(Article) title } => 7
              | {(Article) year = 42 } => 888
     in
       print (int-to-string n)
     end
     )"), "7");

  CHECK_EQ(RunToString(R"(
     let object Article of { title : string, year : int }
         val x = {(Article) title = "hi"} with title = "bye"
         val n =
           case x of
                {(Article) year = 1997 } => 555
              | {(Article) year = 2024 } => 666
              | {(Article) title = "bye" } => 7
              | {(Article) title = "hi" } => fail "Wrong!"
              | {(Article) year = 42 } => 888
     in
       print (int-to-string n)
     end
    )"), "7");

  CHECK_EQ(RunToString(R"(
     let object Article of { title : string, year : int }
         val x = ({(Article) title = "hi"} with year = 2024)
                                          without (Article) title
         val n =
           case x of
                {(Article) year = 1997 } => 555
              | {(Article) title } => 666
              | {(Article) year = 2024 } => 7
              | {(Article) year } => 999
              | {(Article) year = 42 } => 888
     in
       print (int-to-string n)
     end
    )"), "7");

  std::string layout_lib_preamble = R"##(
       val NODE_BOX = "box"
       val NODE_SPAN = "span"
       val NODE_STICKER = "stick"
       object Node of { display : string }

       object Span of { line-height: float,
                        (* base font. *)
                        font-face: string,
                        font-size: float,
                        (* Selects a font variant *)
                        font-bold: bool,
                        font-italic: bool }
       fun span (attrs : obj) (l : layout) =
         node (attrs with (Node) display = NODE_SPAN) l

       fun b layout =
         span {(Span) font-bold = true} layout
  )##";

  CHECK_EQ(RunToString(
     "let\n" +
     layout_lib_preamble + R"##(

       val author = b[Tom 7 [[Murphy]] ]
       fun layout-tostring l =
         if is-text l
         then get-text l
         else
           let
               val z = layout-vec-size l
               fun r n = if n == z then "." ^ int-to-string n
                         else layout-tostring (layout-vec-sub (l, n)) ^
                              r (n + 1)
           in r 0
           end
      in
         print (layout-tostring author)
      end
      )##"),
           // This depends on how nodes are collapsed. We should
           // probably be concatenating the text nodes, giving
           // "Tom 7 Murphy .1".
           "Tom 7 Murphy .3");

  CHECK_EQ(RunToString(
     "let\n" +
     layout_lib_preamble + R"##(

       val author = b[Tom 7 [[Murphy]] ]
       fun print-layout l =
         if is-text l
         then print (get-text l)
         else
           let
               val z = layout-vec-size l
               fun r n = if n == z then print ("." ^ int-to-string n)
                         else
                           let do print-layout (layout-vec-sub (l, n))
                           in r (n + 1)
                           end
           in r 0
           end
      in
         print-layout author
      end
      )##"), "Tom 7 Murphy .3");

  CHECK_EQ(RunToString(
     "let\n" +
     layout_lib_preamble + R"##(
       datatype (a) list = :: of a * list | nil
       fun list-app f nil = ()
         | list-app f (h :: t) =
         let do f h
         in list-app f t
         end

       datatype layoutcase =
           Text of string
         | Node of obj * layout list

       fun layoutcase (l : layout) =
         if is-text l
         then Text (get-text l)
         else
           let
             val z = layout-vec-size l
             fun r n =
               let in
                 if n == z then nil
                 else (layout-vec-sub (l, n) :: r (n + 1))
               end
           in
             Node (get-attrs l, r 0)
           end

       val author = b[Tom 7 [[Murphy]] ]
       fun print-layout l =
         case layoutcase l of
           Text s => print s
         | Node (attrs, children) =>
           let
               do print "<node>"
               do list-app print-layout children
           in
               print "</node>"
           end
      in
        print-layout author
      end
      )##"), "<node>Tom 7 Murphy </node>");

  CHECK_EQ(RunToString(
               "let object Nothing of { }\n"
               "in\n"
               "if obj-empty {(Nothing) }\n"
               "then print \"yeah\"\n"
               "else print \"no\"\n"
               "end\n"), "yeah");
}

static void NewTests() {

}

}  // namespace bc

int main(int argc, char **argv) {
  ANSI::Init();

  bc::TestTrivial();
  bc::TestEndToEndEasy();
  bc::ExecTests();
  bc::ObjTests();
  bc::NewTests();

  printf("OK\n");
  return 0;
}
