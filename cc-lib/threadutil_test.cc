
#include "threadutil.h"

#include <vector>
#include <string>

#include "base/stringprintf.h"
#include "base/logging.h"

using namespace std;

int Square(int i) {
  return i * i;
}

template<class T>
static void CheckSameVec(const vector<T> &a,
                         const vector<T> &b) {
  CHECK(a.size() == b.size()) << "\n" << a.size() << " != " << b.size();
  for (int i = 0; i < (int)a.size(); i++) {
    CHECK(a[i] == b[i]) << "\n" << a[i] << " != " << b[i];
  }
}

static void TestAccumulate() {
  int NUM = 100000;
  // Note that vector<bool> is no good here; it does not have
  // thread-safe access to individual elements (which are packed into
  // words).
  vector<int> did_run(NUM, 0);
  int64 acc =
    ParallelAccumulate<int64>(NUM, 0LL, [](int64 a, int64 b) { return a + b; },
                              [&did_run](int idx, int64 *acc) {
                                CHECK(did_run[idx] == 0) << idx << " = "
                                                         << did_run[idx];
                                did_run[idx] = idx;
                                ++*acc;
                              }, 25);
  for (int i = 0; i < NUM; i++) CHECK(did_run[i] == i) << i;
  CHECK_EQ(acc, NUM) << acc;
}

static void TestMap() {
  {
    vector<int> v = { 3, 2, 1 };
    vector<int> vs = ParallelMap(v, Square, 25);
    CHECK((vector<int>{9, 4, 1}) == vs);
    CHECK(vs == UnParallelMap(v, Square, 25));

    for (int i = 0; i < 10; i++) {
      CheckSameVec(UnParallelMap(v, Square, i),
                   ParallelMap(v, Square, i));
    }
  }

  {
    vector<string> v;
    for (int i = 0; i < 100; i++)
      v.push_back(StringPrintf("hello %d", i));

    auto F = [](const string &s) { return s + " world"; };

    for (int i = 0; i < 20; i++) {
      CheckSameVec(UnParallelMap(v, F, i), ParallelMap(v, F, i));
    }
  }
}

static void TestMapi() {
  vector<char> ecs;
  for (int c = 0; c < 255; c++) ecs.push_back(c ^ 0x5F);

  for (int th = 1; th < 100; th++) {
    vector<char> cs = ParallelMapi(ecs, [](int i, char c) {
        return (char)((c ^ 0x5f) - i);
      }, th);
    for (char c : cs) CHECK(c == 0);
  }
}

static void TestAsynchronously() {
  static constexpr int MAX_THREADS = 4;

  std::mutex m;
  int simultaneous = 0;
  int max_simultaneous = 0;
  int ran = 0;

  {
    Asynchronously async(MAX_THREADS);
    for (int i = 0; i < 100; i++) {
      async.Run([&m, &simultaneous, &max_simultaneous, &ran]() {
          {
            std::unique_lock<std::mutex> ul(m);
            simultaneous++;
            CHECK(simultaneous <= MAX_THREADS) << simultaneous;
            max_simultaneous =
              std::max(simultaneous, max_simultaneous);
          }
          std::this_thread::sleep_for(50ms);
          {
            std::unique_lock<std::mutex> ul(m);
            ran++;
            simultaneous--;
            CHECK(simultaneous >= 0);
          }
        });
    }
  }

  CHECK(simultaneous == 0);
  CHECK(ran == 100);
  CHECK(max_simultaneous == MAX_THREADS) << "This can fail "
    "with bad schedules, but it would at least suggest that "
    "the code could be improved to do what it says on the tin "
    "in practice! " << max_simultaneous;
}

static void TestInParallel() {
  int a = 0, b = 0;
  InParallel([&a]() { a++; },
             [&b]() { b = 7; });
  CHECK(a == 1 && b == 7);
}

int main(int argc, char **argv) {

  TestMap();
  TestMapi();
  TestAccumulate();
  TestAsynchronously();
  TestInParallel();

  printf("OK.\n");
  return 0;
}
