
#include <cstdio>
#include <functional>

#include "base/logging.h"
#include "util.h"
#include "um.h"

int main(int argc, char **argv) {
  CHECK(argc == 2) << "./run-um.exe codex.um";

  std::vector<uint8_t> pgm = Util::ReadFileBytes(argv[1]);
  CHECK(!pgm.empty()) << argv[1];

  UM um(pgm);

  um.Run([]() { return getchar(); },
         [](uint8_t c) { putchar(c); fflush(stdout); });

  return 0;
}
