
#include "aig.h"

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"

static void TestConstants() {
  AIG simp(5);
  CHECK(simp.F() != simp.T());
  CHECK(simp.NOT(simp.F()) != simp.F());
  CHECK(simp.NOT(simp.NOT(simp.F())) == simp.F());
  CHECK(simp.NOT(simp.NOT(simp.T())) == simp.T());
}

static void TestVariables() {
  AIG simp(5);
  CHECK(simp.V(0) != simp.V(1));
  CHECK(simp.V(0) == simp.V(0));
  CHECK(simp.V(2) != simp.F());
  CHECK(simp.V(2) != simp.T());
  CHECK(simp.NOT(simp.V(2)) != simp.V(2));
  CHECK(simp.NOT(simp.NOT(simp.V(3))) == simp.V(3));
}

static void TestHashConsing() {
  AIG simp(5);
  auto a = simp.V(0);
  auto b = simp.V(1);
  auto c = simp.V(2);

  auto and1 = simp.AND(a, b);
  auto and2 = simp.AND(a, b);
  // Hash consing should ensure these evaluate to the same node ID.
  CHECK(and1 == and2);

  // Normalization should ensure swapped arguments yield the same node.
  auto and3 = simp.AND(b, a);
  CHECK(and1 == and3);

  // Transitive properties of hash consing.
  auto and4 = simp.AND(and1, c);
  auto and5 = simp.AND(and3, c);
  CHECK(and4 == and5);

  CHECK(and4 != and1);
  CHECK(and4 != and3);
  CHECK(and4 != c);

  CHECK(and1 != a);
  CHECK(and1 != simp.F());
}

int main() {
  ANSI::Init();

  TestConstants();
  TestVariables();
  TestHashConsing();

  Print("OK\n");
  return 0;
}
