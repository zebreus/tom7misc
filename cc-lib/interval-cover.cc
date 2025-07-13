
#include "interval-cover.h"

#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>

using namespace std;

template<>
void IntervalCover<string>::DebugPrint() const {
  printf("------\n");
  for (const pair<const uint64_t, string> &p : spans) {
    printf("%" PRIu64 ": %s\n", p.first, p.second.c_str());
  }
  printf("------\n");
}

template<>
void IntervalCover<int>::DebugPrint() const {
  printf("------\n");
  for (const pair<const uint64_t, int> &p : spans) {
    printf("%" PRIu64 ": %d\n", p.first, p.second);
  }
  printf("------\n");
}

template<>
void IntervalCover<bool>::DebugPrint() const {
  printf("------\n");
  for (const pair<const uint64_t, bool> &p : spans) {
    printf("%" PRIu64 ": %s\n", p.first, p.second ? "true" : "false");
  }
  printf("------\n");
}
