
#include "layout.h"

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "prop.h"
#include <vector>

static void Empty() {
  World empty;
  std::vector<Prop> nothing;

  Layout trivial = DoLayout(empty, nothing);
  CHECK(trivial.input_vars.empty());
  // ... should be one empty layer in this case? ...
}

int main(int argc, char **argv) {
  ANSI::Init();

  Empty();

  Print("OK\n");
  return 0;
}
