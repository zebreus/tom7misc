#include "drc.h"

#include <optional>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "cell-library.h"
#include "circuit.h"
#include "layout.h"

static void TestEmptyCircuit(const CellLibrary &library) {
  Circuit circuit;
  CHECK(
      !DRC::GetCircuitError(library, "TestEmptyCircuit", circuit).has_value());
}

static void TestCircuitMismatchedLayers(const CellLibrary &library) {
  {
    Circuit circuit;
    circuit.layers.push_back({Cell(Gate::WIREA), Cell(Gate::WIREA)});
    circuit.layers.push_back({Cell(Gate::WIREA)});
    CHECK(DRC::GetCircuitError(library, "TestCircuitMismatchedLayers", circuit)
              .has_value());
  }
}

static void TestLargeWireMustUseAShape(const CellLibrary &library) {
  {
    Circuit circuit;
    circuit.layers.push_back({Cell(Gate::WIREB, CellLibrary::SMALL_WIRE)});
    CHECK(DRC::GetCircuitError(library, "TestLargeWireMustUseAShape", circuit)
              .has_value());
  }
}

static void TestEmptyLayout(const CellLibrary &library) {
  Layout layout;
  CHECK(!DRC::GetLayoutError(library, "TestEmptyLayout", layout).has_value());
}

static void TestLayoutInputMismatch(const CellLibrary &library) {
  {
    Layout layout;
    layout.circuit.layers.push_back({Cell(Gate::WIREA)});
    // First layer has an input (since it's a wire), but input_vars is empty.
    CHECK(DRC::GetLayoutError(library, "TestLayoutInputMismatch", layout)
              .has_value());
  }
}

static void TestEquivalentLayouts(const CellLibrary &library) {
  {
    Layout a;
    Layout b;
    // Two empty layouts should be equivalently empty.
    DRC::AssertEquivalentLayout(library, "TestEquivalentLayouts", a, b);
  }
}

int main(int argc, char **argv) {
  ANSI::Init();

  CellLibrary library;
  TestEmptyCircuit(library);
  TestCircuitMismatchedLayers(library);
  TestLargeWireMustUseAShape(library);
  TestEmptyLayout(library);
  TestLayoutInputMismatch(library);
  TestEquivalentLayouts(library);

  Print("OK\n");
  return 0;
}
