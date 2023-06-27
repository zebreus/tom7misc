
// TODO: Clean up for CC_LIB.

#ifndef _CC_LIB_ANSI_H
#define _CC_LIB_ANSI_H

#include <string>
#include <cstdint>

#define ANSI_PREVLINE "\x1B[F"
#define ANSI_CLEARLINE "\x1B[2K"
#define ANSI_CLEARTOEOL "\x1B[0K"
#define ANSI_BEGINNING_OF_LINE "\x1B[G"
// Put the cursor at the beginning of the current line, and clear it.
#define ANSI_RESTART_LINE ANSI_BEGINNING_OF_LINE ANSI_CLEARTOEOL

#define ANSI_RED "\x1B[1;31;40m"
#define ANSI_GREY "\x1B[1;30;40m"
#define ANSI_BLUE "\x1B[1;34;40m"
#define ANSI_CYAN "\x1B[1;36;40m"
#define ANSI_YELLOW "\x1B[1;33;40m"
#define ANSI_GREEN "\x1B[1;32;40m"
#define ANSI_WHITE "\x1B[1;37;40m"
#define ANSI_PURPLE "\x1B[1;35;40m"
#define ANSI_RESET "\x1B[m"

#define ARED(s) ANSI_RED s ANSI_RESET
#define AGREY(s) ANSI_GREY s ANSI_RESET
#define ABLUE(s) ANSI_BLUE s ANSI_RESET
#define ACYAN(s) ANSI_CYAN s ANSI_RESET
#define AYELLOW(s) ANSI_YELLOW s ANSI_RESET
#define AGREEN(s) ANSI_GREEN s ANSI_RESET
#define AWHITE(s) ANSI_WHITE s ANSI_RESET
#define APURPLE(s) ANSI_PURPLE s ANSI_RESET

// standard two-level trick to expand and stringify
#define ANSI_INTERNAL_STR2(s) #s
#define ANSI_INTERNAL_STR(s) ANSI_INTERNAL_STR2(s)

// r, g, b must be numbers (numeric literals or #defined constants)
// in [0,255].
#define ANSI_FG(r, g, b) "\x1B[38;2;" \
  ANSI_INTERNAL_STR(r) ";" \
  ANSI_INTERNAL_STR(g) ";" \
  ANSI_INTERNAL_STR(b) "m"

#define ANSI_BG(r, g, b) "\x1B[48;2;" \
  ANSI_INTERNAL_STR(r) ";" \
  ANSI_INTERNAL_STR(g) ";" \
  ANSI_INTERNAL_STR(b) "m"

#define AFGCOLOR(r, g, b, s) "\x1B[38;2;" \
  ANSI_INTERNAL_STR(r) ";" \
  ANSI_INTERNAL_STR(g) ";" \
  ANSI_INTERNAL_STR(b) "m" s ANSI_RESET

#define ABGCOLOR(r, g, b, s) "\x1B[48;2;" \
  ANSI_INTERNAL_STR(r) ";" \
  ANSI_INTERNAL_STR(g) ";" \
  ANSI_INTERNAL_STR(b) "m" s ANSI_RESET

// Without ANSI_RESET.
std::string AnsiForegroundRGB(uint8_t r, uint8_t g, uint8_t b);
std::string AnsiBackgroundRGB(uint8_t r, uint8_t g, uint8_t b);

// Strip ANSI codes. Only CSI codes are supported (which is everything
// in this file), and they are not validated. There are many nonstandard
// codes that would be treated incorrectly here.
std::string AnsiStripCodes(const std::string &s);

// Return the number of characters after stripping ANSI codes. Same
// caveats as above.
int AnsiStringWidth(const std::string &s);

// Call this in main for compatibility on windows.
void AnsiInit();

// Same as printf, but using WriteConsole on windows so that we
// can communicate with pseudoterminal. Without this, ansi escape
// codes will work (VirtualTerminalProcessing) but not mintty-
// specific ones. Calls normal printf on other platforms, which
// hopefully support the ansi codes.
void CPrintf(const char* format, ...);


inline std::string AnsiForegroundRGB(uint8_t r, uint8_t g, uint8_t b) {
  char escape[24] = {};
  sprintf(escape, "\x1B[38;2;%d;%d;%dm", r, g, b);
  return escape;
}

inline std::string AnsiBackgroundRGB(uint8_t r, uint8_t g, uint8_t b) {
  // Max size is 12.
  char escape[24] = {};
  sprintf(escape, "\x1B[48;2;%d;%d;%dm", r, g, b);
  return escape;
}

// Return an ansi-colored representation of the duration, with arbitrary
// but Tom 7-approved choices of color and significant figures.
std::string AnsiTime(double seconds);

// Print a color progress bar. [numer/denom  operation  ETA HH:MM:SS]
// Doesn't update in place; you need to move the cursor for that.
std::string AnsiProgressBar(uint64_t numer, uint64_t denom,
                            const std::string &operation,
                            double taken);

#endif
