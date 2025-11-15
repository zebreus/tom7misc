
#include "process-util.h"

#include <cstdio>
#include <optional>
#include <stdio.h>
#include <string>

using namespace std;

// popen() does kind of work on windows, but some characters
// cannot be piped through to the process.
std::optional<string> ProcessUtil::GetOutput(const string &cmd) {
  string ret;

  FILE *f = popen(cmd.c_str(), "rb");
  if (f == nullptr) return {};

  int count = 0;
  static constexpr int CHUNK_SIZE = 256;
  char buf[CHUNK_SIZE];
  while ((count = fread(buf, 1, CHUNK_SIZE, f))) {
    ret.append(buf, count);
    if (count < CHUNK_SIZE) break;
  }

  pclose(f);

  return {ret};
}

