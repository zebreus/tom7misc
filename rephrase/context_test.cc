
#include "context.h"

#include <string>

static void CreateAndDestroy() {
  [[maybe_unused]]
  Context context;
}

static void Simple() {
  Context context;



}

int main(int argc, char **argv) {
  CreateAndDestroy();
  Simple();

  printf("OK\n");
  return 0;
}
