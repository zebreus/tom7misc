
// I think this is a throwaway test of getting status output
// with ANSI codes working in emacs.

#include <cstdio>
#include <format>
#include <string_view>
#include <chrono>
#include <thread>

#include "ansi.h"
#include "markdown.h"
#include "base/print.h"
#include "util.h"
#include "status-bar.h"

int main(int argc, char **argv) {
  ANSI::Init();

  StatusBar status(3);
  for (int i = 0; i < 10; i++) {
    status.EmitStatus({
        "A " ARED("red") " line up here.",
        std::format("This is iteration #{}", i),
        ABGCOLOR(0, 0, 40, "Bottom line."),
      });
    fflush(stdout);
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(200ms);

    if (i == 5) {
      status.Print("Up here " AGREY ("too") ".\n");
    }
  }

  status.Remove();


  Print(AGREEN ("OK") "\n");


  Print("<OUTPUT>");
  Print("Here's the output: 1234567");
  Print("</OUTPUT>\n");

  return 0;
}
