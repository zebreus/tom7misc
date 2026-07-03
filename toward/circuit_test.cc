
#include "circuit.h"

#include <optional>
#include <string>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "prop.h"

static void TestTransformConst() {
  Layer layer = {Cell(CONST0), Cell(CONST1)};
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
    Cell(SPACER, 24),
    Cell(SINK),
    Cell(WIREA, 8, true),
    Cell(SEPARATOR01, 15),
    Cell(NOT),
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
    Cell(AND0110),
    Cell(DUPSEP0011),
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
    Cell(XCHG00),
    Cell(XCHG01),
    Cell(XCHG10),
    Cell(XCHG11),
  };
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
    Cell(SEPARATOR01, 0, true),
    Cell(DUPSEP0011, 0, true),
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

static void TestSerialization() {
  Circuit circuit;
  circuit.layers.push_back({
      Cell(SPACER, 24),
      Cell(SINK),
      Cell(SPACER, 3),
      Cell(WIREA, 8, true),
      Cell(SEPARATOR01, 15),
      Cell(NOT),
    });
  circuit.layers.push_back({
      Cell(AND0110),
      Cell(SPACER, 1),
      Cell(DUPSEP0011, 0, true),
      Cell(SPACER, 5),
    });
  circuit.layers.push_back({
      Cell(XCHG00),
      Cell(XCHG01),
      Cell(XCHG10),
      Cell(XCHG11),
    });

  std::string s = SerializeCircuit(circuit);
  std::optional<Circuit> parsed = ParseCircuit(s);
  CHECK(parsed.has_value());

  auto CircuitEq = [](const Circuit &a, const Circuit &b) {
    if (a.layers.size() != b.layers.size()) return false;
    for (size_t i = 0; i < a.layers.size(); i++) {
      if (a.layers[i].size() != b.layers[i].size()) return false;
      for (size_t j = 0; j < a.layers[i].size(); j++) {
        const Cell &ca = a.layers[i][j];
        const Cell &cb = b.layers[i][j];
        CHECK(ca == cb);
      }
    }
    return true;
  };

  CHECK(CircuitEq(circuit, parsed.value()));
}

int main(int argc, char **argv) {
  ANSI::Init();

  TestTransformConst();
  TestTransform1();
  TestTransform2();
  TestTransformXchg();
  TestTransformFlip();
  TestSerialization();

  Print("OK\n");
  return 0;
}
