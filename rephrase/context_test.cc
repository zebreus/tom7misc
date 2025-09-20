
#include "context.h"

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"

namespace il {

static void CreateAndDestroy() {
  [[maybe_unused]]
  ElabContext elab_context;

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

  Print("OK\n");
  return 0;
}
