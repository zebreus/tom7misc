#ifndef _CONSOLE_H
#define _CONSOLE_H

#include <deque>
#include <string>
#include <mutex>
#include <cstdlib>
#include <thread>
#include <condition_variable>

// TODO: Printf, which keeps the output separate from the input line(s).
// TODO: Unbuffered IO.
// Asynchronous console.
struct Console {
  Console();
  ~Console();

  // Block in the calling thread until there's a line available.
  // Remove it from the queue and return it.
  std::string WaitLine();

  bool HasInput();

private:
  void ReadThread();
  std::mutex m;
  std::condition_variable cond;
  bool should_die = false;
  std::thread read_thread;
  std::deque<std::string> input_lines;
};

#endif
