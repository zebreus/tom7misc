
#include "interval-cover.h"

#include <string>

using namespace std;

template<>
void IntervalCover<string>::DebugPrint() const {
  printf("------\n");
  for (const pair<const uint64_t, string> &p : spans) {
    printf("%llu: %s\n", p.first, p.second.c_str());
  }
  printf("------\n");
}

template<>
void IntervalCover<int>::DebugPrint() const {
  printf("------\n");
  for (const pair<const uint64_t, int> &p : spans) {
    printf("%llu: %d\n", p.first, p.second);
  }
  printf("------\n");
}
