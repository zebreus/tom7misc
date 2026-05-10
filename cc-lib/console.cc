#include "console.h"

#include <algorithm>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <winnt.h>
#include <processenv.h>
#include <minwindef.h>
#include <consoleapi.h>
#else
// POSIX
#include <termios.h>
#include <unistd.h>
#include <iostream>
#endif

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "utf8.h"

// Hot tip for mysterious crashes: Make sure you're not
// calling Console::Print when you meant ::Print!

#if defined(_WIN32)
struct ScopedRawMode {
  ScopedRawMode() {
    HANDLE hstdin = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(hstdin, &old_mode);

    DWORD mode = old_mode;
    mode |= ENABLE_VIRTUAL_TERMINAL_INPUT;
    mode &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);
    SetConsoleMode(hstdin, mode);
  }

  ~ScopedRawMode() {
    HANDLE hstdin = GetStdHandle(STD_INPUT_HANDLE);
    SetConsoleMode(hstdin, old_mode);
  }

  static constexpr bool ASYNCHRONOUS = true;

 private:
  DWORD old_mode;
  ScopedRawMode(ScopedRawMode &) = delete;
  ScopedRawMode &operator =(ScopedRawMode &) = delete;
};

#elif defined(_POSIX_C_SOURCE)
struct ScopedRawMode {
  ScopedRawMode() {
    struct termios newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
  }

  ~ScopedRawMode() {
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
  }

  static constexpr bool ASYNCHRONOUS = true;

 private:
  struct termios oldt;
  ScopedRawMode(ScopedRawMode &) = delete;
  ScopedRawMode &operator =(ScopedRawMode &) = delete;
};

#else
struct ScopedRawMode {
  /* nothing */
  static constexpr bool ASYNCHRONOUS = false;
};

#endif

using namespace std;

struct ConsoleData {
  std::mutex m;
  std::condition_variable cond;
  bool should_die = false;

  // TODO: Make this configurable
  std::string input_style = ANSI_RESET;

  // May become null if detached.
  Console *parent = nullptr;

  // Current line (user hasn't pressed enter).
  std::string current_line;

  // Lines that have already been committed by the user by
  // pressing enter (so we don't display them) but that
  // haven't ben read by the client.
  std::deque<std::string> input_lines;

  int screen_cols = 80, screen_rows = 24;

  int max_history_lines = 10000;

  // The history as lines without newlines.
  // These lines are not wrapped!
  std::vector<std::string> history;

  // Array of lines for each status region.
  std::vector<std::string> top_status;
  std::vector<std::string> mid_status;
  std::vector<std::string> bot_status;

  ConsoleData(Console *parent,
              int top_status_lines,
              int max_history_lines,
              int mid_status_lines,
              int bottom_status_lines) :
    parent(parent),
    max_history_lines(max_history_lines) {
    top_status.resize(top_status_lines, "");
    mid_status.resize(mid_status_lines, "");
    bot_status.resize(bottom_status_lines, "");
  }
};

void Console::AppendWithLock(std::string_view s) {
  std::vector<std::string> &history = data->history;

  if (history.empty()) history.push_back("");

  for (;;) {
    size_t pos = s.find('\n');
    if (pos == std::string_view::npos) {
      if (!s.empty()) {
        history.back().append(s);
      }
      return;
    }

    history.back().append(s.substr(0, pos));
    history.push_back("");
    s.remove_prefix(pos + 1);
  }
}

void Console::Append(std::string_view s) {
  {
    std::unique_lock<std::mutex> ul(data->m);
    AppendWithLock(s);
  }

  Redraw();
}

