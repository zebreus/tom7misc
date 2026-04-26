
#include "console.h"

#include <deque>
#include <cstdlib>
#include <chrono>

#include "threadutil.h"
#include "ansi.h"


void Chat() {
  Console console;

  for (;;) {
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(200ms);
    printf("hi ");

    if (console.HasInput()) {
      std::string line = console.WaitLine();
      printf("Client read [%s]\n", line.c_str());
    }
  }
}

int main(int argc, char **argv) {
  ANSI::Init();

  Chat();

  return 0;
}
