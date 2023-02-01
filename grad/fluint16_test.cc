
#include "fluint16.h"
#include "fluint8.h"

#include "base/logging.h"

#define CHECK_EQ16(a, b) do {                               \
    uint16_t aa = (a), bb = (b);                                \
    CHECK(aa == bb) << #a << " vs " << #b << ":\n" <<       \
      StringPrintf("%04x vs %04x (%d vs %d)",               \
                   aa, bb, aa, bb);                         \
} while (false)

static void RightShifts() {
  Fluint16 x(0x8100);
  CHECK_EQ16(Fluint16::RightShift<1>(x).ToInt(), 0x4080);
  CHECK_EQ16(Fluint16::RightShift<2>(x).ToInt(), 0x2040);
}

int main(int argc, char **argv) {

  RightShifts();

  printf("OK\n");
  return 0;
}
