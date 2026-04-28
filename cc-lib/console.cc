#include "console.h"

#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

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
#include "threadutil.h"
#include "utf8.h"

#if defined(_WIN32)
struct ScopedRawMode {
  ScopedRawMode() {
    HANDLE hstdin = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(hstdin, &old_mode);
    SetConsoleMode(hstdin,
                   old_mode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT));
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
  std::string current_line;
  std::deque<std::string> input_lines;
};

static void ReadThread(std::shared_ptr<ConsoleData> data) {
  // Input happens in this thread, so protect the raw IO
  // state using RAII here.
  ScopedRawMode scoped_raw_mode;

  for (;;) {
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

      Print("{}", UTF8::Encode(cp));
      fflush(stdout);

      bool is_enter = false;
      {
        std::unique_lock<std::mutex> ul(data->m);
        if (data->should_die) return;

        if (cp == '\n' || cp == '\r') {
          is_enter = true;
        } else if (cp == '\b' || cp == 0x7F) {
          size_t len = UTF8::Length(data->current_line);
          if (len > 0) {
            data->current_line = UTF8::Truncate(data->current_line, len - 1);
          }
        } else if (cp != UTF8::INVALID) {
          data->current_line += UTF8::Encode(cp);
        }
      }

      if (!is_enter) {
        continue;
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
      data->cond.notify_all();
    }

    // Print("Got [%s]\n", input.c_str());
    {
      std::unique_lock<std::mutex> ul(data->m);
      if (data->should_die) return;
      data->input_lines.push_back(std::move(data->current_line));
      data->current_line.clear();
    }
    data->cond.notify_all();
  }
}

// TODO: Printf, which keeps the output separate from the input line(s).
// TODO: Unbuffered IO.
// TODO: Way to destroy console!

Console::Console() : data(std::make_shared<ConsoleData>()),
                     read_thread(&ReadThread, data) {}
Console::~Console() {
  {
    std::unique_lock<std::mutex> ul(data->m);
    data->should_die = true;
    data->cond.notify_all();
  }

  read_thread.detach();
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
