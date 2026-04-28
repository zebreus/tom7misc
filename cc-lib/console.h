#ifndef _CC_LIB_CONSOLE_H
#define _CC_LIB_CONSOLE_H

#include <cstdlib>
#include <memory>
#include <string>
#include <thread>

// TODO: Printf, which keeps the output separate from the input line(s).
// TODO: Unbuffered IO.
// TODO: Merge with StatusBar, or share some functionality?
// Asynchronous console.
struct ConsoleData;
struct Console {
  Console();
  ~Console();

  // Block in the calling thread until there's a line available.
  // Remove it from the queue and return it.
  std::string WaitLine();

  bool HasInput();

 private:
  std::shared_ptr<ConsoleData> data;
  std::thread read_thread;
};

#endif
