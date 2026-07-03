
#include "layout.h"

#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "cell-library.h"
#include "circuit.h"
#include "prop.h"
#include "render-circuit.h"

static void StartTest(std::string_view name) {
  Print("\n\n" ABGCOLOR(0, 0, 160, "== {} ==") "\n", name);
}

static void Verify(const Layout &layout, const std::vector<Prop> &props) {
  std::vector<Func> funcs;
  for (auto [v, t] : layout.input_vars) {
    funcs.push_back(Func{.prop = Prop{Var{.id = v}}, .type = t});
  }
  for (const Layer &layer : layout.circuit.layers) {
    funcs = Transform(layer, funcs);
  }
  CHECK(funcs.size() == props.size());
  for (size_t i = 0; i < funcs.size(); i++) {
    CHECK(funcs[i].type == CType::MIXED);
    CHECK(PropEq(funcs[i].prop, props[i]));
  }
}

static void Empty(const CellLibrary &library) {
  World world;
  std::unique_ptr<LayoutEngine> le = LayoutEngine::Create(library, world);
  std::vector<Prop> nothing;
  Layout trivial = le->DoLayout(nothing);
  CHECK(trivial.input_vars.empty());
  // ... should be one empty layer in this case? ...
  Verify(trivial, nothing);
}

static void Consts(const CellLibrary &library) {
  StartTest("Consts");
  World world;
  std::unique_ptr<LayoutEngine> le = LayoutEngine::Create(library, world);
  std::vector<Prop> output = {True(), False()};
  Layout layout = le->DoLayout(output);
  library.DRC(layout.circuit);
  Verify(layout, output);
}

static void SingleVar(const CellLibrary &library) {
  StartTest("Single Var");
  World world{.symbol_names = {"a"}};
  std::unique_ptr<LayoutEngine> le = LayoutEngine::Create(library, world);
  Prop a{Var{.id = 0}};

  std::vector<Prop> output = {a};
  Layout layout = le->DoLayout(output);
  library.DRC(layout.circuit);
  Verify(layout, output);
}

static void NotVar(const CellLibrary &library) {
  StartTest("Not Var ");
  World world{.symbol_names = {"a"}};
  std::unique_ptr<LayoutEngine> le = LayoutEngine::Create(library, world);
  Prop a{Var{.id = 0}};

  std::vector<Prop> output = {-a};
  Layout layout = le->DoLayout(output);
  library.DRC(layout.circuit);
  Verify(layout, output);
}

static void AndVars(const CellLibrary &library) {
  StartTest("And Vars ");
  World world{.symbol_names = {"a", "b", "c", "d"}};
  std::unique_ptr<LayoutEngine> le = LayoutEngine::Create(library, world);
  Prop a{Var{.id = 0}}, b{Var{.id = 1}}, c{Var{.id = 2}}, d{Var{.id = 3}};

  std::vector<Prop> output = {a & b};
  Layout layout = le->DoLayout(output);
  library.DRC(layout.circuit);
  Verify(layout, output);
}

static void OrVars(const CellLibrary &library) {
  StartTest("Or Vars");
  World world{.symbol_names = {"a", "b"}};
  std::unique_ptr<LayoutEngine> le = LayoutEngine::Create(library, world);
  Prop a{Var{.id = 0}}, b{Var{.id = 1}};

  std::vector<Prop> output = {a | b};
  Layout layout = le->DoLayout(output);
  library.DRC(layout.circuit);
  Verify(layout, output);
}

static void XorVars(const CellLibrary &library) {
  StartTest("Xor Vars");
  World world{.symbol_names = {"a", "b"}};
  std::unique_ptr<LayoutEngine> le = LayoutEngine::Create(library, world);
  Prop a{Var{.id = 0}}, b{Var{.id = 1}};

  std::vector<Prop> output = {a ^ b};
  Layout layout = le->DoLayout(output);
  library.DRC(layout.circuit);
  Verify(layout, output);
}

static void MultiOutput(const CellLibrary &library) {
  StartTest("Multi Output");
  World world{.symbol_names = {"a", "b", "c"}};
  Prop a{Var{.id = 0}}, b{Var{.id = 1}}, c{Var{.id = 2}};

  std::vector<Prop> output = {a & b, b | c, c ^ a, -a};
  std::unique_ptr<LayoutEngine> le = LayoutEngine::Create(library, world);
  le->SetVerbose(2);
  Layout layout = le->DoLayout(output);
  library.DRC(layout.circuit);
  Verify(layout, output);
}

