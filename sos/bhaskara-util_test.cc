
#include "bhaskara-util.h"

#include "bignum/big.h"
#include "ansi.h"

static void TestLongNum() {
  CHECK(LongNum(BigInt{1234567}) == "1,234,567") << LongNum(BigInt{1234567});
  CHECK(LongNum(BigInt{31234567}) == "31,234,567");
  CHECK(LongNum(BigInt{431234567}) == "431,234,567");
  CHECK(LongNum(BigInt{"9431234567"}) == "9,431,234,567") <<
    LongNum(BigInt{"9431234567"});
  CHECK(LongNum(BigInt{1234}) == "1234");
}

int main(int argc, char **argv) {
  ANSI::Init();

  TestLongNum();

  printf("OK\n");
  return 0;
}
