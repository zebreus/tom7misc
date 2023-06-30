
#include "ansi.h"

#include <cstdlib>
#include <string>
#include <cmath>

#ifdef __MINGW32__
#include <windows.h>
#undef ARRAYSIZE
#endif

// TODO: Would be good to avoid this dependency too.
#include "base/stringprintf.h"

void AnsiInit() {
  #ifdef __MINGW32__
  // Turn on ANSI support in Windows 10+. (Otherwise, use ANSICON etc.)
  // https://docs.microsoft.com/en-us/windows/console/setconsolemode
  //
  // TODO: This works but seems to subsequently break control keys
  // and stuff like that in cygwin bash?
  HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
  // mingw headers may not know about this new flag
  static constexpr int kVirtualTerminalProcessing = 0x0004;
  DWORD old_mode = 0;
  GetConsoleMode(hStdOut, &old_mode);
  SetConsoleMode(hStdOut, old_mode | kVirtualTerminalProcessing);

  // XXX Not related to ANSI; enables utf-8 output.
  // Maybe we should separate this out? Someone might want to use
  // high-ascii output?
  SetConsoleOutputCP( CP_UTF8 );
  #endif
}

// TODO: It would be better if we had a way of stripping the
// terminal codes if they are not supported?
void CPrintf(const char* format, ...) {
  // Do formatting.
  va_list ap;
  va_start(ap, format);
  std::string result;
  StringAppendV(&result, format, ap);
  va_end(ap);

  #ifdef __MINGW32__
  DWORD n = 0;
  WriteConsole(GetStdHandle(STD_OUTPUT_HANDLE),
               result.c_str(),
               result.size(),
               &n,
               nullptr);
  #else
  printf("%s", result.c_str());
  #endif
}

std::string AnsiTime(double seconds) {
  char result[64] = {};
  if (seconds < 0.001) {
    sprintf(result, AYELLOW("%.2f") "us", seconds * 1000000.0);
  } else if (seconds < 1.0) {
    sprintf(result, AYELLOW("%.2f") "ms", seconds * 1000.0);
  } else if (seconds < 60.0) {
    sprintf(result, AYELLOW("%.3f") "s", seconds);
  } else if (seconds < 60.0 * 60.0) {
    int sec = std::round(seconds);
    int omin = sec / 60;
    int osec = sec % 60;
    sprintf(result, AYELLOW("%d") "m" AYELLOW("%02d") "s",
            omin, osec);
  } else {
    int sec = std::round(seconds);
    int ohour = sec / 3600;
    sec -= ohour * 3600;
    int omin = sec / 60;
    int osec = sec % 60;
    sprintf(result, AYELLOW("%d") "h"
            AYELLOW("%d") "m"
            AYELLOW("%02d") "s",
            ohour, omin, osec);
  }
  return (string)result;
}

std::string AnsiStripCodes(const std::string &s) {
  std::string out;
  out.reserve(s.size());
  bool in_escape = false;
  for (int i = 0; i < (int)s.size(); i++) {
    if (in_escape) {
      if (s[i] >= '@' && s[i] <= '~') {
        in_escape = false;
      }
    } else {
      // OK to access one past the end of the string.
      if (s[i] == '\x1B' && s[i + 1] == '[') {
        in_escape = true;
        i++;
      } else {
        out += s[i];
      }
    }
  }
  return out;
}

int AnsiStringWidth(const std::string &s) {
  // PERF could do this without copying.
  return AnsiStripCodes(s).size();
}

std::string AnsiProgressBar(uint64_t numer, uint64_t denom,
                            const std::string &operation,
                            double seconds) {
  // including [].
  static constexpr int FULL_WIDTH = 76;

  double frac = numer / (double)denom;

  double spe = numer > 0 ? seconds / numer : 1.0;
  double remaining_sec = (denom - numer) * spe;
  string eta = AnsiTime(remaining_sec);
  int eta_len = AnsiStringWidth(eta);

  int bar_width = FULL_WIDTH - 2 - 1 - eta_len;
  // Number of characters that get background color.
  int filled_width = std::clamp((int)(bar_width * frac), 0, bar_width);
  string bar_text = StringPrintf("%llu / %llu  (%.1f%%) %s", numer, denom,
                                 frac * 100.0,
                                 operation.c_str());
  // could do "..."
  if ((int)bar_text.size() > bar_width) bar_text.resize(bar_width);
  bar_text.reserve(bar_width);
  while ((int)bar_text.size() < bar_width) bar_text.push_back(' ');

  // int unfilled_width = bar_width - filled_width;
  string colored_bar =
    StringPrintf(ANSI_FG(252, 252, 230)
                 ANSI_BG(15, 21, 145) "%s"
                 ANSI_BG(0, 3, 26) "%s"
                 ANSI_RESET,
                 bar_text.substr(0, filled_width).c_str(),
                 bar_text.substr(filled_width).c_str());

  string out = AWHITE("[") + colored_bar + AWHITE("]") " " + eta;
  return out;
}