static void TestLoop1() {
  CellLibrary library;
  World world;
  std::unique_ptr<LayoutEngine> le = LayoutEngine::Create(library, world);
  le->SetVerbose(0);

  /*
 [40] Chute(pos=1542, prop=v733, type=ONE)
 [41] Chute(pos=1563, prop=v733, type=ZERO)
 [42] Chute(pos=3070, prop=¬v629 ⋀ ¬v655, type=ONE)
 [43] Chute(pos=3091, prop=¬v629 ⋀ ¬v655, type=ZERO)
 [44] Chute(pos=3105, prop=v752, type=ZERO)
 [45] Chute(pos=5935, prop=v746, type=ONE)
 [46] Chute(pos=5969, prop=v752, type=ONE)
  */

  Prop v629{Var{.id = 629}};
  Prop v655{Var{.id = 655}};
  Prop v733{Var{.id = 733}};
  Prop v746{Var{.id = 746}};
  Prop v752{Var{.id = 752}};

  struct ChuteDesc {
    int pos;
    Prop prop;
    CType type;
  };

  std::vector<ChuteDesc> chutes = {
      {1542, v733, CType::ONE},
      {1563, v733, CType::ZERO},
      {3070, -v629 & -v655, CType::ONE},
      {3091, -v629 & -v655, CType::ZERO},
      {3105, v752, CType::ZERO},
      {5935, v746, CType::ONE},
      {5969, v752, CType::ONE},
  };

  std::vector<LayoutEngine::LC> top_layer;
  int current_x = 0;

  for (const ChuteDesc &desc : chutes) {
    Cell cell = CellLibrary::WireA(0, desc.type);
    int in_x = library.GetInfo(cell).inputs[0].xblock;
    int cell_x = desc.pos - in_x;

    if (cell_x > current_x) {
      top_layer.push_back(LayoutEngine::LC{
          .inprops = {},
          .cell = CellLibrary::Spacer(cell_x - current_x),
      });
      current_x = cell_x;
    }
    CHECK(cell_x == current_x) << "Overlap in test case construction!";
    top_layer.push_back(LayoutEngine::LC{
        .inprops = {desc.prop},
        .cell = cell,
    });
    current_x += library.GetInfo(cell).block_width;
  }

  std::deque<std::vector<LayoutEngine::LC>> layers;
  layers.push_back(std::move(top_layer));

  for (int i = 0; i < 16; i++) {
    CHECK(!layers.empty());
    if (le->AllVars(layers.front()).has_value()) {
      Print("Got all vars!\n");
      break;
    }
    le->DoAddLayer(&layers);
  }

}

static void Modest(const CellLibrary &library) {
  StartTest("Modest");
  World world{.symbol_names = {"a", "b", "c", "d", "e", "f", "g", "h"}};
  std::unique_ptr<LayoutEngine> le = LayoutEngine::Create(library, world);
  Prop a{Var{.id = 0}}, b{Var{.id = 1}}, c{Var{.id = 2}}, d{Var{.id = 3}},
    e{Var{.id = 4}}, f{Var{.id = 5}}, g{Var{.id = 6}}, h{Var{.id = 7}};

  std::vector<Prop> output = {
    BalanceProp(
        (-e & -d & (f | c)) |
        (g & h & a & b & c) |
        (f & ((b & d & a) | (a & -b))) |
        ((-f) & (-(a & -d & (b | c))))
                ),
  };
  le->SetVerbose(2);
  le->SetWriteImages(true);
  Layout layout = le->DoLayout(output);
  RenderCircuit(library, layout.circuit).Save("modest.png");
  library.DRC(layout.circuit);
  Verify(layout, output);
}

static void TestSerialization() {
  StartTest("Serialization");

  {
    Layout layout;
    layout.input_vars = {
      {10, CType::MIXED},
      {20, CType::ZERO},
      {30, CType::ONE},
    };
    // Add a dummy layer to make sure it doesn't break basic circuit
    // serialization.
    layout.circuit.layers.push_back({
        Cell(SPACER, 42),
        Cell(WIRE0A, 8, true),
      });

    std::string s = LayoutEngine::Serialize(layout);
    std::optional<Layout> parsed = LayoutEngine::Parse(s);

    CHECK(parsed.has_value());
    CHECK(parsed->input_vars == layout.input_vars);
    CHECK(parsed->circuit.layers == layout.circuit.layers);
  }

  {
    Layout layout;
    std::string s = LayoutEngine::Serialize(layout);
    std::optional<Layout> parsed = LayoutEngine::Parse(s);

    CHECK(parsed.has_value());
    CHECK(parsed->input_vars.empty());
    CHECK(parsed->circuit.layers.empty());
  }
}


int main(int argc, char **argv) {
  ANSI::Init();

  CellLibrary library;

  TestSerialization();

  TestLoop1();

  // Tests of the full layout algorithm.
  Empty(library);
  Consts(library);
  SingleVar(library);
  NotVar(library);
  AndVars(library);
  OrVars(library);
  XorVars(library);

  MultiOutput(library);

  // took 190 layers!
  Modest(library);

  Print("OK\n");
  return 0;
}
