
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

int main(int argc, char* argv[]) {
  if (argc != 2) {
    fprintf(stderr, "afactor number\n");
    return -1;
  }

  BigInteger num;
  ParseBigInteger(argv[1], &num);

  // some functions take arguments by global variables, ugh
  NumberLength = num.nbrLimbs;
  // An "int array" representation uses a length (signed, to give the number's
  // sign) followed by that many limbs.
  BigInteger2IntArray(nbrToFactor, &num);

  factor(&num, nbrToFactor, factorsMod, astFactorsMod);

  auto PrintIntArray = [](const int *arr) {
      printf("arr[0] = %d\n", arr[0]);
      fflush(stdout);

      char buffer[20000];
      BigInteger tmp;
      NumberLength = *arr;
      IntArray2BigInteger(arr, &tmp);

      char *ptr = buffer;
      BigInteger2Dec(&ptr, &tmp, 0);
      *ptr = 0;
      printf("%s\n", buffer);
    };

  // NumberLength = *pstFactor->ptrFactor;
  // IntArray2BigInteger(pstFactor->ptrFactor, &bigTmp);

  const sFactors *hdr = &astFactorsMod[0];
  printf("Header:\n"
         "multiplicity: %d\n"
         "upperBound: %d\n"
         "type: %d\n"
         "num: ",
         hdr->multiplicity,
         hdr->upperBound,
         hdr->type);
  // PrintIntArray(hdr->ptrFactor);
  // printf("hdr->ptrFactor is %p\n", hdr->ptrFactor);

  printf("Factors:\n");

  for (int i = 1; i <= hdr->multiplicity; i++) {
    PrintIntArray(astFactorsMod[i].ptrFactor);
    printf("  multiplicity: %d\n"
           "  upperBound: %d\n"
           "  type: %d\n",
           astFactorsMod[i].multiplicity,
           astFactorsMod[i].upperBound,
           astFactorsMod[i].type);
  }

  return 0;
}
