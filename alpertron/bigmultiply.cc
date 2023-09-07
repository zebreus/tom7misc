
#include "bigmultiply.h"

#include "bignbr.h"
#include "bignum/big.h"
#include "bigconv.h"

void BigMultiply(const limb* factor1, const limb* factor2, limb* result,
                 int len, int* pResultLen) {
  BigMultiplyWithBothLen(factor1, factor2, result, len, len, pResultLen);
}

void BigMultiplyWithBothLen(const limb* factor1, const limb* factor2, limb* result,
                            int len1, int len2, int* pResultLen) {
  BigInt f1 = LimbsToBigInt(factor1, len1);
  BigInt f2 = LimbsToBigInt(factor2, len2);
  BigInt r = BigInt::Times(f1, f2);
  *pResultLen = BigIntToLimbs(r, result);

  // TODO: Compare against Karatsuba.
  // Otherwise, this might be because of some of the "caching" that fft.cc does?
}

