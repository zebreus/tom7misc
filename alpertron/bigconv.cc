
#include "bigconv.h"

#include <string>
#include <cstring>

#include "base/logging.h"
#include "bignum/big.h"
#include "bignbr.h"
#include "baseconv.h"

using namespace std;

extern int NumberLength;

bool ParseBigInteger(const char *str, BigInteger *big) {
  big->sign = SIGN_POSITIVE;
  if (str[0] == '-') {
    str++;
    big->sign = SIGN_NEGATIVE;
  }

  int nbrDigits = (int)strlen(str);
  Dec2Bin(str, big->limbs, nbrDigits, &big->nbrLimbs);

  return true;
}

void BigIntToBigInteger(const BigInt &b, BigInteger *g) {
  // PERF: Can use mpz_export, although that assumes GMP.
  string s = b.ToString();
  CHECK(ParseBigInteger(s.c_str(), g));
}

int BigIntToArray(const BigInt &b, int *arr) {
  // PERF: Directly...
  BigInteger tmp;
  BigIntToBigInteger(b, &tmp);

  const int number_length = tmp.nbrLimbs;
  NumberLength = number_length;
  BigInteger2IntArray(number_length, arr, &tmp);
  return 1 + *arr;
}

BigInt BigIntegerToBigInt(const BigInteger *g) {
  // PERF not through decimal, at least...
  char buffer[20000];
  char *ptr = buffer;
  BigInteger2Dec(&ptr, g, 0);
  *ptr = 0;
  return BigInt(buffer);
}
