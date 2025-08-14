
#include "small-int-set.h"

#include <algorithm>
#include <cstdio>
#include <format>
#include <initializer_list>
#include <set>
#include <vector>

#include "ansi.h"
#include "base/macros.h"

static void CreateAndDestroy() {
  SmallIntSet<1> one;
  SmallIntSet<2> two;
  SmallIntSet<63> sixty_three;
  SmallIntSet<64> sixty_four;
}

template<int N>
static void Simple() {
  static_assert(N >= 2);
  SmallIntSet<N> s;
  CHECK(s.Empty());
  CHECK(s.Size() == 0);
  CHECK(!s.Contains(0));
  CHECK(!s.Contains(N - 1));
  CHECK(s.begin() == s.end());

  s.Add(1);
  CHECK(!s.Empty());
  CHECK(s.Size() == 1);
  CHECK(!s.Contains(0));
  if constexpr (N - 1 != 1) {
    CHECK(!s.Contains(N - 1));
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
  CHECK(!s.Contains(N - 1));
  CHECK(!s.Contains(1));
  CHECK(s.begin() == s.end());

  s = SmallIntSet<N>::Top();
  CHECK(s.Size() == N);
  CHECK(s.Contains(0));
  CHECK(s.Contains(N - 1));
  CHECK(s.Contains(1));
  CHECK(s[0] == 0);
  CHECK(s[1] == 1);

  s.Toggle(0);
  CHECK(s.Size() == N - 1);
  CHECK(!s.Contains(0));
  CHECK(s.Contains(N - 1));
  CHECK(s.Contains(1));
  CHECK(s[0] == 1);
  if constexpr (N > 2) {
    CHECK(s[1] == 2);
  }

  s.Toggle(N - 1);
  CHECK(s.Size() == N - 2);
  CHECK(!s.Contains(0));
  CHECK(!s.Contains(N - 1));
  if constexpr (N > 2) {
    CHECK(s.Contains(1));
  }

#define ITERATE_LIST(...) do {                                          \
    static constexpr std::initializer_list<int> init = {__VA_ARGS__};   \
    if constexpr (N > std::max(init)) {                                 \
      SmallIntSet<N> s(init);                                           \
      std::set<int> ss(init);                                           \
      CHECK(s.Size() == (int)ss.size());                                \
      std::vector<int> expected;                                        \
      for (int i : ss) expected.push_back(i);                           \
      std::vector<int> actual;                                          \
      for (int i : s) actual.push_back(i);                              \
      CHECK(actual == expected);                                        \
    }                                                                   \
  } while (0)

  ITERATE_LIST(4, 8, 15, 16, 23, 42);
  ITERATE_LIST(0);
  ITERATE_LIST(63);
  ITERATE_LIST(0, 0);
  ITERATE_LIST(63, 2, 2, 0, 63, 31);
}


int main(int argc, char **argv) {
  ANSI::Init();

  CreateAndDestroy();
  Simple<2>();
  Simple<31>();
  Simple<32>();
  Simple<63>();
  Simple<64>();

  printf("OK\n");
  return 0;
}
