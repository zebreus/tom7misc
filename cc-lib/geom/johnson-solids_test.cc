
#include "johnson-solids.h"

#include "ansi.h"
#include "base/print.h"

static void Create() {
  for (int i = 1; i <= 92; i++) {
    (void)JohnsonSolid(i);
  }
}

int main(int argc, char **argv) {
  ANSI::Init();

  Create();

  Print("OK\n");
  return 0;
}
