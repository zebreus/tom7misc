
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
  if (seconds < 1.0) {
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
