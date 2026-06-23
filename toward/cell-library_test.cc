
#include "cell-library.h"

#include <memory>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "level.h"

static void Simple() {
  // Simply creating the library validates that nothing is seriously
  // wrong with the SVG files.a
  CellLibrary library;

  {
    CellLibrary::Info info = library.GetInfo(
        CellLibrary::Spacer(7));
    CHECK(info.block_width == 7);
    CHECK(info.inputs.empty());
    CHECK(info.outputs.empty());
  }

  {
    std::unique_ptr<Level> spacer1 =
      library.GetLevel(CellLibrary::Spacer(1));
    CHECK(spacer1.get() != nullptr);
    CHECK(spacer1->inputs.empty());
    CHECK(spacer1->outputs.empty());
    CHECK(spacer1->bodies.empty()) << "Expected that a spacer "
      "is empty, although it would not be wrong to define it "
      "to have some geometry in there.";
  }

}


int main(int argc, char **argv) {
  ANSI::Init();

  Simple();

  Print("OK\n");
  return 0;
}
