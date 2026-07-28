
#include "minitable.h"

#include <cstdint>
#include <optional>
#include <tuple>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "prop.h"

static void TestFunctionallyComplete() {
  MiniTable mt(MiniTable::OPT_AND | MiniTable::OPT_NOT | MiniTable::OPT_OR |
               MiniTable::OPT_XOR);

  // Spot check to keep the test fast.
  for (int i = 0; i < 65536; i += 257) {
    uint16_t fn = (uint16_t)i;
    Prop p = mt.Minimal(0, 1, 2, 3, fn);
    uint16_t eval = MiniTable::Eval(p);
    CHECK(eval == fn) << "Eval should match requested fn.";
  }
}

static void TestGetQuadReconstruction() {
  MiniTable mt(MiniTable::OPT_AND | MiniTable::OPT_NOT | MiniTable::OPT_OR);

  // Pick a truth table that depends on multiple variables.
  uint16_t fn = 0xAA55;
  Prop p = mt.Minimal(2, 4, 6, 8, fn);

  std::optional<std::tuple<int, int, int, int, uint16_t>> quad =
      MiniTable::GetQuad(p);
  CHECK(quad.has_value()) << "GetQuad should succeed on minimal prop.";

  auto [v0, v1, v2, v3, tt] = quad.value();

  Prop reconstructed = mt.Minimal(v0, v1, v2, v3, tt);
  std::optional<std::tuple<int, int, int, int, uint16_t>> quad2 =
      MiniTable::GetQuad(reconstructed);

  CHECK(quad2.has_value()) << "Reconstructed should succeed.";

  auto [rv0, rv1, rv2, rv3, rtt] = quad2.value();
  CHECK(rtt == tt) << "Truth table should be preserved under reconstruction.";
}

static void TestConstantProperties() {
  MiniTable mt(MiniTable::OPT_AND | MiniTable::OPT_NOT | MiniTable::OPT_OR);

  // A truth table that evaluates to True everywhere.
  uint16_t fn = 0xFFFF;
  Prop p = mt.Minimal(10, 11, 12, 13, fn);

  std::optional<std::tuple<int, int, int, int, uint16_t>> quad =
      MiniTable::GetQuad(p);
  CHECK(quad.has_value()) << "GetQuad should succeed.";

  auto [v0, v1, v2, v3, tt] = quad.value();
  CHECK(tt == 0xFFFF) << "Constant function should retain its truth table.";
}

int main(int argc, char **argv) {
  ANSI::Init();

  TestFunctionallyComplete();
  TestGetQuadReconstruction();
  TestConstantProperties();

  Print("OK\n");
  return 0;
}