static void ReadThread(std::shared_ptr<ConsoleData> data) {
  // Input happens in this thread, so protect the raw IO
  // state using RAII here.
  ScopedRawMode scoped_raw_mode;

  for (;;) {

    // Do we need to do something other than let the OS
    // echo the character where the cursor is?
    bool input_dirty = false;
    bool is_enter = false;

    if constexpr (ScopedRawMode::ASYNCHRONOUS) {
      uint8_t first_byte = 0;
      #if _WIN32
      {
        int c = std::cin.get();
        if (c == std::char_traits<char>::eof()) {
          // TODO: Should probably note EOF here..?
          return;
        }
        first_byte = c;
      }
      #else

      if (std::cin.readsome(&first_byte, 1) == 0) {
        {
          std::unique_lock<std::mutex> ul(data->m);
          if (data->should_die) return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        continue;
      }
      #endif

      uint8_t u_first = first_byte;
      int enc_len = 1;
      if ((u_first & 0xe0) == 0xc0) enc_len = 2;
      else if ((u_first & 0xf0) == 0xe0) enc_len = 3;
      else if ((u_first & 0xf8) == 0xf0) enc_len = 4;

      char buffer[4];
      buffer[0] = first_byte;
      for (int i = 1; i < enc_len; ++i) {
        buffer[i] = (char)std::cin.get();
      }

      auto [_, cp] = UTF8::ParsePrefix(buffer, enc_len);

      ::Print("{}", UTF8::Encode(cp));
      fflush(stdout);

      {
        std::unique_lock<std::mutex> ul(data->m);
        if (data->should_die) return;

        if (cp == '\n' || cp == '\r') {
          input_dirty = true;
          is_enter = true;
        } else if (cp == '\b' || cp == 0x7F) {
          input_dirty = true;
          size_t len = UTF8::Length(data->current_line);
          if (len > 0) {
            data->current_line =
              UTF8::Truncate(data->current_line, len - 1);
          }
        } else if (cp != UTF8::INVALID) {
          data->current_line += UTF8::Encode(cp);
        }
      }

    } else {
      // All we can do is wait for an entire line of input, then.
      std::string input;
      std::getline(cin, input);
      {
        std::unique_lock<std::mutex> ul(data->m);
        if (data->should_die) return;
        data->current_line = std::move(input);
      }
      is_enter = true;
      input_dirty = true;
      data->cond.notify_all();
    }

    if (is_enter) {
      std::unique_lock<std::mutex> ul(data->m);
      if (data->should_die) return;

      data->input_lines.push_back(std::move(data->current_line));
      data->current_line.clear();

      data->cond.notify_all();
    }

    // XXX this is not right because we might be detached!
    // but we don't want to be holding the mutex when we
    // call this...
    if (input_dirty) {
      data->parent->Redraw();
    }
  }
}

// TODO: Unbuffered IO.

Console::Console(int top_status_lines,
                 int max_history_lines,
                 int mid_status_lines,
                 int bottom_status_lines) :
  data(std::make_shared<ConsoleData>(this,
                                     top_status_lines,
                                     max_history_lines,
                                     mid_status_lines,
                                     bottom_status_lines)),
  read_thread(&ReadThread, data) {}

Console::~Console() {
  {
    std::unique_lock<std::mutex> ul(data->m);
    data->should_die = true;
    data->cond.notify_all();
  }

  read_thread.detach();
}

// Split the (ansi-colored) input string into lines of at most the
// given number of columns. The result always has at least one
// line, even if the input is empty.
static std::vector<std::string> AnsiSplitLines(std::string_view in,
                                               int cols) {
  std::string line(in);
  std::vector<std::string> line_parts;
  while (ANSI::StringWidth(line) > cols) {
    line_parts.push_back(ANSI::ColorSubstring(line, 0, cols));
    line = ANSI::ColorSubstring(line, cols);
  }

  line_parts.push_back(std::move(line));
  return line_parts;
}

// Get the part of the history that fits on the screen, wrapping
// to lines.
// TODO: Support scrollback with pgup/pgdn
static std::vector<std::string>
GetHistoryLines(std::span<const std::string> history,
                int nlines, int cols) {
  std::deque<std::string> lines;

  for (int pos = history.size() - 1;
       pos >= 0 && (int)lines.size() < nlines;
       pos--) {
    std::string line = history[pos];
    // TODO: Wrap at words?

    // Wrap the line if it exceeds the number of columns.
    std::vector<std::string> line_parts =
      AnsiSplitLines(line, cols);

    // There will always be at least one line in the line_parts array,
    // even if the original line is empty, but that's what we want.
    while ((int)lines.size() < nlines && !line_parts.empty()) {
      lines.push_front(std::move(line_parts.back()));
      line_parts.pop_back();
    }
  }

  return std::vector<std::string>(lines.begin(), lines.end());
}

void Console::SetStatusTo(Location loc, int idx, std::string_view s) {
  std::string str(s);
  str.erase(std::remove(str.begin(), str.end(), '\n'), str.end());
  str.erase(std::remove(str.begin(), str.end(), '\r'), str.end());

  std::unique_lock<std::mutex> ul(data->m);
  switch (loc) {
    case TOP:
      CHECK(idx >= 0 && idx < (int)data->top_status.size());
      data->top_status[idx] = std::move(str);
      break;
    case MID:
      CHECK(idx >= 0 && idx < (int)data->mid_status.size());
      data->mid_status[idx] = std::move(str);
      break;
    case BOT:
      CHECK(idx >= 0 && idx < (int)data->bot_status.size());
      data->bot_status[idx] = std::move(str);
      break;
  }

  HideCursorWithLock();
  RedrawStatusWithLock(loc);
  ReplaceCursorWithLock();
}

// Print a line, padding it with spaces to the full screen width to
// overwrite previous contents. Avoids a newline on the very last line
// to prevent scrolling.
static void PrintPadded(
    int row,
    std::string_view start_style,
    std::string_view line_in,
    int rows, int cols) {
  std::string line(line_in);
  int w = ANSI::StringWidth(line);
  if (w > cols) {
    line = ANSI::ColorSubstring(line, 0, cols);
  }
  // Are we using exactly the full width?
  w = ANSI::StringWidth(line);
  std::string_view eol = (w == cols) ? "" : "\x1B[K";

  ::Print(
      // Go to column 1 in the line
      "\x1b[{};1H"
      // And reset to default style.
      "{}"
      // And then the line and EOL.
      "{}" "{}",
      row + 1,
      start_style,
      line, eol);
}

// Does not hide or show cursor.
void Console::RedrawStatusWithLock(Location loc) {
  if (loc == Location::TOP) {
    for (size_t r = 0; r < data->top_status.size(); r++) {
      PrintPadded(r, ANSI_RESET, data->top_status[r],
                  data->screen_rows, data->screen_cols);
    }
    return;
  }

  if (loc == Location::BOT) {
    const int bot_loc = data->screen_rows - data->bot_status.size();
    for (size_t r = 0; r < data->bot_status.size(); r++) {
      PrintPadded(bot_loc + r, ANSI_RESET, data->bot_status[r],
                  data->screen_rows, data->screen_cols);
    }
    return;
  }

  // Otherwise, the location depends on the size of the
  // input.

  // PERF: Cache this?
  std::vector<std::string> input_lines =
    AnsiSplitLines(data->current_line, data->screen_cols);

  const int ninput = input_lines.size();
  const int ntop = data->top_status.size();
  const int nmid = data->mid_status.size();
  const int nbot = data->bot_status.size();
  const int nhist = data->screen_rows - ninput - ntop - nmid - nbot;

  CHECK(loc == Location::MID);

  const int mid_loc = ntop + nhist;
  for (size_t r = 0; r < data->mid_status.size(); r++) {
    PrintPadded(mid_loc + r, ANSI_RESET, data->mid_status[r],
                data->screen_rows, data->screen_cols);
  }
}

void Console::Redraw() {
  const auto &[new_cols, new_rows] =
    ANSI::TerminalDimensions().value_or(std::make_pair(80, 24));

  std::unique_lock<std::mutex> ul(data->m);
  data->screen_cols = new_cols;
  data->screen_rows = new_rows;

  std::vector<std::string> input_lines =
    AnsiSplitLines(data->current_line, new_cols);

  const int ninput = input_lines.size();

  const int ntop = data->top_status.size();
  const int nmid = data->mid_status.size();
  const int nbot = data->bot_status.size();

  const int nhist = data->screen_rows - ninput - ntop - nmid - nbot;

  std::vector<std::string> hist_lines =
    GetHistoryLines(data->history, std::max(nhist, 0), new_cols);

  // "\x1B[%dA" and "\x1B[%dG"

  // Don't show a flickering cursor while we're redrawing.
  // We restore this at the end.
  HideCursorWithLock();

  // Move to the top-left of the screen.
  // (Not necessary now because we move to each section.)
  // ::Print(ANSI_HOME);

  RedrawStatusWithLock(Location::TOP);

  int empty_hist_lines = std::max(nhist, 0) - (int)hist_lines.size();

  const int hist_loc = ntop;
  for (int i = 0; i < empty_hist_lines; i++) {
    PrintPadded(hist_loc + i, ANSI_RESET, "", new_rows, new_cols);
  }

  for (size_t i = 0; i < hist_lines.size(); i++) {
    const std::string &line = hist_lines[i];
    PrintPadded(hist_loc + empty_hist_lines + i,
                ANSI_RESET, line, new_rows, new_cols);
  }

  RedrawStatusWithLock(Location::MID);

  const int input_loc = ntop + nhist + nmid;
  for (size_t i = 0; i < input_lines.size(); i++) {
    const std::string &line = input_lines[i];
    PrintPadded(input_loc + i,
                data->input_style,
                line, new_rows, new_cols);
  }

  RedrawStatusWithLock(Location::BOT);

  ReplaceCursorWithLock();
}

void Console::HideCursorWithLock() {
  ::Print("\x1b[?25l");
}

void Console::ReplaceCursorWithLock() {
  // PERF: Cache this
  // Restore the cursor to the input line so typing feels natural.
  std::vector<std::string> input_lines =
    AnsiSplitLines(data->current_line, data->screen_cols);

  // const int ninput = input_lines.size();
  const int nbot = data->bot_status.size();

  int cursor_row = data->screen_rows - nbot - 1;
  int cursor_col = ANSI::StringWidth(input_lines.back());
  ::Print(
      // move to the end of the input
      "\x1b[{};{}H"
      // and show cursor again
      "\x1b[?25h"
      // and reset colors
      ANSI_RESET, cursor_row + 1, cursor_col + 1);

  fflush(stdout);
}

// Block in the calling thread until there's a line available.
std::string Console::WaitLine() {
  std::unique_lock<std::mutex> ul(data->m);
  // Wait (without the lock) until we have thread budget.
  data->cond.wait(ul, [this]{ return !data->input_lines.empty(); });
  CHECK(!data->input_lines.empty());
  std::string line = std::move(data->input_lines.front());
  data->input_lines.pop_front();
  return line;
}

bool Console::HasInput() {
  std::unique_lock<std::mutex> ul(data->m);
  return !data->input_lines.empty();
}
