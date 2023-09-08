
#include "bignbr.h"
#include "bigmultiply.h"
#include "bigconv.h"
#include "karatsuba.h"
#include "bignum/big.h"
#include "bignum/big-overloads.h"
#include "base/logging.h"

static void TestMult(const char *s1, const char *s2) {
  BigInteger a, b;
  CHECK(ParseBigInteger(s1, &a));
  CHECK(ParseBigInteger(s2, &b));


  BigInteger r;
  r.sign = SIGN_POSITIVE;
  multiplyWithBothLenKaratsuba(a.limbs, b.limbs, r.limbs,
                               a.nbrLimbs, b.nbrLimbs, &r.nbrLimbs);

  BigInt br = BigIntegerToBigInt(&r);

  BigInt rr = BigInt(s1) * BigInt(s2);
  CHECK(BigInt::Eq(br, rr)) << br.ToString() << " vs\n" << rr.ToString();
}


int main(int argc, char **argv) {

  TestMult("0", "0");
  TestMult("1", "0");
  TestMult("0", "1");
  TestMult("1", "1");
  TestMult("2", "3");
  TestMult("2147483648", "2");
  TestMult("2147483647", "2");
  TestMult("2147483649", "2");
  TestMult("2147483648", "2147483648");
  TestMult("2983741982734981741",
           "100000000000000000000000000000100000000000000000000");
  printf("OK\n");
  return 0;
}
