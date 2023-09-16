
#include "bignbr.h"
#include "bigconv.h"

#include "bignum/big.h"
#include "bignum/big-overloads.h"
#include "base/logging.h"

#include "modmult.h"
#include "bigconv.h"

static void TestSubModN() {
  BigInt a(100);
  BigInt b(125);
  BigInt c(200);

  limb n1[8], n2[8], diff[8], mod[8];
  BigIntToFixedLimbs(a, 8, n1);
  BigIntToFixedLimbs(b, 8, n2);
  BigIntToFixedLimbs(c, 8, mod);

  SubtBigNbrModN(n1, n2, diff, mod, 8);

  BigInt d = LimbsToBigInt(diff, 8);
  CHECK(d == 175);
}

int main(int argc, char **argv) {
  TestSubModN();

  printf("OK\n");
  return 0;
}
