#include "simplification.h"

#include <variant>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "prop.h"

static void TestSimplificationBasic(const Simplification &simp) {
  Prop a = Prop{.p = Var{1}};
  Prop b = Prop{.p = Var{2}};

  Prop p = a | (a & b);
  Prop s = simp.Simplify(p);

  CHECK(PropEq(s, a)) << "Expected a | (a & b) to simplify to a.";
}

static void TestSimplificationConstants(const Simplification &simp) {
  Prop a = Prop{.p = Var{1}};

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

static void TestSimplificationFiveVars(const Simplification &simp) {
  Prop v1 = Prop{.p = Var{1}};
  Prop v2 = Prop{.p = Var{2}};
  Prop v3 = Prop{.p = Var{3}};
  Prop v4 = Prop{.p = Var{4}};
  Prop v5 = Prop{.p = Var{5}};

  Prop p = (v1 & v2) | (v1 & v3) | (v1 & v4) | (v1 & v5);
  Prop s = simp.Simplify(p);

  CHECK(PropEq(s, p)) << "Should maintain semantic equality.";
}

static void TestSimplificationDeMorgan(const Simplification &simp) {
  Prop v1 = Prop{.p = Var{1}};
  Prop v2 = Prop{.p = Var{2}};
  Prop v3 = Prop{.p = Var{3}};
  Prop v4 = Prop{.p = Var{4}};
  Prop v5 = Prop{.p = Var{5}};

  // AIG representation of an OR with 5 variables.
  // By De Morgan's laws it should simplify using ORs.
  Prop p = -(-v1 & -v2 & -v3 & -v4 & -v5);
  Prop s = simp.Simplify(p);

  CHECK(PropEq(s, p)) << "De Morgan simplification should maintain "
    "semantic equality.";

  // At the root, we expect it to be an OR proposition now.
  CHECK(std::holds_alternative<Binop>(s.p));
  if (const Binop *bop = std::get_if<Binop>(&s.p)) {
    CHECK(bop->op == BinopOp::OR) << "Expected an OR proposition at the root.";
  }
}

static void TestSimplificationIte(const Simplification &simp) {
  Prop a = Prop{.p = Var{1}};
  Prop b = Prop{.p = Var{2}};
  Prop c = Prop{.p = Var{3}};

  std::vector<Prop> props = {
    Ite(c, a, b),
    Ite(True(), a, b),
    Ite(False(), a, b),
    Ite(c, a, a),
    Ite(c, True(), False()),
    Ite(c, Ite(a, b, c), Ite(b, c, a)),
  };

  for (const Prop &p : props) {
    Prop s = simp.Simplify(p);
    CHECK(PropEq(s, p))
        << "Simplification of ITE should maintain semantic equality.";
  }
}

int main(int argc, char **argv) {
  ANSI::Init();

  Simplification simp;

  TestSimplificationBasic(simp);
  TestSimplificationConstants(simp);
  TestSimplificationFiveVars(simp);
  TestSimplificationDeMorgan(simp);
  TestSimplificationIte(simp);

  Print("OK\n");
  return 0;
}
