
#include "base/print.h"
#include "dense-int-set.h"

#include <algorithm>
#include <cstdio>
#include <format>
#include <initializer_list>
#include <set>
#include <utility>
#include <vector>

#include "ansi.h"

static void CreateAndDestroy() {
  DenseIntSet one(1);
  DenseIntSet two(2);
  DenseIntSet sixty_three(63);
  DenseIntSet sixty_four(64);
  DenseIntSet two_fifty_five(255);
}

static void Extreme() {
  DenseIntSet degenerate(0);
  CHECK(degenerate.Empty());
  CHECK(degenerate.Size() == 0);
  CHECK(degenerate.begin() == degenerate.end());
}

static void Simple(size_t n) {
  CHECK(n >= 2);
  DenseIntSet s(n);
  CHECK(s.Empty());
  CHECK(s.Size() == 0);
  CHECK(!s.Contains(0));
  CHECK(!s.Contains(n - 1));
  CHECK(s.begin() == s.end());

  s.Add(1);
  CHECK(!s.Empty());
  CHECK(s.Size() == 1);
  CHECK(!s.Contains(0));
  if (n - 1 != 1) {
    CHECK(!s.Contains(n - 1));
  }
  CHECK(s.Contains(1));
  CHECK(s[0] == 1);
  CHECK(s.begin() != s.end());
  {
    auto it = s.begin();
    CHECK(*it == 1) << std::format("{}", *it);
    ++it;
    CHECK(it == s.end());
  }

  s.Remove(1);
  CHECK(s.Empty());
  CHECK(!s.Contains(0));
  CHECK(!s.Contains(n - 1));
  CHECK(!s.Contains(1));
  CHECK(s.begin() == s.end());

  s = DenseIntSet::Top(n);
  CHECK(s.Size() == n);
  CHECK(s.Contains(0));
  CHECK(s.Contains(n - 1));
  CHECK(s.Contains(1));
  CHECK(s[0] == 0);
  CHECK(s[1] == 1);

  s.Toggle(0);
  CHECK(s.Size() == n - 1);
  CHECK(!s.Contains(0));
  CHECK(s.Contains(n - 1));
  CHECK(s.Contains(1));
  CHECK(s[0] == 1);
  if (n > 2) {
    CHECK(s[1] == 2);
  }

  s.Toggle(n - 1);
  CHECK(s.Size() == n - 2);
  CHECK(!s.Contains(0));
  CHECK(!s.Contains(n - 1));
  if (n > 2) {
    CHECK(s.Contains(1));
  }

#define ITERATE_LIST(...) do {                              \
    static constexpr std::initializer_list<size_t> init =   \
      {__VA_ARGS__};                                        \
    if (n > std::max(init)) {                               \
      DenseIntSet s(n);                                     \
      for (size_t x : init) s.Add(x);                       \
      std::set<size_t> ss(init);                            \
      CHECK(s.Size() == ss.size());                         \
      std::vector<size_t> expected;                         \
      for (size_t i : ss) expected.push_back(i);            \
      std::vector<size_t> actual;                           \
      for (size_t i : s) actual.push_back(i);               \
      CHECK(actual == expected);                            \
    }                                                       \
  } while (0)

  ITERATE_LIST(4, 8, 15, 16, 23, 42);
  ITERATE_LIST(0);
  ITERATE_LIST(63);
  ITERATE_LIST(0, 0);
  ITERATE_LIST(255, 4, 8, 15, 16, 23, 42);
  ITERATE_LIST(63, 2, 2, 0, 63, 31);
  ITERATE_LIST(1, 0, 64, 255, 200, 255, 1, 3, 3);
}

static void Assignments() {
  DenseIntSet a(64);
  a.Add(5);

  DenseIntSet b(128);
  b.Add(100);

  a = b;
  CHECK(a.Radix() == 128);
  CHECK(a.Contains(100));
  CHECK(a.Size() == 1);
  CHECK(a == b);

  auto &a_alias = a;
  a = a_alias;
  CHECK(a.Radix() == 128);
  CHECK(a.Size() == 1);
  CHECK(a == a_alias);

  DenseIntSet c(256);
  c.Add(200);

  a = std::move(c);
  CHECK(a.Radix() == 256);
  CHECK(a.Contains(200));
  CHECK(a.Size() == 1);

  DenseIntSet d(std::move(a));
}

