
#include "city/city.h"

#include <cstdio>
#include <format>

#include "base/logging.h"

using namespace std;

#define CHECK_EQ64(a, b) do {                             \
    CHECK(a == b) << std::format("{:x}", a)               \
                  << " vs " << std::format("{:x}", b);    \
  } while (0)

static void TestKnown() {
  CHECK_EQ64(CityHash64("archaeopteryx"), 0xbf8577469841d551ULL);
  CHECK_EQ64(CityHash64WithSeed("archaeopteryx", 0x12345678abababULL),
             0xc70acdb8b87d82a0ULL);
}

int main(int argc, char **argv) {

  TestKnown();

  printf("OK\n");
  return 0;
}
