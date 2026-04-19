#include "inline-vector.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <utility>

#include "ansi.h"
#include "base/print.h"
#include "base/logging.h"

namespace {
struct WithPadding {
  uint64_t a;
  uint8_t b;
};
}

static void TestSizes() {
  InlineVector<uint8_t> iv1;
  CHECK(iv1.MaxInline() >= 48) << "This depends on compiler alignment so it isn't "
    "really guaranteed. But we should tweak something if we can't even inline "
    "a significant number of bytes.";

  InlineVector<uint8_t *> iv2;
  CHECK(iv2.MaxInline() >= 4) << "This depends on compiler alignment so it isn't "
    "really guaranteed. But we should tweak something if we can't even inline "
    "a few pointers.";

  InlineVector<WithPadding> iv3;
  CHECK(iv3.MaxInline() < iv1.MaxInline()) << "Object is bigger";

  InlineVector<std::array<uint8_t, 1023>> iv4;
  CHECK(iv4.MaxInline() == 1) << "Expected a very large object to have the "
    "minimum inlining.";
}

static void TestPair() {
  std::string a = "hello", b = "world", c = "goodbye", d = "chores";
  InlineVector<std::pair<const std::string *, int64_t>> v;
  v.reserve(2);
  v.emplace_back(&a, 31337);
  CHECK(v[0].first == &a);
  CHECK(v[0].second == 31337);
  v.emplace_back(&b, 27);
  v.emplace_back(&c, 1234);
  v.emplace_back(&d, 42);
  for (int i = 0; i < 32; i++) {
    v.emplace_back(&a, i);
    CHECK(v[0].first == &a);
    CHECK(v[0].second == 31337);
  }
  v[1].second = 131072;

  for (int i = 0; i < 32; i++) {
    CHECK(v.back().first == &a);
    CHECK(v[0].first == &a);
    CHECK(v[0].second == 31337);
    v.pop_back();
  }
  CHECK(v.back().second == 42);
  CHECK(v[1].second == 131072);
  CHECK(v[1].first == &b);
}

static void TestWithPadding() {
  InlineVector<WithPadding> iv4;
  CHECK(iv4.empty());
  iv4.push_back(WithPadding{.a = uint64_t{0x0123456789ABCDEF}, .b = 0x2a});
  iv4.push_back(WithPadding{.a = uint64_t{0x4567890ABCDEF123}, .b = 0x5f});
  CHECK(iv4[0].a == 0x0123456789ABCDEF);
  CHECK(iv4[0].b == 0x2a);
  iv4.push_back(WithPadding{.a = uint64_t{0x890ABCDEF1234567}, .b = 0xf3});
  iv4.push_back(WithPadding{.a = uint64_t{0xBCDEF1234567890A}, .b = 0x19});
  CHECK(iv4[2].a == 0x890ABCDEF1234567);
  CHECK(iv4.back().b == 0x19);
  iv4.push_back(WithPadding{.a = uint64_t{0xCDEF1234567890AB}, .b = 0x27});
  iv4.push_back(WithPadding{.a = uint64_t{0x234567890ABCDEF1}, .b = 0x87});
  CHECK(iv4[4].a == 0xCDEF1234567890AB);
  CHECK(iv4[4].b == 0x27);
  CHECK(iv4.back().b == 0x87);
  CHECK(iv4.capacity() > iv4.MaxInline()) << "Expand the test so that it "
    "exercises external allocation.";
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
  // (Suppress clang warning by doing it via an alias.)
  auto &alias = assigned;
  assigned = alias;
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

static void TakesSpanIncBack(std::span<int> s) {
  CHECK(s[0] == 27);
  CHECK(s.back() == 42);
  s.back()++;
}

static void TakesConstSpan(std::span<int> s) {
  CHECK(s[0] == 27);
  CHECK(s.back() == 42);
}

static void TestSpans() {
  InlineVector<int> iv;
  iv.push_back(27);
  iv.push_back(33);
  iv.push_back(42);
  TakesConstSpan(iv);
  TakesSpanIncBack(iv);
  CHECK(iv.size() == 3);
  CHECK(iv[2] == 43) << iv[2];
  CHECK(iv.back() == 43);

  iv.pop_back();
  for (int i = 0; i < 100; i++) {
    iv.push_back(i ^ 0x5A);
  }
  iv.push_back(42);
  TakesConstSpan(iv);
  TakesSpanIncBack(iv);
  CHECK(iv.back() == 43);
}

int main(int argc, char **argv) {
  ANSI::Init();

  TestSizes();
  TestSimple();
  TestSimple2();
  TestWithPadding();
  TestReserve();
  TestIterators();
  TestAlloc();
  TestCopySemantics();
  TestMoveSemantics();
  TestPair();
  TestSpans();

  Print("OK\n");
  return 0;
}
