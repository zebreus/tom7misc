
#ifndef _LLM_LLM_UTIL_H
#define _LLM_LLM_UTIL_H

#include <string>
#include <cstdio>

#include "ansi.h"
#include "timer.h"
#include "base/stringprintf.h"

inline void EmitTimer(const std::string &name, const Timer &timer) {
  printf(AWHITE("%s") " in %s\n",
         name.c_str(),
         ANSI::Time(timer.Seconds()).c_str());
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
