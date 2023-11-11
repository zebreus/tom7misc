
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bignum/big.h"
#include "factor.h"

int main(int argc, char* argv[]) {
  if (argc != 2) {
    fprintf(stderr, "afactor number\n");
    return -1;
  }

  BigInt num(argv[1]);

  printf("%s:", num.ToString().c_str());
  fflush(stdout);

  std::vector<std::pair<BigInt, int>> factors = BigIntFactor(num);
  for (const auto &[p, e] : factors) {
    for (int m = 0; m < e; m++) {
      printf(" %s", p.ToString().c_str());
    }
  }
  printf("\n");

  return 0;
}
