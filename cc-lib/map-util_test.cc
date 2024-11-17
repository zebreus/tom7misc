
#include "map-util.h"

#include <map>
#include <unordered_map>
#include <string>
#include <cstdint>

#include "base/logging.h"

static void TestCountMap() {
  {
    std::unordered_map<std::string, int> m;
    m["goodbye"] = 50;
    m["hello"] = 100;
    m["a"] = 3;

    std::vector<std::pair<std::string, int>> v =
      CountMapToDescendingVector(m);
    CHECK(v.size() == 3);
    CHECK(v[0].first == "hello");
    CHECK(v[0].second == 100);
    CHECK(v[2].second == 3);
  }

  {
    std::map<char, int64_t> m;
    m['g'] = 50;
    m['h'] = 100;
    m['a'] = 3;

    std::vector<std::pair<char, int64_t>> v =
      CountMapToDescendingVector(m);
    CHECK(v.size() == 3);
    CHECK(v[0].first == 'h');
    CHECK(v[0].second == 100);
    CHECK(v[2].second == 3);
  }
}



int main(int argc, char **argv) {

  TestCountMap();

  printf("OK\n");
  return 0;
}
