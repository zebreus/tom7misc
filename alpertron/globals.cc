#include "globals.h"

#include <cstdlib>
#include <cstring>

#include "bignbr.h"

static char *GetOutput() {
  char *output = new char[2'000'000'000];
  memset(output, 0, 2'000'000'000);
  return output;
}

char *output = GetOutput();


