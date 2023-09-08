
#include "bigconv.h"

#include <string>
#include <cstring>

#include "base/logging.h"
#include "bignum/big.h"
#include "bignbr.h"
#include "baseconv.h"
#include "base/stringprintf.h"

static constexpr bool CHECK_INVARIANTS = false;

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
  return LimbsToBigInt((const limb *)(arr + 1), *arr);
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

BigInt LimbsToBigInt(const limb *limbs, int num_limbs) {
  BigInt out;
  mpz_import(out.GetRep(), num_limbs,
             // words are little-endian
             -1,
             // word size
             sizeof (int),
             // native byte-order
             0,
             // "nails": high bits to skip in each word
             1,
             limbs);
  return out;
}

int BigIntToLimbs(const BigInt &b, limb *limbs) {
  size_t count = 0;
  mpz_export(limbs, &count,
             // words are little endian.
             -1,
             // word size
             sizeof(int),
             // native byte-order
             0,
             // 31 bits per word
             1,
             b.GetRep());
  return count;
}



string LongNum(const BigInt &a) {
  string sign = BigInt::Less(a, 0) ? "-" : "";
  string num = BigInt::Abs(a).ToString();
  if (num.size() > 80) {
    static constexpr int SHOW_SIDE = 8;
    int skipped = num.size() - (SHOW_SIDE * 2);
    return StringPrintf("%s%s…(%d)…%s",
                        sign.c_str(),
                        num.substr(0, SHOW_SIDE).c_str(),
                        skipped,
                        num.substr(num.size() - SHOW_SIDE,
                                   string::npos).c_str());
  } else if (num.size() < 7) {
    return sign + num;
  } else {
    // with commas.
    string out;
    while (num.size() > 3) {
      if (!out.empty()) out = "," + out;
      out = num.substr(num.size() - 3, string::npos) + out;
      num.resize(num.size() - 3);
    }
    CHECK(!num.empty());
    CHECK(!out.empty());
    return sign + num + "," + out;
  }
}
