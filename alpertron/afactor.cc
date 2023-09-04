
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bignbr.h"
#include "globals.h"
#include "factor.h"

#include "baseconv.h"

static bool ParseBigInteger(const char *str, BigInteger *big) {
  big->sign = SIGN_POSITIVE;
  if (str[0] == '-') {
    str++;
    big->sign = SIGN_NEGATIVE;
  }

  int nbrDigits = (int)strlen(str);
  Dec2Bin(str, big->limbs, nbrDigits, &big->nbrLimbs);

  return true;
}

static void PrintBigInteger(const BigInteger *big) {
  char buffer[20000];
  char *ptr = buffer;
  BigInteger2Dec(&ptr, big, 0);
  *ptr = 0;
  printf("%s", buffer);
}

static void PrintIntArray(const int *arr) {
  // printf("arr[0] = %d\n", arr[0]);
  // fflush(stdout);

  BigInteger tmp;
  NumberLength = *arr;
  IntArray2BigInteger(arr, &tmp);

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
