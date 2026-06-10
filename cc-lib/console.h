#ifndef _CC_LIB_CONSOLE_H
#define _CC_LIB_CONSOLE_H

#include <cstdlib>
#include <format>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

/*

  For your reference, here's the implementation plan for parsed/colorized input:

  We want to allow the client to customize the rendering of the active
  input line (e.g., syntax highlighting, command coloring, rendering
  an escape sequence like "\n" as a line break) while keeping input
  latency low and ensuring correct cursor placement.

  API plan: ANSI String with Adjusted Plain-Text Cursor
  - The client provides a callback with the signature:
      std::pair<std::string, int> FormatInput(std::string_view raw_input,
                                              int raw_cursor_offset);
    where the returned string contains ANSI codes (and optional raw '\n's),
    and the returned integer is the index (codepoint) of the cursor in
    the plain UTF-8 (ANSI-stripped) version of that string.
  - Pros: Simple, highly flexible, allows clients to use standard ANSI
    formatters. For simple coloring (no length changes), the client just
    returns the raw offset.
  - Cons: Client must adjust the cursor offset if they modify text lengths
    (e.g., replacing "\\n" with '\n').

  Thread Safety & Latency:
  - We must not hold the internal console mutex `data->m` while executing the
    client callback, as it could block other threads and degrade UI latency.
  - Instead, the input thread (`ReadThread`) will:
    1. Copy the current input state (raw text & cursor position) under the lock.
    2. Release the lock.
    3. Run the formatter callback.
    4. Re-acquire the lock and update a cached formatted string & plain
       cursor offset in `ConsoleData`.
  - Any call to `Redraw()` (which might be triggered by resizing or history
    updates) will simply read from the cache, ensuring redraws remain fast,
    deterministic, and completely independent of the client's callback latency.

  Line Wrapping & Cursor Mapping:
  - To wrap the client's output, we will first split the formatted string
    by '\n' (if any).
  - For each chunk, we will wrap it using a modified version of `AnsiSplitLines`
    that also tracks the plain-text cursor offset. As we consume characters
    to build wrapped lines, we can precisely identify the row and column of
    the cursor.
*/

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

  // Replace the current input with this string as though the
  // cleared their current input and typed this. Does not process
  // special characters. Should not have a newline.
  void SetInput(std::string_view s);

  // Set the input formatter. This takes a plain UTF-8 string and a
  // cursor position (codepoint offset) representing the current
  // input. It returns a UTF-8 string which may have ANSI color codes
  // and newlines in it, as well as a cursor position (codepoint offset)
  // for the color-stripped string.
  using Formatter =
    std::function<std::pair<std::string, int>(std::string_view, int)>;
  void SetFormatter(Formatter f);

  bool HasInput();

  // Redraw the whole screen, for example if you know that the terminal
  // has changed size.
  void Redraw();

 private:
  void SetStatusTo(Location loc, int idx, std::string_view s);
  void RedrawStatusWithLock(Location loc);
  void HideCursorWithLock();
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
