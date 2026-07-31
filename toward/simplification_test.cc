#include "simplification.h"

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "prop.h"

static void TestSimplificationBasic() {
  Prop a = Prop{.p = Var{1}};
  Prop b = Prop{.p = Var{2}};

  Simplification simp;

  Prop p = a | (a & b);
  Prop s = simp.Simplify(p);

  CHECK(PropEq(s, a)) << "Expected a | (a & b) to simplify to a.";
}

static void TestSimplificationConstants() {
  Prop a = Prop{.p = Var{1}};

  Simplification simp;

  Prop p1 = a & False();
  Prop s1 = simp.Simplify(p1);
  CHECK(s1 == False());

  Prop p2 = a | True();
  Prop s2 = simp.Simplify(p2);
  CHECK(s2 == True());

  Prop p3 = a ^ a;
  Prop s3 = simp.Simplify(p3);
  CHECK(s3 == False());
}

static void TestSimplificationFiveVars() {
  Prop v1 = Prop{.p = Var{1}};
  Prop v2 = Prop{.p = Var{2}};
  Prop v3 = Prop{.p = Var{3}};
  Prop v4 = Prop{.p = Var{4}};
  Prop v5 = Prop{.p = Var{5}};

  Simplification simp;

  Prop p = (v1 & v2) | (v1 & v3) | (v1 & v4) | (v1 & v5);
  Prop s = simp.Simplify(p);

  CHECK(PropEq(s, p)) << "Should maintain semantic equality.";
}

int main(int argc, char **argv) {
  ANSI::Init();

  TestSimplificationBasic();
  TestSimplificationConstants();
  TestSimplificationFiveVars();

  Print("OK\n");
  return 0;
}
