
// TODO: To cc-lib? I don't love that you have to call a special
// printf, nor the init.

#ifndef _GRAD_ANSI_H
#define _GRAD_ANSI_H

#define ANSI_PREVLINE "\x1B[F"
#define ANSI_CLEARLINE "\x1B[2K"
#define ANSI_CLEARTOEOL "\x1B[0K"

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

// r, g, b must be numbers (numberic literals or #defined constants)
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

// Call this in main for compatibility on windows.
void AnsiInit();

// Same as printf, but using WriteConsole on windows so that we
// can communicate with pseudoterminal. Without this, ansi escape
// codes will work (VirtualTerminalProcessing) but not mintty-
// specific ones. Calls normal printf on other platforms, which
// hopefully support the ansi codes.
void CPrintf(const char* format, ...);

#endif
