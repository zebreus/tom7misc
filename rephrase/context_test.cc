
#include "context.h"

#include <string>
#include "base/logging.h"
#include "ansi.h"

namespace il {

static void CreateAndDestroy() {
  [[maybe_unused]]
  Context context;
}

static void Simple() {
  // Context context;
  // TODO!
}

}  // il

int main(int argc, char **argv) {
  ANSI::Init();
  il::CreateAndDestroy();
  il::Simple();

  printf("OK\n");
  return 0;
}
