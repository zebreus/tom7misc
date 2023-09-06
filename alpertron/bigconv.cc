
#include "bigconv.h"

#include <string>
#include <cstring>

#include "base/logging.h"
#include "bignum/big.h"
#include "bignbr.h"
#include "baseconv.h"

static constexpr bool CHECK_INVARIANTS = true;

using namespace std;

extern int NumberLength;

bool ParseBigInteger(const char *str, BigInteger *big) {

  const char *snat = str;
  big->sign = SIGN_POSITIVE;
  if (snat[0] == '-') {
    snat++;
    big->sign = SIGN_NEGATIVE;
  }

  int nbrDigits = (int)strlen(snat);
  Dec2Bin(snat, big->limbs, nbrDigits, &big->nbrLimbs);

  if (CHECK_INVARIANTS) {
    // XXX PERF!
    BigInt test(str);

    BigInt compare = BigIntegerToBigInt(big);
    CHECK(BigInt::Eq(test, compare)) << test.ToString() << " vs " << compare.ToString();
  }

  return true;
}

void BigIntToBigInteger(const BigInt &b, BigInteger *g) {
  // XXX could check up front that output has enough space...
  size_t count = 0;
  mpz_export(g->limbs, &count,
             // words are little endian.
             -1,
             // word size
             sizeof(int),
             // native byte-order
             0,
             // 31 bits per word
             1,
             b.GetRep());
  // Already overwrote the end of the array in this case,
  // but maybe we could be more useful by aborting.
  CHECK(count < MAX_LEN) << count;
  g->nbrLimbs = count;
  if (mpz_sgn(b.GetRep()) < 0) {
    g->sign = SIGN_NEGATIVE;
  } else {
    // 0 should also be "positive."
    g->sign = SIGN_POSITIVE;
  }
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

BigInt ArrayToBigInt(const int *arr) {
  int len = *arr;
  const int *data = arr + 1;
  BigInt out;
  mpz_import(out.GetRep(), len,
             // words are little-endian
             -1,
             // word size
             sizeof (int),
             // native byte-order
             0,
             // "nails": high bits to skip in each word
             1,
             data);
  return out;
}

BigInt BigIntegerToBigInt(const BigInteger *g) {
  BigInt out;
  mpz_import(out.GetRep(), g->nbrLimbs,
             // words are little-endian
             -1,
             // word size
             sizeof (int),
             // native byte-order
             0,
             // "nails": high bits to skip in each word
             1,
             g->limbs);
  if (g->sign == SIGN_NEGATIVE) {
    mpz_neg(out.GetRep(), out.GetRep());
  }
  return out;
}
