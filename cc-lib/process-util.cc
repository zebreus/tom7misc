
#include "process-util.h"

#include <cstdio>
#include <optional>
#include <stdio.h>
#include <string>

using namespace std;

#if defined(WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
// Non-standard mode that may cause linux glibc to fail.
# define POPEN_MODE "rb"
#else
# define POPEN_MODE "r"
#endif

// popen() does kind of work on windows, but some characters
// cannot be piped through to the process.
std::optional<string> ProcessUtil::GetOutput(const string &cmd) {
  string ret;

  FILE *f = popen(cmd.c_str(), POPEN_MODE);
  if (f == nullptr) return {};

  int count = 0;
  static constexpr int CHUNK_SIZE = 256;
  char buf[CHUNK_SIZE];
  while ((count = fread(buf, 1, CHUNK_SIZE, f))) {
    ret.append(buf, count);
    // Short reads are technically OK; just try again.
  }

  pclose(f);

  return {ret};
}

