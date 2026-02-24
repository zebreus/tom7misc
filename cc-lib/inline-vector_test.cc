#include "inline-vector.h"

#include <cstdint>

#include "ansi.h"
#include "base/print.h"
#include "base/logging.h"

static void TestSizes() {
  InlineVector<uint8_t> iv1;
  CHECK(iv1.MaxInline() >= 48) << "This depends on compiler alignment so it isn't "
    "really guaranteed. But we should tweak something if we can't even inline "
    "a significant number of bytes.";

  InlineVector<uint8_t *> iv2;
  CHECK(iv2.MaxInline() >= 4) << "This depends on compiler alignment so it isn't "
    "really guaranteed. But we should tweak something if we can't even inline "
    "a few pointers.";
}

static void TestSimple() {
  InlineVector<int> v;
  Print("InlineVector<int> can inline {}\n", v.MaxInline());

  CHECK(v.size() == 0);
  CHECK(v.empty());

  for (int i = 0; i < 24; i++) {
    CHECK((int)v.size() == i);
    v.push_back(i);
    CHECK(!v.empty());
    CHECK(v.back() == i);
    for (int j = 0; j < (int)v.size(); j++) {
      CHECK(v[j] == j) << "On iter " << i << ", "
        "index " << j << " is actually " << v[j];
    }
  }

  InlineVector<int> vv = v;
  CHECK(vv == v);

  vv.push_back(9);
  CHECK(vv != v);
  v.push_back(9);
  CHECK(v == vv);

  CHECK(vv.back() == v.back());

  vv.pop_back();
  v.pop_back();
  CHECK(vv == v);

  v.clear();
  CHECK(v.empty());
  CHECK(v.size() == 0);
  for (int i = 0; i < (int)vv.size(); i++) {
    v.push_back(vv[i]);
  }
  CHECK(v == vv);
}

static void TestSimple2() {
  InlineVector<uint8_t> v;
  Print("InlineVector<uint8_t> can inline {}\n", v.MaxInline());

  CHECK(v.empty());
  CHECK(v.size() == 0);

  v.push_back(10);
  v.push_back(20);
  v.push_back(30);
  CHECK(!v.empty());
  CHECK(v.size() == 3);
  CHECK(v[0] == 10);
  CHECK(v[1] == 20);
  CHECK(v[2] == 30);

  CHECK(v.back() == 30);
  v.pop_back();
  CHECK(v.size() == 2);
  CHECK(v.back() == 20);

  InlineVector<uint8_t> fill(5, 99);
  CHECK(fill.size() == 5);
  for (uint8_t x : fill) {
    CHECK(x == 99);
  }
}

static void TestAlloc() {
  InlineVector<uint64_t> v;

  static constexpr int NUM_ELEMENTS = 1000;
  for (int i = 0; i < NUM_ELEMENTS; i++) {
    v.push_back((uint64_t)i);
  }

  CHECK(v.size() == NUM_ELEMENTS);
  for (int i = 0; i < NUM_ELEMENTS; i++) {
    CHECK(v[i] == (uint64_t)i);
  }

  // Clear after alloc, which should normally
  // preserve the capacity.
  v.clear();
  CHECK(v.empty());
  CHECK(v.size() == 0);

  v.push_back(42);
  CHECK(v.size() == 1);
  CHECK(v.back() == 42);
  CHECK(v[0] == 42);
}

static void TestCopySemantics() {
  InlineVector<int> original;
  for (int i = 0; i < 20; i++) {
    original.push_back(i);
  }

  InlineVector<int> copy(original);
  CHECK(copy.size() == 20);
  CHECK(copy == original);

  copy[0] = 999;
  CHECK(copy[0] == 999);
  CHECK(original[0] == 0);
  CHECK(copy != original);

  InlineVector<int> assigned;
  assigned.reserve(1000);
  assigned.push_back(1);
  assigned = original;
  CHECK(assigned.size() == 20);
  CHECK(assigned == original);

  // Self-assignment.
  assigned = assigned;
  CHECK(assigned.size() == 20);
  CHECK(assigned[19] == 19);
}

static void TestMoveSemantics() {
  InlineVector<int> original;
  for (int i = 0; i < 20; i++) {
    original.push_back(i * 2);
  }

  // Move constructor.
  InlineVector<int> moved_to(std::move(original));
  CHECK(moved_to.size() == 20);
  for (int i = 0; i < 20; i++) {
    CHECK(moved_to[i] == i * 2);
  }
  // Clear has no preconditions, so we can call it on a moved-from object.
  original.clear();
  CHECK(original.empty());
  CHECK(original.size() == 0);

  // Move assignment.
  InlineVector<int> move_assigned;
  move_assigned.reserve(1000);
  move_assigned.push_back(999);

  move_assigned = std::move(moved_to);
  CHECK(move_assigned.size() == 20);
  CHECK(move_assigned[19] == 38);
  CHECK(moved_to.empty());

  // Self-assignment.
  auto &alias = move_assigned;
  move_assigned = std::move(alias);
  CHECK(move_assigned.size() == 20);
  CHECK(move_assigned[19] == 38);
}

static void TestIterators() {
  InlineVector<int> v;
  for (int i = 1; i <= 5; i++) {
    v.push_back(i * 10);
  }

  {
    int expected = 10;
    for (int val : v) {
      CHECK(val == expected);
      expected += 10;
    }
    CHECK(expected == 60);
  }

  CHECK(*v.begin() == 10);
  CHECK(*(v.end() - 1) == 50);
  CHECK(v.end() - v.begin() == 5);

  {
    const InlineVector<int> &cv = v;
    int expected = 10;
    for (int val : cv) {
      CHECK(val == expected);
      expected += 10;
    }
  }
}

static void TestReserve() {
  InlineVector<int> v;

  v.reserve(100);
  CHECK(v.capacity() >= 100);
  CHECK(v.empty());

  for (int i = 0; i < 50; i++) {
    v.push_back(i);
  }
  CHECK(v.size() == 50);

  // Reserving a smaller amount than current capacity should do nothing
  size_t cap = v.capacity();
  v.reserve(10);
  CHECK(v.capacity() == cap);
  CHECK(v.size() == 50);
}

int main(int argc, char **argv) {
  ANSI::Init();

  TestSizes();
  TestSimple();
  TestSimple2();
  TestReserve();
  TestIterators();
  TestAlloc();
  TestCopySemantics();
  TestMoveSemantics();

  Print("OK\n");
  return 0;
}
