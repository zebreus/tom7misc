// It is very common for console apps to report some
// status using ANSI color etc., and to want to update
// that (like a progress bar). If the program also
// outputs other stuff, then we get ugly duplicated
// progress bars or messages get overwritten. This
// manages I/O so that it comes out cleanly.

#ifndef _CC_LIB_STATUS_BAR_H
#define _CC_LIB_STATUS_BAR_H

#include <string>
#include <vector>
#include <mutex>

#include "base/port.h"

// Thread safe.
struct StatusBar {
  // Give the fixed number of lines that the status bar uses.
  // Must be greater than zero.
  explicit StatusBar(int num_lines);

  // Print to the screen. Adds trailing newline if not present.
  void Printf(const char* format, ...); PRINTF_ATTRIBUTE(1, 2);

  // Prints to the screen. Adds trailing newline if not present.
  void Emit(const std::string &s);

  // Update the status bar. This should be done in one call that
  // contains num_lines lines. Trailing newline not necessary.
  void Statusf(const char* format, ...) PRINTF_ATTRIBUTE(1, 2);

  // Prints the status to the screen.
  void EmitStatus(const std::string &s);

 private:
  void MoveUp();

  void EmitStatusLinesWithLock(
      const std::vector<std::string> &lines);

  std::mutex m;
  int num_lines = 0;
  bool first = true;
  std::vector<std::string> prev_status_lines;
};



#endif
