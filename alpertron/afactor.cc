
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

  // some functions take arguments by global variables, ugh
  NumberLength = num.nbrLimbs;
  // An "int array" representation uses a length (signed, to give the number's
  // sign) followed by that many limbs.
  BigInteger2IntArray(nbrToFactor, &num);

  factor(&num, nbrToFactor, factorsMod, astFactorsMod);

  const sFactors *hdr = &astFactorsMod[0];
  const int num_factors = hdr->multiplicity;

  for (int i = 1; i <= num_factors; i++) {
    for (int m = 0; m < astFactorsMod[i].multiplicity; m++) {
      printf(" ");
      PrintIntArray(astFactorsMod[i].ptrFactor);
    }
  }
  printf("\n");

  return 0;
}
