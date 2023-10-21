
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bignum/big.h"

#include "bignbr.h"
#include "factor.h"
#include "bigconv.h"

static void PrintBigInteger(const BigInteger *big) {
  BigInt b = BigIntegerToBigInt(big);
  printf("%s", b.ToString().c_str());
}

int main(int argc, char* argv[]) {
  if (argc != 2) {
    fprintf(stderr, "afactor number\n");
    return -1;
  }

  BigInteger num;
  ParseBigInteger(argv[1], &num);

  PrintBigInteger(&num);
  printf(":");
  fflush(stdout);

  std::vector<std::pair<BigInt, int>> factors =
    BigIntFactor(BigIntegerToBigInt(&num));
  for (const auto &[p, e] : factors) {
    for (int m = 0; m < e; m++) {
      printf(" %s", p.ToString().c_str());
    }
  }
  printf("\n");


  return 0;
}
