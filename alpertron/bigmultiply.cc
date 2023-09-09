
#include "bigmultiply.h"

#include <string>

#include "bignbr.h"
#include "bignum/big.h"
#include "bigconv.h"

#include "base/logging.h"
#include "base/stringprintf.h"

using namespace std;

static constexpr bool VERBOSE = false;

void BigMultiply(const limb* factor1, const limb* factor2, limb* result,
                 int len, int* pResultLen) {
  BigMultiplyWithBothLen(factor1, factor2, result, len, len, pResultLen);
}

void BigMultiplyWithBothLen(const limb* factor1, const limb* factor2, limb* result,
                            int len1, int len2, int* pResultLen) {
  // Note: Sometimes result is one of the factors.

  BigInt f1 = LimbsToBigInt(factor1, len1);
  BigInt f2 = LimbsToBigInt(factor2, len2);
  BigInt r = BigInt::Times(f1, f2);

  // This is the actual size used; for example multiplying a huge number by
  // zero yields zero, which is stored in one limb. But Alpertron appears to
  // assume in some cases that the full possible width is zeroed.
  const int zerolen = len1 + len2;
  memset(result, 0, zerolen * sizeof(int));

  if (VERBOSE) {
    fprintf(stderr, "%s [%d] * %s [%d] = %s\n",
            LongNum(f1).c_str(), len1, LongNum(f2).c_str(), len2,
            LongNum(r).c_str());
  }
  int sz = BigIntToLimbs(r, result);
  if (pResultLen != nullptr)
    *pResultLen = sz;

  // TODO: Compare against Karatsuba.
  // Otherwise, this might be because of some of the "caching" that fft.cc does?
}

