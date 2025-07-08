
#include "heap.h"

#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <format>
#include <vector>

#include "base/logging.h"

using namespace std;

using uint64 = uint64_t;

static constexpr bool VERBOSE = false;

namespace {
struct TestValue : public Heapable {
  TestValue(uint64 i) : i(i) {}
  uint64 i;
};
}  // namespace

static uint64 CrapHash(int a) {
  uint64 ret = ~a;
  ret *= 31337;
  ret ^= 0xDEADBEEF;
  ret = (ret >> 17) | (ret << (64 - 17));
  ret -= 911911911911;
  ret *= 65537;
  ret ^= 0xCAFEBABE;
  return ret;
}

static void Simple() {
  static constexpr int kNumValues = 1000;

  Heap<uint64, TestValue> heap;

  vector<TestValue> values;
  for (int i = 0; i < kNumValues; i++) {
    values.push_back(TestValue(CrapHash(i)));
  }

  for (int i = 0; i < (int)values.size(); i++) {
    heap.Insert(values[i].i, &values[i]);
  }

  TestValue *last = heap.PopMinimumValue();
  while (!heap.Empty()) {
    TestValue *now = heap.PopMinimumValue();
    if (VERBOSE) {
      fprintf(stderr, "%" PRIu64 " %" PRIu64 "\n", last->i, now->i);
    }
    CHECK(now->i >= last->i) <<
      std::format("FAIL: {} {}\n", last->i, now->i);
    last = now;
  }

  for (int i = 0; i < (int)values.size(); i++) {
    CHECK(values[i].location == -1) <<
      std::format("{} still in heap at {}\n", i, values[i].location);
  }

  for (int i = 0; i < (int)values.size() / 2; i++) {
    heap.Insert(values[i].i, &values[i]);
  }

  heap.Clear();
  CHECK(heap.Empty()) << "Heap not empty after clear?\n";

  for (int i = 0; i < (int)values.size() / 2; i++) {
    CHECK(values[i].location == -1) <<
      std::format("FAIL (B)! {} still in heap at {}\n", i, values[i].location);
  }
}

int main () {
  Simple();

  printf("OK\n");
  return 0;
}