static void SetOperations() {
  DenseIntSet s64(64);
  s64.Add(1);
  s64.Add(10);
  s64.Add(63);

  DenseIntSet s128(128);
  s128.Add(10);
  s128.Add(100);
  s128.Add(127);

  DenseIntSet u1 = DenseIntSet::Union(s64, s128);
  CHECK(u1.Radix() == 128);
  CHECK(u1.Size() == 5);
  CHECK(u1.Contains(1));
  CHECK(u1.Contains(10));
  CHECK(u1.Contains(63));
  CHECK(u1.Contains(100));
  CHECK(u1.Contains(127));

  DenseIntSet u2 = DenseIntSet::Union(s128, s64);
  CHECK(u2 == u1);

  DenseIntSet ix = DenseIntSet::Intersection(s64, s128);
  CHECK(ix.Radix() == 128);
  CHECK(ix.Size() == 1);
  CHECK(ix.Contains(10));

  DenseIntSet ix2 = DenseIntSet::Intersection(s128, s64);
  CHECK(ix2 == ix);

  DenseIntSet empty64(64);
  DenseIntSet empty128(128);

  CHECK(DenseIntSet::Subset(empty64, empty64));
  CHECK(DenseIntSet::Subset(empty64, empty128));
  CHECK(DenseIntSet::Subset(empty128, empty64));

  CHECK(DenseIntSet::Subset(empty64, s64));
  CHECK(DenseIntSet::Subset(empty64, s128));
  CHECK(DenseIntSet::Subset(empty128, s64));

  // True subsets
  CHECK(DenseIntSet::Subset(s64, u1));
  CHECK(DenseIntSet::Subset(s128, u1));

  // Not subsets
  CHECK(!DenseIntSet::Subset(u1, s64));
  CHECK(!DenseIntSet::Subset(u1, s128));

  DenseIntSet s200_small(200);
  s200_small.Add(10);
  CHECK(DenseIntSet::Subset(s200_small, s64));

  DenseIntSet s200_large(200);
  s200_large.Add(150);
  CHECK(!DenseIntSet::Subset(s200_large, s64));

  DenseIntSet s64_copy = s64;
  CHECK(s64 == s64_copy);
  CHECK(!(s64 == s128)); << "Different radix.";

  // We don't really say how these are ordered, but they must
  // be consistent.
  CHECK((empty64 < s64) != (s64 < empty64));
  CHECK((empty64 < empty128) != (empty128 < empty64));
  CHECK((s64 < s128) != (s128 < s64));
  CHECK((s128 > s64) != (s64 > s128));
}

static void With() {
  // These have some subtle vectorization, so check on a
  // moderately large radix.
  DenseIntSet s1(1027);
  s1.Add(1);
  s1.Add(10);
  s1.Add(63);
  s1.Add(500);

  DenseIntSet s2(1027);
  s2.Add(10);
  s2.Add(100);
  s2.Add(127);
  s2.Add(500);
  s2.Add(501);

  {
    DenseIntSet s = s1;
    s.UnionWith(s2);
    CHECK(s.Contains(1));
    CHECK(s.Contains(10));
    CHECK(s.Contains(63));
    CHECK(s.Contains(500));
    CHECK(s.Contains(100));
    CHECK(s.Contains(127));
    CHECK(s.Contains(501));

    CHECK(DenseIntSet::Subset(s1, s));
    CHECK(DenseIntSet::Subset(s2, s));
  }

  {
    DenseIntSet s = s1;
    s.IntersectWith(s2);
    CHECK(s.Contains(10));
    CHECK(s.Contains(500));
    CHECK(!s.Contains(1));
    CHECK(!s.Contains(501));
    CHECK(!s.Contains(127));

    CHECK(DenseIntSet::Subset(s, s1));
    CHECK(DenseIntSet::Subset(s, s2));
  }
}

int main(int argc, char **argv) {
  ANSI::Init();

  CreateAndDestroy();
  Simple(2);
  Simple(31);
  Simple(32);
  Simple(63);
  Simple(64);
  Simple(65);
  Simple(255);
  Simple(256);
  Simple(257);
  Simple(65536);
  Extreme();

  Assignments();
  SetOperations();
  With();

  Print("OK\n");
  return 0;
}
