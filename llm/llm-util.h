
#ifndef _LLM_LLM_UTIL_H
#define _LLM_LLM_UTIL_H

#include <string>
#include <cstdio>

#include "ansi.h"
#include "timer.h"
#include "base/stringprintf.h"

inline std::string AnsiTime(double seconds) {
  if (seconds < 1.0) {
    return StringPrintf(AYELLOW("%.2f") "ms", seconds * 1000.0);
  } else if (seconds < 60.0) {
    return StringPrintf(AYELLOW("%.3f") "s", seconds);
  } else if (seconds < 60.0 * 60.0) {
    int sec = std::round(seconds);
    int omin = sec / 60;
    int osec = sec % 60;
    return StringPrintf(AYELLOW("%d") "m" AYELLOW("%02d") "s",
                        omin, osec);
  } else {
    int sec = std::round(seconds);
    int ohour = sec / 3600;
    sec -= ohour * 3600;
    int omin = sec / 60;
    int osec = sec % 60;
    return StringPrintf(AYELLOW("%d") "h"
                        AYELLOW("%d") "m"
                        AYELLOW("%02d") "s",
                        ohour, omin, osec);
  }
}

inline void EmitTimer(const std::string &name, const Timer &timer) {
  printf(AWHITE("%s") " in %s\n",
         name.c_str(),
         AnsiTime(timer.Seconds()).c_str());
}

static inline bool ContainsChar(const std::string &s, char t) {
  for (char c : s)
    if (c == t) return true;
  return false;
}


static bool IsAscii(const std::string &s) {
  for (char c : s) {
    if (c < ' ' || c > '~') return false;
  }
  return true;
}

static bool AllSpace(const std::string &s) {
  for (char c : s) {
    if (c != ' ') return false;
  }
  return true;
}

#endif
