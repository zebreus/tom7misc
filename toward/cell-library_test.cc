
#include "cell-library.h"

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"

static void CreateAndDestroy() {
  CellLibrary library;
}

int main(int argc, char **argv) {
  ANSI::Init();

  CreateAndDestroy();

  Print("OK\n");
  return 0;
}
