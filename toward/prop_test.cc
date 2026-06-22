
#include "prop.h"

#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"

static void TestValue() {
  std::vector<bool> assignments;

  {
    Prop p = True();
    CHECK(EvaluateProp(assignments, p));
  }
  {
    Prop p = False();
    CHECK(!EvaluateProp(assignments, p));
  }
}

static void TestVar() {
  std::vector<bool> assignments = {true, false};

  {
    Prop p = Prop{.p = Var{.id = 0}};
    CHECK(EvaluateProp(assignments, p));
  }
  {
    Prop p = Prop{.p = Var{.id = 1}};
    CHECK(!EvaluateProp(assignments, p));
  }
}

static void TestUnop() {
  std::vector<bool> assignments;
  {
    Prop p = -True();
    CHECK(!EvaluateProp(assignments, p));
  }
  {
    Prop p = -False();
    CHECK(EvaluateProp(assignments, p));
  }
}

static void TestBinop() {
  std::vector<bool> assignments;
  {
    Prop p = True() & True();
    CHECK(EvaluateProp(assignments, p));
  }
  {
    Prop p = True() & False();
    CHECK(!EvaluateProp(assignments, p));
  }
  {
    Prop p = True() | False();
    CHECK(EvaluateProp(assignments, p));
  }
  {
    Prop p = False() | False();
    CHECK(!EvaluateProp(assignments, p));
  }
  {
    Prop p = True() ^ False();
    CHECK(EvaluateProp(assignments, p));
  }
  {
    Prop p = True() ^ True();
    CHECK(!EvaluateProp(assignments, p));
  }
}

static void TestVariadicOr() {
  std::vector<bool> assignments;
  {
    Prop p = Or();
    CHECK(!EvaluateProp(assignments, p));
  }
  {
    Prop p = Or(False(), False(), True());
    CHECK(EvaluateProp(assignments, p));
  }
}

static void TestVariadicAnd() {
  std::vector<bool> assignments;
  {
    Prop p = And();
    CHECK(EvaluateProp(assignments, p));
  }
  {
    Prop p = And(True(), True(), False());
    CHECK(!EvaluateProp(assignments, p));
  }
}

static void TestPropVars() {
  Prop p0 = Prop{.p = Var{.id = 0}};
  Prop p1 = Prop{.p = Var{.id = 1}};
  Prop p2 = Prop{.p = Var{.id = 2}};

  {
    Prop p = True();
    CHECK(PropVars(p).empty());
  }
  {
    Prop p = p0 | p1;
    std::vector<int> vars = PropVars(p);
    CHECK(vars.size() == 2 && vars[0] == 0 && vars[1] == 1);
  }
  {
    Prop p = p2 | (p0 & p1);
    std::vector<int> vars = PropVars(p);
    CHECK(vars.size() == 3 && vars[0] == 0 && vars[1] == 1 && vars[2] == 2);
  }
  {
    Prop p = p1 & p1;
    std::vector<int> vars = PropVars(p);
    CHECK(vars.size() == 1 && vars[0] == 1);
  }
}

static void TestPropEq() {
  Prop p0 = Prop{.p = Var{.id = 0}};
  Prop p1 = Prop{.p = Var{.id = 1}};
  Prop p2 = Prop{.p = Var{.id = 2}};
  Prop p5 = Prop{.p = Var{.id = 5}};
  Prop p10 = Prop{.p = Var{.id = 10}};

  CHECK(PropEq(True(), True()));
  CHECK(!PropEq(True(), False()));

  CHECK(PropEq(p0, p0));
  CHECK(!PropEq(p0, p1));

  CHECK(PropEq(p0 | p1, p1 | p0));
  CHECK(PropEq(p0 & p1, p1 & p0));

  // De Morgan's laws
  CHECK(PropEq(-(p0 | p1), -p0 & -p1));
  CHECK(PropEq(-(p0 & p1), -p0 | -p1));

  // Double negation
  CHECK(PropEq(-(-p0), p0));

  // Test cases where we have gaps in the variable assignments.
  Prop a = p5 | (p2 & -p2);
  Prop b = p5 | (p10 & -p10);
  CHECK(PropEq(a, b));
  CHECK(PropEq(p5, a));
  CHECK(PropEq(b, p5));

  Prop c = p5 & (p2 | -p2);
  Prop d = p5 & (p10 | -p10);
  CHECK(PropEq(c, d));
  CHECK(PropEq(p5, c));
  CHECK(PropEq(d, p5));

  // Sanity check that they aren't just always returning true.
  CHECK(!PropEq(a, p2));
  CHECK(!PropEq(b, p10));
}

int main(int argc, char **argv) {
  ANSI::Init();

  TestValue();
  TestVar();
  TestUnop();
  TestBinop();
  TestVariadicOr();
  TestVariadicAnd();
  TestPropVars();
  TestPropEq();

  Print("OK\n");
  return 0;
}
