
#include "prop.h"

#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"

static void TestValue() {
  World world;
  std::vector<bool> assignments;

  {
    Prop p = True();
    CHECK(EvaluateProp(world, assignments, p));
  }
  {
    Prop p = False();
    CHECK(!EvaluateProp(world, assignments, p));
  }
}

static void TestVar() {
  World world;
  world.symbol_names = {"A", "B"};
  std::vector<bool> assignments = {true, false};

  {
    Prop p = Prop{.p = Var{.id = 0}};
    CHECK(EvaluateProp(world, assignments, p));
  }
  {
    Prop p = Prop{.p = Var{.id = 1}};
    CHECK(!EvaluateProp(world, assignments, p));
  }
}

static void TestUnop() {
  World world;
  std::vector<bool> assignments;

  {
    Prop p = -True();
    CHECK(!EvaluateProp(world, assignments, p));
  }
  {
    Prop p = -False();
    CHECK(EvaluateProp(world, assignments, p));
  }
}

static void TestBinop() {
  World world;
  std::vector<bool> assignments;

  {
    Prop p = True() & True();
    CHECK(EvaluateProp(world, assignments, p));
  }
  {
    Prop p = True() & False();
    CHECK(!EvaluateProp(world, assignments, p));
  }
  {
    Prop p = True() | False();
    CHECK(EvaluateProp(world, assignments, p));
  }
  {
    Prop p = False() | False();
    CHECK(!EvaluateProp(world, assignments, p));
  }
  {
    Prop p = True() ^ False();
    CHECK(EvaluateProp(world, assignments, p));
  }
  {
    Prop p = True() ^ True();
    CHECK(!EvaluateProp(world, assignments, p));
  }
}

static void TestVariadicOr() {
  World world;
  std::vector<bool> assignments;

  {
    Prop p = Or();
    CHECK(!EvaluateProp(world, assignments, p));
  }
  {
    Prop p = Or(False(), False(), True());
    CHECK(EvaluateProp(world, assignments, p));
  }
}

static void TestVariadicAnd() {
  World world;
  std::vector<bool> assignments;

  {
    Prop p = And();
    CHECK(EvaluateProp(world, assignments, p));
  }
  {
    Prop p = And(True(), True(), False());
    CHECK(!EvaluateProp(world, assignments, p));
  }
}

int main(int argc, char **argv) {
  ANSI::Init();

  TestValue();
  TestVar();
  TestUnop();
  TestBinop();
  TestVariadicOr();
  TestVariadicAnd();

  Print("OK\n");
  return 0;
}
