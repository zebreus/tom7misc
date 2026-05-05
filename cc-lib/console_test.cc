
#include "console.h"

#include <cstdio>
#include <deque>
#include <cstdlib>
#include <chrono>
#include <string>
#include <thread>

#include "ansi.h"
#include "base/print.h"
#include "threadutil.h"
#include "hexdump.h"

void Chat() {
  Print("Creating console...");
  fflush(stdout);
  Console console(1, 1000, 1, 1);
  console.Print("This is an interactive test!\n");

  int num_inputs = 0;
  for (int n = 0; true; n++) {
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(500ms);
    console.Print(AGREY("hi") " " ACYAN("{}") "\n", n);

    console.SetStatus(Console::TOP,
                      0,
                      ANSI_BG(0, 0, 80)
                      ANSI_FG(255, 255, 255)
                      "HACKER ALERT!! "
                      ANSI_FG(0, 255, 255)
                      "{}"
                      ANSI_FG(255, 255, 255) " HACKERS.", n);

    console.SetStatus(Console::MID,
                      0,
                      ANSI_BG(30, 30, 30)
                      "─────────────────────────────────────────");

    console.SetStatus(Console::BOT,
                      0,
                      ANSI_BG(0, 80, 0)
                      ANSI_FG(200, 200, 0) "Input: "
                      ANSI_FG(255, 255, 255) "{}", num_inputs);

    if (console.HasInput()) {
      std::string line = console.WaitLine();
      console.Print("Client read [{}], which is:\n{}\n",
                    line, HexDump::Color(line));
      num_inputs++;
    }
  }
}

int main(int argc, char **argv) {
  ANSI::Init();

  Chat();

  return 0;
}
