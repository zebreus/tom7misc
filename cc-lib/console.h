#ifndef _CC_LIB_CONSOLE_H
#define _CC_LIB_CONSOLE_H

#include <cstdlib>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

// TODO: Unbuffered IO.
// TODO: Merge with StatusBar, or share some functionality?

// Asynchronous ANSI console, for an IRC chat-like interface. Manages
// all I/O (and will get a bit messed up if anything else writes to
// the terminal while it's active). Has:
//   - status lines at the top
//   - the history region, which is a line-based scrollback buffer
//   - "middle" status lines between the history and input region
//   - input region at bottom.
//   - bottom status lines.
// The input region automatically grows for multi-line input.
struct ConsoleData;
struct Console {

  Console(int top_status_lines,
          int max_history_lines,
          int mid_status_lines,
          int bottom_status_lines);
  ~Console();

  // Prints to the history region. Newlines are not implied.
  template<typename... Args>
  void Print(std::format_string<Args...> fmt, Args&&... args);

  enum Location {
    TOP,
    MID,
    BOT,
  };

  // Set a specific line of the status. The index must be in bounds
  // for that status line's size. Newline ignored here.
  template<typename... Args>
  void SetStatus(Location loc, int idx,
                 std::format_string<Args...> fmt, Args&&... args);

  // Block in the calling thread until there's a line available.
  // Remove it from the queue and return it.
  std::string WaitLine();

  bool HasInput();

  // Redraw the whole screen, for example if you know that the terminal
  // has changed size.
  void Redraw();

 private:
  void SetStatusTo(Location loc, int idx, std::string_view s);
  void RedrawStatusWithLock(Location loc);
  void ReplaceCursorWithLock();
  void Append(std::string_view s);
  void AppendWithLock(std::string_view s);
  std::shared_ptr<ConsoleData> data;
  std::thread read_thread;
};


// Template implementations follow:
template<typename... Args>
void Console::SetStatus(Location loc, int idx,
                        std::format_string<Args...> fmt,
                        Args&&... args) {
  SetStatusTo(loc, idx, std::format(fmt, std::forward<Args>(args)...));
}


template<typename... Args>
void Console::Print(std::format_string<Args...> fmt, Args&&... args) {
  Append(std::format(fmt, std::forward<Args>(args)...));
}


#endif
