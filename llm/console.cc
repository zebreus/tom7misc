#include "console.h"

#include <deque>
#include <string>
#include <vector>
#include <mutex>
#include <cstdio>
#include <iostream>
#include <cstdlib>
#include <chrono>
#include <condition_variable>

#include "base/logging.h"
#include "threadutil.h"
#include "ansi.h"

using namespace std;

// TODO: Printf, which keeps the output separate from the input line(s).
// TODO: Unbuffered IO.
// TODO: Way to destroy console!

Console::Console() : read_thread(ReadThread, this) {}
Console::~Console() {}

// Asynchronous console.
void Console::ReadThread() {
  for (;;) {
    string input;
    getline(cin, input);
    // printf("Got [%s]\n", input.c_str());
    {
      std::unique_lock<std::mutex> ul(m);
      input_lines.push_back(std::move(input));
    }
    cond.notify_all();
  }
}

  // Block in the calling thread until there's a line available.
std::string Console::WaitLine() {
  std::unique_lock<std::mutex> ul(m);
  // Wait (without the lock) until we have thread budget.
  cond.wait(ul, [this]{ return !input_lines.empty(); });
  CHECK(!input_lines.empty());
  std::string line = std::move(input_lines.front());
  input_lines.pop_front();
  return line;
}

bool Console::HasInput() {
  std::unique_lock<std::mutex> ul(m);
  return !input_lines.empty();
}
