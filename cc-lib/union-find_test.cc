
#include "union-find.h"
#include "base/logging.h"
#include "base/print.h"
#include "ansi.h"

static void TestDisjoint() {
  UnionFind uf(5);
  CHECK(uf.Size() == 5);

  // Initially, every element is in its own set.
  for (int i = 0; i < 5; i++) {
    CHECK(uf.Find(i) == i);
  }
}

static void TestUnion() {
  UnionFind uf(5);

  // Join 0 and 1
  uf.Union(0, 1);
  CHECK(uf.Find(0) == uf.Find(1));
  CHECK(uf.Find(0) != uf.Find(2));

  // Join 1 and 2 (transitive: 0, 1, 2 are together)
  uf.Union(1, 2);
  CHECK(uf.Find(0) == uf.Find(2));
  CHECK(uf.Find(1) == uf.Find(2));

  // Join 3 and 4
  uf.Union(3, 4);
  CHECK(uf.Find(3) == uf.Find(4));
  CHECK(uf.Find(0) != uf.Find(3));

  // Join the two sets
  uf.Union(2, 4);
  CHECK(uf.Find(0) == uf.Find(4));
}

static void TestReset() {
  // Test Reset
  UnionFind uf(3);
  uf.Union(0, 1);
  CHECK(uf.Find(0) == uf.Find(1));

  uf.Reset();
  for (int i = 0; i < 3; i++) {
    CHECK(uf.Find(i) == i);
  }
}

static void TestDeepChain() {
  // Linking sequentially creates an O(N) depth chain.
  constexpr int kSize = 300000;
  UnionFind uf(kSize);
  for (int i = 0; i < kSize - 1; i++) {
    uf.Union(i, i + 1);
  }

  // Make sure we don't have a stack overflow.
  uf.Find(0);
  CHECK(uf.Find(0) == uf.Find(kSize - 1));
}

int main() {
  ANSI::Init();
  TestDisjoint();
  TestUnion();
  TestReset();
  TestDeepChain();
  Print("OK\n");
  return 0;
}
