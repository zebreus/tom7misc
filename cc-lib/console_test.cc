
#include "console.h"

#include <deque>
#include <cstdlib>
#include <chrono>
#include <string>
#include <thread>

#include "ansi.h"
#include "base/print.h"
#include "threadutil.h"

void Chat() {
  Console console;
  Print("This is an interactive test!\n");

  for (;;) {
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(200ms);
    Print("hi ");

    if (console.HasInput()) {
      std::string line = console.WaitLine();
      Print("Client read [{}]\n", line);
    }
  }
}

int main(int argc, char **argv) {
  ANSI::Init();

  Chat();

  return 0;
}
