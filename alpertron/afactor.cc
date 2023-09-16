
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bignum/big.h"

#include "bignbr.h"
#include "factor.h"
#include "bigconv.h"

#include "baseconv.h"

static void PrintBigInteger(const BigInteger *big) {
  BigInt b = BigIntegerToBigInt(big);

  char buffer[20000];
  char *ptr = buffer;
  BigInteger2Dec(&ptr, big);
  *ptr = 0;
  printf("%s", buffer);
}

static void PrintIntArray(const int *arr) {
  // printf("arr[0] = %d\n", arr[0]);
  // fflush(stdout);

  BigInteger tmp;
  int number_length = *arr;
  IntArray2BigInteger(number_length, arr, &tmp);

  PrintBigInteger(&tmp);
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

  std::unique_ptr<Factors> factors = BigFactor(&num);
  for (int i = 0; i < (int)factors->product.size(); i++) {
    for (int m = 0; m < factors->product[i].multiplicity; m++) {
      printf(" ");
      PrintIntArray(factors->product[i].array);
    }
  }
  printf("\n");


  return 0;
}
