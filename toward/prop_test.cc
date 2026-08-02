
#include "prop.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "interesting-props.h"

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

static void TestNand() {
  std::vector<bool> assignments;
  {
    Prop p = Nand(True(), True());
    CHECK(!EvaluateProp(assignments, p));
  }
  {
    Prop p = Nand(True(), False());
    CHECK(EvaluateProp(assignments, p));
  }
  {
    Prop p = Nand(False(), True());
    CHECK(EvaluateProp(assignments, p));
  }
  {
    Prop p = Nand(False(), False());
    CHECK(EvaluateProp(assignments, p));
  }

  Prop p0 = Prop{.p = Var{.id = 0}};
  Prop p1 = Prop{.p = Var{.id = 1}};
  CHECK(PropEq(Nand(p0, p1), -(p0 & p1)));
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

static void TestPropSize() {
  Prop p0 = Prop{.p = Var{.id = 0}};
  Prop p1 = Prop{.p = Var{.id = 1}};

  CHECK(PropSize(True()) == 1);
  CHECK(SharedPropSize(True()) == 1);

  Prop p = p0 | (p0 & p1);
  CHECK(PropSize(p) == 5);
  CHECK(SharedPropSize(p) == 4);

  Prop and_tree = (p0 & p1) & (p0 & p1);
  CHECK(PropSize(and_tree) == 7);
  CHECK(SharedPropSize(and_tree) == 4);

  Prop not_tree = -(-p0);
  CHECK(PropSize(not_tree) == 3);
  CHECK(SharedPropSize(not_tree) == 3);
}

static bool IsAndNormalForm(const Prop &prop) {
  std::vector<const Prop *> v = {&prop};
  while (!v.empty()) {
    const Prop *p = v.back();
    v.pop_back();

    if (std::holds_alternative<Value>(p->p) ||
        std::holds_alternative<Var>(p->p)) {
      continue;
    } else if (const Unop *u = std::get_if<Unop>(&p->p)) {
      CHECK(u->op == UnopOp::NOT);
      v.push_back(u->a.get());
    } else if (const Binop *b = std::get_if<Binop>(&p->p)) {
      if (b->op != BinopOp::AND) return false;
      v.push_back(b->a.get());
      v.push_back(b->b.get());
    } else {
      LOG(FATAL) << "Bad variant?";
    }
  }
  return true;
}

static std::vector<Prop> TestProps() {
  std::vector<Prop> props;
  for (const Prop &p : SmallInterestingProps()) props.push_back(p);
  for (const Prop &p : MediumInterestingProps()) props.push_back(p);
  return props;
}

static void TestSimplifyProp() {
  for (const Prop &p : TestProps()) {
    Prop simp = SimplifyProp(p);
    CHECK(PropEq(p, simp)) << PropString(p) << "\n" << PropString(simp);
  }
}

static void TestBalanceProp() {
  for (const Prop &p : TestProps()) {
    Prop bal = BalanceProp(p);
    CHECK(PropEq(p, bal)) << PropString(p) << "\n" << PropString(bal);
  }
}

static void TestNormalizeToAnd() {
  for (const Prop &p : TestProps()) {
    Prop norm = NormalizeToAnd(p);
    CHECK(PropEq(p, norm)) << PropString(p) << "\n" << PropString(norm);
    CHECK(IsAndNormalForm(norm)) << PropString(p) << "\n"
                                 << PropString(norm);
  }
}

static void TestSerializeParse() {
  for (const Prop &p : TestProps()) {
    std::string s = SerializeProp(p);
    std::optional<Prop> parsed = ParseProp(s);
    CHECK(parsed.has_value()) << "Failed to parse: " << s;
    CHECK(*parsed == p) << "Round trip failed for: " << s;
  }
}

static void TestUnorderedMap() {
  std::unordered_map<Prop, int> m;
  Prop p0 = Prop{.p = Var{.id = 0}};
  Prop p1 = Prop{.p = Var{.id = 1}};

  m[True()] = 10;
  m[False()] = 20;
  m[p0] = 30;
  m[p1] = 40;
  m[p0 & p1] = 50;

  CHECK(m.size() == 5);
  CHECK(m[True()] == 10);
  CHECK(m[False()] == 20);
  CHECK(m[p0] == 30);
  CHECK(m[p1] == 40);
  CHECK(m[p0 & p1] == 50);

  // Check that equivalent Props resolve to the same keys.
  Prop p0_copy = Prop{.p = Var{.id = 0}};
  CHECK(m.contains(p0_copy));
  CHECK(m[p0_copy] == 30);

  Prop and_copy = p0_copy & Prop{.p = Var{.id = 1}};
  CHECK(m.contains(and_copy));
  CHECK(m[and_copy] == 50);
}

int main(int argc, char **argv) {
  ANSI::Init();

  TestValue();
  TestVar();
  TestUnop();
  TestBinop();
  TestNand();
  TestVariadicOr();
  TestVariadicAnd();
  TestPropVars();
  TestPropEq();
  TestPropSize();
  TestSimplifyProp();
  TestBalanceProp();
  TestNormalizeToAnd();
  TestSerializeParse();
  TestUnorderedMap();

  Print("OK\n");
  return 0;
}
