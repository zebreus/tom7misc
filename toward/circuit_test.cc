
#include "circuit.h"

#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "prop.h"

static void TestTransformConst() {
  World world;
  std::vector<bool> assignments;

  Layer layer = {Cell{.gate = CONST0}, Cell{.gate = CONST1}};
  std::vector<Func> in = {};
  std::vector<Func> out = Transform(layer, in);
  CHECK(out.size() == 2);
  CHECK(out[0].type == CType::MIXED);
  CHECK(out[1].type == CType::MIXED);
  CHECK(!EvaluateProp(world, assignments, out[0].prop));
  CHECK(EvaluateProp(world, assignments, out[1].prop));
}

static void TestTransformSpacerSinkWireSepNot() {
  World world;
  std::vector<bool> assignments;

  Layer layer = {Cell{.gate = SPACER}, Cell{.gate = SINK}, Cell{.gate = WIRE},
                 Cell{.gate = SEPARATOR}, Cell{.gate = NOT}};
  std::vector<Func> in = {
    Func{.prop = True(), .type = CType::MIXED},   // SINK
    Func{.prop = False(), .type = CType::MIXED},  // WIRE
    Func{.prop = True(), .type = CType::MIXED},   // SEPARATOR
    Func{.prop = True(), .type = CType::MIXED},   // NOT
  };
  std::vector<Func> out = Transform(layer, in);
  CHECK(out.size() == 4);
  CHECK(out[0].type == CType::MIXED);
  CHECK(!EvaluateProp(world, assignments, out[0].prop));
  CHECK(out[1].type == CType::ZERO);
  CHECK(EvaluateProp(world, assignments, out[1].prop));
  CHECK(out[2].type == CType::ONE);
  CHECK(EvaluateProp(world, assignments, out[2].prop));
  CHECK(out[3].type == CType::MIXED);
  CHECK(!EvaluateProp(world, assignments, out[3].prop));
}

static void TestTransformAndDup() {
  World world;
  std::vector<bool> assignments;

  Layer layer = {Cell{.gate = AND0110}, Cell{.gate = DUPSEP0011}};
  std::vector<Func> in = {
    Func{.prop = False(), .type = CType::ZERO},
    Func{.prop = True(), .type = CType::ONE},
    Func{.prop = True(), .type = CType::ONE},
    Func{.prop = False(), .type = CType::ZERO},
    Func{.prop = True(), .type = CType::MIXED},
  };
  std::vector<Func> out = Transform(layer, in);
  CHECK(out.size() == 5);
  CHECK(out[0].type == CType::MIXED);
  CHECK(!EvaluateProp(world, assignments, out[0].prop));

  CHECK(out[1].type == CType::ZERO);
  CHECK(out[2].type == CType::ZERO);
  CHECK(out[3].type == CType::ONE);
  CHECK(out[4].type == CType::ONE);
  for (int i = 1; i <= 4; i++) {
    CHECK(EvaluateProp(world, assignments, out[i].prop));
  }
}

static void TestTransformXchg() {
  World world;
  std::vector<bool> assignments;

  Layer layer = {Cell{.gate = XCHG00}, Cell{.gate = XCHG01},
                 Cell{.gate = XCHG10}, Cell{.gate = XCHG11}};
  std::vector<Func> in = {
    Func{.prop = True(), .type = CType::ZERO},
    Func{.prop = False(), .type = CType::ZERO},
    Func{.prop = True(), .type = CType::ZERO},
    Func{.prop = False(), .type = CType::ONE},
    Func{.prop = False(), .type = CType::ONE},
    Func{.prop = True(), .type = CType::ZERO},
    Func{.prop = False(), .type = CType::ONE},
    Func{.prop = True(), .type = CType::ONE},
  };
  std::vector<Func> out = Transform(layer, in);
  CHECK(out.size() == 8);

  CHECK(out[0].type == CType::ZERO);
  CHECK(!EvaluateProp(world, assignments, out[0].prop));
  CHECK(out[1].type == CType::ZERO);
  CHECK(EvaluateProp(world, assignments, out[1].prop));

  CHECK(out[2].type == CType::ONE);
  CHECK(!EvaluateProp(world, assignments, out[2].prop));
  CHECK(out[3].type == CType::ZERO);
  CHECK(EvaluateProp(world, assignments, out[3].prop));

  CHECK(out[4].type == CType::ZERO);
  CHECK(EvaluateProp(world, assignments, out[4].prop));
  CHECK(out[5].type == CType::ONE);
  CHECK(!EvaluateProp(world, assignments, out[5].prop));

  CHECK(out[6].type == CType::ONE);
  CHECK(EvaluateProp(world, assignments, out[6].prop));
  CHECK(out[7].type == CType::ONE);
  CHECK(!EvaluateProp(world, assignments, out[7].prop));
}

static void TestTransformFlip() {
  World world;
  std::vector<bool> assignments;

  Layer layer = {Cell{.gate = SEPARATOR, .flip = true},
                 Cell{.gate = DUPSEP0011, .flip = true}};
  std::vector<Func> in = {
    Func{.prop = True(), .type = CType::MIXED},
    Func{.prop = False(), .type = CType::MIXED},
  };
  std::vector<Func> out = Transform(layer, in);
  CHECK(out.size() == 6);
  CHECK(out[0].type == CType::ONE);
  CHECK(out[1].type == CType::ZERO);
  CHECK(out[2].type == CType::ONE);
  CHECK(out[3].type == CType::ONE);
  CHECK(out[4].type == CType::ZERO);
  CHECK(out[5].type == CType::ZERO);

  CHECK(EvaluateProp(world, assignments, out[0].prop));
  CHECK(EvaluateProp(world, assignments, out[1].prop));
  for (int i = 2; i <= 5; i++) {
    CHECK(!EvaluateProp(world, assignments, out[i].prop));
  }
}

int main(int argc, char **argv) {
  ANSI::Init();

  TestTransformConst();
  TestTransformSpacerSinkWireSepNot();
  TestTransformAndDup();
  TestTransformXchg();
  TestTransformFlip();

  Print("OK\n");
  return 0;
}
