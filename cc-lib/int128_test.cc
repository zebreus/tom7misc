
#include "base/int128.h"

#include <cstdio>

#include "ansi.h"
#include "base/logging.h"

static void Basic() {

  // 2^70 * 3 * 7^2
  uint128 u = 1;
  u <<= 70;
  u *= 3;
  u *= 7 * 7;

  CHECK((uint64_t)u == 0);
  CHECK((uint64_t)(u >> 64) == 9408);

  CHECK(u % 7 == 0);
  CHECK(u % 3 == 0);
  CHECK(u % 2 == 0);
  CHECK(u % 49 == 0);
  CHECK(u % 21 == 0);
  CHECK(u % 42 == 0);

  u++;

  CHECK(u % 7 == 1);
  CHECK(u % 42 == 1);
  CHECK(u % 3 == 1);

}

int main(int argc, char **argv) {
  ANSI::Init();

  Basic();

  printf("OK\n");
  return 0;
}
