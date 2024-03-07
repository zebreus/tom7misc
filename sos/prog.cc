
#include <cstdio>

#include "ansi.h"
#include "base/logging.h"
#include "util.h"

int main(int argc, char **argv) {
  ANSI::Init();
  CHECK(argc == 4) << "./prog.exe n x y\n";
  int n = Util::stoi(argv[1]);
  int x = Util::stoi(argv[2]);
  int y = Util::stoi(argv[3]);

  printf("%d %d %d\n"
         "%d %d %d\n"
         "%d %d %d\n",
         n + 2 * x + y, n, n + x + 2 * y,
         n + 2 * y, n + x + y, n + 2 * x,
         n + x, n + 2 * x + 2 * y, n + y);
  return 0;
}
