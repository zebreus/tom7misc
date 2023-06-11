#include "vector-util.h"

#include "base/logging.h"
#include <stdio.h>
#include <cstdint>
#include <vector>
#include <string>

using namespace std;

static void TestApp() {
  std::string s = "012345";
  std::vector<std::pair<int, char>> v = {
    {2, 'a'},
    {1, 'c'},
    {5, 'z'},
  };

  AppVector(v, [&s](const std::pair<int, char> p) {
      CHECK(p.first >= 0 && p.first < (int)s.size());
      s[p.first] = p.second;
    });
  CHECK(s == "0ca34z") << s;
}

static void TestFilter() {
  {
    std::vector<std::string> v = {
      "hello",
      "world",
    };

    FilterVector(&v, [](const std::string &s) { return true; });
    CHECK(v.size() == 2);
    CHECK(v[0] == "hello");
    CHECK(v[1] == "world");

    FilterVector(&v, [](const std::string &s) { return false; });
    CHECK(v.empty());
  }

  std::vector<std::string> v = {
    "all",
    "you",
    "have",
    "to",
    "do",
    "is",
    "maybe",
    "do",
    "it",
    "yeah?",
  };

  FilterVector(&v, [](const std::string &s) { return s.size() == 2; });
  CHECK(v.size() == 5);
  CHECK(v[0] == "to");
  CHECK(v[1] == "do");
  CHECK(v[2] == "is");
  CHECK(v[3] == "do");
  CHECK(v[4] == "it");
}

int main(int argc, char **argv) {
  TestApp();
  TestFilter();
  printf("OK\n");
  return 0;
}

