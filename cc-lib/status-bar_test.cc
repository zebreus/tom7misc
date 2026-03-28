
#include "status-bar.h"

#include "ansi.h"
#include "base/print.h"

static void Test() {
  StatusBar bar(3);

  bar.EmitStatus(
      "You should not see " ARED("ANY") " of these lines persist!\n"
      "Not " ARED("this one") "...\n"
      "Nor " ARED("this one either") "!!\n");

  bar.EmitStatus({
      AORANGE("...............this should be overwritten..........."
              "......................."),
      ARED("this line should also be overwritten!"),
      ABGCOLOR(255, 0, 0, "this line should be gone")
    });

  bar.Printf(AWHITE("This line should say forty-two: %d and be before "
                    "the status.") "\n",
             42);

  bar.Print(AYELLOW("This one should be yellow and say ") AGREEN("{}")
            AYELLOW("."), "green");

  bar.Statusf(
      "| This is the three-line " ABLUE("status bar") ".\n"
      ARED("# this one should get overwritten!!!") "\n"
      "| This is the end of the status bar. " ACYAN("♥") "\n");


  bar.LineStatusf(
      1,
      // test that it strips the trailing newline
      "| It should appear below two %s lines about 42 and green.\n",
      "status");
}

static void TestIndexed() {
  StatusBar bar(10);

  bar.LineStatusf(7, "Testing that I can index into a line without "
                  "emitting first.");
}

static void TestThatItGoesAway() {
  StatusBar bar(3);

  Print("The next line should say " AWHITE("xzzy") ":\n");

  bar.EmitStatus(
      ARED("WRONG") ": These bars should go away when we Remove().\n"
      ARED("NO") "................................................\n"
      ARED("ERROR") "!!!!!!!\n");

  bar.Remove();

  Print("I am saying " AWHITE("xyzzy") " to confirm success.\n");
}

int main(int argc, char **argv) {
  ANSI::Init();

  TestThatItGoesAway();

  Print(AGREY("---- blank lines expected in here ----") "\n");
  TestIndexed();
  Print(AGREY("--------------------------------------") "\n");

  Test();

  Print("^ This test requires visual inspection\n");
  Print("OK\n");
  return 0;
}
