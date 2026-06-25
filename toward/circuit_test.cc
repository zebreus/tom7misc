
#include "circuit.h"

#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "prop.h"

static void TestTransformConst() {
  Layer layer = {Cell{.gate = CONST0}, Cell{.gate = CONST1}};
  std::vector<Func> in = {};
  std::vector<Func> out = Transform(layer, in);
  CHECK(out.size() == 2);
  CHECK(out[0].type == CType::MIXED);
  CHECK(out[1].type == CType::MIXED);
  CHECK(PropEq(out[0].prop, False()));
  CHECK(PropEq(out[1].prop, True()));
}

// Sink, wire, separator, not
static void TestTransform1() {
  Prop p0 = Prop{.p = Var{.id = 0}};
  Prop p1 = Prop{.p = Var{.id = 1}};
  Prop p2 = Prop{.p = Var{.id = 2}};
  Prop p3 = Prop{.p = Var{.id = 3}};

  Layer layer = {
    Cell{.gate = SPACER, .v = 24},
    Cell{.gate = SINK},
    Cell{.gate = WIRE, .v = -8},
    Cell{.gate = SEPARATOR, .v = 15},
    Cell{.gate = NOT}
  };
  std::vector<Func> in = {
    // Sink
    Func{.prop = p0, .type = CType::MIXED},
    // Wire
    Func{.prop = p1, .type = CType::MIXED},
    // Separator
    Func{.prop = p2, .type = CType::MIXED},
    // Not
    Func{.prop = p3, .type = CType::MIXED},
  };

  std::vector<Func> out = Transform(layer, in);
  CHECK(out.size() == 4);
  CHECK(out[0].type == CType::MIXED);
  CHECK(PropEq(out[0].prop, p1));
  CHECK(out[1].type == CType::ZERO);
  CHECK(PropEq(out[1].prop, p2));
  CHECK(out[2].type == CType::ONE);
  CHECK(PropEq(out[2].prop, p2));
  CHECK(out[3].type == CType::MIXED);
  CHECK(PropEq(out[3].prop, -p3));
}

static void TestTransform2() {
  Prop pa = Prop{.p = Var{.id = 0}};
  Prop pb = Prop{.p = Var{.id = 1}};
  Prop pc = Prop{.p = Var{.id = 2}};

  Layer layer = {
    Cell{.gate = AND0110},
    Cell{.gate = DUPSEP0011},
  };
  std::vector<Func> in = {
    Func{.prop = pa, .type = CType::ZERO},
    Func{.prop = pa, .type = CType::ONE},
    Func{.prop = pb, .type = CType::ONE},
    Func{.prop = pb, .type = CType::ZERO},
    Func{.prop = pc, .type = CType::MIXED},
  };
  std::vector<Func> out = Transform(layer, in);
  CHECK(out.size() == 5);
  CHECK(out[0].type == CType::MIXED);
  CHECK(PropEq(out[0].prop, pa & pb));

  CHECK(out[1].type == CType::ZERO);
  CHECK(out[2].type == CType::ZERO);
  CHECK(out[3].type == CType::ONE);
  CHECK(out[4].type == CType::ONE);
  for (int i = 1; i <= 4; i++) {
    CHECK(PropEq(out[i].prop, pc));
  }
}

static void TestTransformXchg() {
  Prop p0 = Prop{.p = Var{.id = 0}};
  Prop p1 = Prop{.p = Var{.id = 1}};
  Prop p2 = Prop{.p = Var{.id = 2}};
  Prop p3 = Prop{.p = Var{.id = 3}};
  Prop p4 = Prop{.p = Var{.id = 4}};
  Prop p5 = Prop{.p = Var{.id = 5}};
  Prop p6 = Prop{.p = Var{.id = 6}};
  Prop p7 = Prop{.p = Var{.id = 7}};

  Layer layer = {
    Cell{.gate = XCHG00},
    Cell{.gate = XCHG01},
    Cell{.gate = XCHG10},
    Cell{.gate = XCHG11}};
  std::vector<Func> in = {
    Func{.prop = p0, .type = CType::ZERO},
    Func{.prop = p1, .type = CType::ZERO},
    Func{.prop = p2, .type = CType::ZERO},
    Func{.prop = p3, .type = CType::ONE},
    Func{.prop = p4, .type = CType::ONE},
    Func{.prop = p5, .type = CType::ZERO},
    Func{.prop = p6, .type = CType::ONE},
    Func{.prop = p7, .type = CType::ONE},
  };
  std::vector<Func> out = Transform(layer, in);
  CHECK(out.size() == 8);

  CHECK(out[0].type == CType::ZERO);
  CHECK(PropEq(out[0].prop, p1));
  CHECK(out[1].type == CType::ZERO);
  CHECK(PropEq(out[1].prop, p0));

  CHECK(out[2].type == CType::ONE);
  CHECK(PropEq(out[2].prop, p3));
  CHECK(out[3].type == CType::ZERO);
  CHECK(PropEq(out[3].prop, p2));

  CHECK(out[4].type == CType::ZERO);
  CHECK(PropEq(out[4].prop, p5));
  CHECK(out[5].type == CType::ONE);
  CHECK(PropEq(out[5].prop, p4));

  CHECK(out[6].type == CType::ONE);
  CHECK(PropEq(out[6].prop, p7));
  CHECK(out[7].type == CType::ONE);
  CHECK(PropEq(out[7].prop, p6));
}

static void TestTransformFlip() {
  World world;

  Prop p0 = Prop{.p = Var{.id = 0}};
  Prop p1 = Prop{.p = Var{.id = 1}};

  Layer layer = {
    Cell{.gate = SEPARATOR, .flip = true},
    Cell{.gate = DUPSEP0011, .flip = true},
  };
  std::vector<Func> in = {
    Func{.prop = p0, .type = CType::MIXED},
    Func{.prop = p1, .type = CType::MIXED},
  };
  std::vector<Func> out = Transform(layer, in);
  CHECK(out.size() == 6);
  CHECK(out[0].type == CType::ONE);
  CHECK(out[1].type == CType::ZERO);
  CHECK(out[2].type == CType::ONE);
  CHECK(out[3].type == CType::ONE);
  CHECK(out[4].type == CType::ZERO);
  CHECK(out[5].type == CType::ZERO);

  CHECK(PropEq(out[0].prop, p0));
  CHECK(PropEq(out[1].prop, p0));
  for (int i = 2; i <= 5; i++) {
    CHECK(PropEq(out[i].prop, p1));
  }
}

int main(int argc, char **argv) {
  ANSI::Init();

  TestTransformConst();
  TestTransform1();
  TestTransform2();
  TestTransformXchg();
  TestTransformFlip();

  Print("OK\n");
  return 0;
}
