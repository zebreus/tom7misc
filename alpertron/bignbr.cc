//
// This file is part of Alpertron Calculators.
//
// Copyright 2015-2021 Dario Alejandro Alpern
//
// Alpertron Calculators is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Alpertron Calculators is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Alpertron Calculators.  If not, see <http://www.gnu.org/licenses/>.
//
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#include "bignbr.h"
#include "modmult.h"

#include "base/logging.h"
#include "base/stringprintf.h"
#include "bigconv.h"

// XXX to bignum-util or something
static std::string LongNum(const BigInt &a) {
  std::string sign = BigInt::Less(a, 0) ? "-" : "";
  std::string num = BigInt::Abs(a).ToString();
  if (num.size() > 80) {
    static constexpr int SHOW_SIDE = 8;
    int skipped = num.size() - (SHOW_SIDE * 2);
    return StringPrintf("%s%s…(%d)…%s",
                        sign.c_str(),
                        num.substr(0, SHOW_SIDE).c_str(),
                        skipped,
                        num.substr(num.size() - SHOW_SIDE,
                                   std::string::npos).c_str());
  } else if (num.size() < 7) {
    return sign + num;
  } else {
    // with commas.
    std::string out;
    while (num.size() > 3) {
      if (!out.empty()) out = "," + out;
      out = num.substr(num.size() - 3, std::string::npos) + out;
      num.resize(num.size() - 3);
    }
    CHECK(!num.empty());
    CHECK(!out.empty());
    return sign + num + "," + out;
  }
}

void CopyBigInt(BigInteger *pDest, const BigInteger *pSrc) {
  if (pDest != pSrc) {
    int lenBytes;
    if (pSrc->nbrLimbs < 1) {
      fprintf(stderr, "nbrlimbs: %d\n", pSrc->nbrLimbs);
      assert(pSrc->nbrLimbs >= 1);
    }

    pDest->sign = pSrc->sign;
    pDest->nbrLimbs = pSrc->nbrLimbs;
    lenBytes = (pSrc->nbrLimbs) * (int)sizeof(limb);
    (void)memcpy(pDest->limbs, pSrc->limbs, lenBytes);
  }
}

void AddBigInt(const limb *pAddend1, const limb *pAddend2,
               limb *pSum, int nbrLimbs) {
  const limb *ptrAddend1 = pAddend1;
  const limb *ptrAddend2 = pAddend2;
  limb *ptrSum = pSum;
  assert(nbrLimbs >= 1);
  unsigned int carry = 0;
  for (int i = 0; i < nbrLimbs; i++) {
    carry = (carry >> BITS_PER_GROUP) + (unsigned int)ptrAddend1->x +
                                        (unsigned int)ptrAddend2->x;
    ptrAddend1++;
    ptrAddend2++;
    ptrSum->x = (int)carry & MAX_INT_NBR;
    ptrSum++;
  }
}

void BigIntChSign(BigInteger *value) {
  if ((value->nbrLimbs == 1) && (value->limbs[0].x == 0)) {
    // Value is zero. Do not change sign.
    return;
  }
  if (value->sign == SIGN_POSITIVE) {
    value->sign = SIGN_NEGATIVE;
  } else {
    value->sign = SIGN_POSITIVE;
  }
}

void BigIntAdd(const BigInteger* pAddend1, const BigInteger* pAddend2,
               BigInteger* pSum) {
  BigInt r = BigInt::Plus(BigIntegerToBigInt(pAddend1),
                          BigIntegerToBigInt(pAddend2));
  BigIntToBigInteger(r, pSum);
}

void BigIntNegate(const BigInteger *pSrc, BigInteger *pDest) {
  if (pSrc != pDest) {
    CopyBigInt(pDest, pSrc);
  }
  BigIntChSign(pDest);
}

void BigIntSubt(const BigInteger *pMinuend, const BigInteger *pSubtrahend,
                BigInteger *pDifference) {
  BigInt r = BigInt::Minus(BigIntegerToBigInt(pMinuend),
                           BigIntegerToBigInt(pSubtrahend));
  BigIntToBigInteger(r, pDifference);
}

enum eExprErr BigIntMultiply(const BigInteger *pFact1, const BigInteger *pFact2,
                             BigInteger *pProduct) {
  // Port note: This used to return EXP_INTERM_TOO_HIGH if the product is too
  // big to fit, but this return value is never checked. So I think we should
  // just abort if big int conversion fails. (And eventually just use native
  // BigInt.)
  BigInt r = BigInt::Times(BigIntegerToBigInt(pFact1),
                           BigIntegerToBigInt(pFact2));
  BigIntToBigInteger(r, pProduct);
  return EXPR_OK;
}

void intToBigInteger(BigInteger *bigint, int value) {
  if (value >= 0) {
    bigint->limbs[0].x = value;
    bigint->sign = SIGN_POSITIVE;
  } else {
    bigint->limbs[0].x = -value;
    bigint->sign = SIGN_NEGATIVE;
  }
  bigint->nbrLimbs = 1;
}

enum eExprErr BigIntPowerIntExp(const BigInteger *pBase, int exponent,
                                BigInteger *pPower) {
  CHECK(exponent >= 0);
  BigInt r = BigInt::Pow(BigIntegerToBigInt(pBase), exponent);
  BigIntToBigInteger(r, pPower);
  return EXPR_OK;
}

enum eExprErr BigIntPower(const BigInteger *pBase, const BigInteger *pExponent,
                          BigInteger *pPower) {
  BigInt a = BigIntegerToBigInt(pBase);
  BigInt e = BigIntegerToBigInt(pExponent);

  if (BigInt::Less(e, 0)) {
    return EXPR_INVALID_PARAM;
  }

  if (BigInt::Eq(a, 0) || BigInt::Eq(a, 1)) {
    BigIntToBigInteger(a, pPower);
    return EXPR_OK;
  }

  if (BigInt::Eq(a, -1)) {
    if (e.IsOdd()) {
      BigIntToBigInteger(a, pPower);
      return EXPR_OK;
    } else {
      BigInt one(1);
      BigIntToBigInteger(one, pPower);
      return EXPR_OK;
    }
  }

  // Otherwise, if the exponent is too big, we can return TOO_HIGH.
  std::optional<int64_t> oexponent = e.ToInt();
  if (!oexponent.has_value()) return EXPR_INTERM_TOO_HIGH;
  const int64_t exponent = oexponent.value();

  BigInt r = BigInt::Pow(a, exponent);
  BigIntToBigInteger(r, pPower);

  return EXPR_OK;
}

void BigIntGcd(const BigInteger *pArg1, const BigInteger *pArg2,
               BigInteger *pResult) {
  BigInt g = BigInt::GCD(BigIntegerToBigInt(pArg1),
                         BigIntegerToBigInt(pArg2));
  BigIntToBigInteger(g, pResult);
}

void addbigint(BigInteger *pResult, int addend) {
  BigInt a = BigInt::Plus(BigIntegerToBigInt(pResult), addend);
  // reference_addbigint(pResult, addend);
  // CHECK(BigInt::Eq(a, BigIntegerToBigInt(pResult)));
  BigIntToBigInteger(a, pResult);
}

void MultInt(BigInteger *pResult, const BigInteger *pMult, int factor) {
  BigInt a = BigIntegerToBigInt(pMult);
  BigInt r = BigInt::Times(a, factor);
  BigIntToBigInteger(r, pResult);
}

void multadd(BigInteger *pResult, int factor,
             const BigInteger *pMult, int addend) {
  BigInt a = BigIntegerToBigInt(pMult);
  BigInt r = BigInt::Plus(BigInt::Times(a, factor), addend);
  BigIntToBigInteger(r, pResult);
}

// number_length here is used to zero-pad the output -tom7
void IntArray2BigInteger(int number_length, const int *ptrValues,
                         BigInteger *bigint) {
  const int* piValues = ptrValues;
  limb *destLimb = bigint->limbs;
  int nbrLimbs = *piValues;
  piValues++;
  if (nbrLimbs > 0) {
    bigint->sign = SIGN_POSITIVE;
  } else {
    bigint->sign = SIGN_NEGATIVE;
    nbrLimbs = -nbrLimbs;
  }
  if (number_length == 1) {
    destLimb->x = *piValues;
    bigint->nbrLimbs = 1;
  } else {
    int ctr;
    bigint->nbrLimbs = nbrLimbs;
    for (ctr = 0; ctr < nbrLimbs; ctr++) {
      destLimb->x = *piValues;
      destLimb++;
      piValues++;
    }
    for (; ctr < number_length; ctr++) {
      destLimb->x = 0;
      destLimb++;
    }
  }
  CHECK(bigint->nbrLimbs > 0);
}

static int getNbrLimbs(int number_length, const limb *bigNbr) {
  const limb *ptrLimb = bigNbr + number_length;
  while (ptrLimb > bigNbr) {
    ptrLimb--;
    if (ptrLimb->x != 0) {
      return (int)(ptrLimb - bigNbr + 1);
    }
  }
  return 1;
}

void BigInteger2IntArray(int number_length,
                         /*@out@*/int *ptrValues, const BigInteger *bigint) {
  int* pValues = ptrValues;
  const limb *srcLimb = bigint->limbs;
  assert(number_length >= 1);

  if (number_length == 1) {
    *pValues = ((bigint->sign == SIGN_POSITIVE)? 1: -1);
    *(pValues + 1) = srcLimb->x;
  } else {
    int nbrLimbs;
    nbrLimbs = getNbrLimbs(number_length, srcLimb);
    *pValues = ((bigint->sign == SIGN_POSITIVE)? nbrLimbs : -nbrLimbs);
    pValues++;

    for (int ctr = 0; ctr < nbrLimbs; ctr++) {
      *pValues = srcLimb->x;
      pValues++;
      srcLimb++;
    }
  }
}

void UncompressLimbsBigInteger(int number_length,
                               const limb *ptrValues,
                               /*@out@*/BigInteger *bigint) {
  assert(number_length >= 1);
  if (number_length == 1) {
    bigint->limbs[0].x = ptrValues->x;
    bigint->nbrLimbs = 1;
  } else {
    int nbrLimbs;
    int numberLengthBytes = number_length * (int)sizeof(limb);
    (void)memcpy(bigint->limbs, ptrValues, numberLengthBytes);
    const limb *ptrValue1 = ptrValues + number_length;
    for (nbrLimbs = number_length; nbrLimbs > 1; nbrLimbs--) {
      ptrValue1--;
      if (ptrValue1->x != 0) {
        break;
      }
    }
    bigint->nbrLimbs = nbrLimbs;
  }

  // Port note: This didn't originally set the sign, but I think
  // that's just a bug. Note that a static BigInteger has positive
  // sign (0).
  bigint->sign = SIGN_POSITIVE;

  CHECK(bigint->nbrLimbs > 0);
}

void CompressLimbsBigInteger(int number_length,
                             /*@out@*/limb *ptrValues,
                             const BigInteger *bigint) {
  assert(number_length >= 1);
  if (number_length == 1) {
    ptrValues->x = bigint->limbs[0].x;
    // In this case, we never need any padding.
  } else {
    const int numberLengthBytes = number_length * (int)sizeof(limb);
    const int nbrLimbs = bigint->nbrLimbs;
    assert(nbrLimbs >= 1);
    if (nbrLimbs > number_length) {
      (void)memcpy(ptrValues, bigint->limbs, numberLengthBytes);
    } else {
      const int nbrLimbsBytes = nbrLimbs * (int)sizeof(limb);
      (void)memcpy(ptrValues, bigint->limbs, nbrLimbsBytes);
      // Padding.
      (void)memset(ptrValues + nbrLimbs, 0, numberLengthBytes - nbrLimbsBytes);
    }
  }
}

void BigIntDivideBy2(BigInteger *nbr) {
  int nbrLimbs = nbr->nbrLimbs;
  assert(nbrLimbs >= 1);
  limb *ptrDest = &nbr->limbs[0];
  unsigned int curLimb = (unsigned int)ptrDest->x;
  for (int ctr = 1; ctr < nbrLimbs; ctr++) {
    // Process starting from least significant limb.
    unsigned int nextLimb = (unsigned int)(ptrDest + 1)->x;
    ptrDest->x = UintToInt(((curLimb >> 1) |
                            (nextLimb << BITS_PER_GROUP_MINUS_1)) &
                           MAX_VALUE_LIMB);
    ptrDest++;
    curLimb = nextLimb;
  }
  ptrDest->x = UintToInt((curLimb >> 1) & MAX_VALUE_LIMB);
  if ((nbrLimbs > 1) && (nbr->limbs[nbrLimbs - 1].x == 0)) {
    nbr->nbrLimbs--;
  }
  CHECK(nbr->nbrLimbs > 0);
}

bool BigIntIsZero(const BigInteger *value) {
  return value->nbrLimbs == 1 && value->limbs[0].x == 0;
}

void BigIntPowerOf2(BigInteger *pResult, int exponent) {
  unsigned int power2 = (unsigned int)exponent % (unsigned int)BITS_PER_GROUP;
  int nbrLimbs = exponent / BITS_PER_GROUP;

  if (nbrLimbs > 0) {
    int nbrLimbsBytes = nbrLimbs * (int)sizeof(limb);
    (void)memset(pResult->limbs, 0, nbrLimbsBytes);
  }

  pResult->limbs[nbrLimbs].x = UintToInt(1U << power2);
  pResult->nbrLimbs = nbrLimbs + 1;
  pResult->sign = SIGN_POSITIVE;
  CHECK(pResult->nbrLimbs > 0);
}

void BigIntAnd(const BigInteger* arg1, const BigInteger* arg2,
               BigInteger* result) {
  BigInt a = BigIntegerToBigInt(arg1);
  BigInt b = BigIntegerToBigInt(arg2);
  BigInt r = BigInt::BitwiseAnd(a, b);
  BigIntToBigInteger(r, result);
}

static constexpr bool VERBOSE = false;

void MultiplyLimbs(const limb* factor1, const limb* factor2, limb* result,
                   int len) {
  // From a version that allowed these to differ. Clean up.
  const int len1 = len;
  const int len2 = len;

  // Note: Sometimes result is one of the factors. It works fine when
  // we copy into BigInt.
  BigInt f1 = LimbsToBigInt(factor1, len1);
  BigInt f2 = LimbsToBigInt(factor2, len2);
  BigInt r = BigInt::Times(f1, f2);

  // This is the actual size used; for example multiplying a huge number by
  // zero yields zero, which is stored in one limb. But Alpertron appears to
  // assume in some cases that the full possible width is zeroed.
  const int zerolen = len1 + len2;
  // PERF can use the return value from BigIntToLibs and just zero the
  // rest.
  memset(result, 0, zerolen * sizeof(int));

  if (VERBOSE) {
    fprintf(stderr, "%s [%d] * %s [%d] = %s\n",
            LongNum(f1).c_str(), len1,
            LongNum(f2).c_str(), len2,
            LongNum(r).c_str());
  }

  (void)BigIntToLimbs(r, result);
}

void ChSignLimbs(limb *nbr, int length) {
  int carry = 0;
  const limb *ptrEndNbr = nbr + length;
  for (limb *ptrNbr = nbr; ptrNbr < ptrEndNbr; ptrNbr++) {
    carry -= ptrNbr->x;
    ptrNbr->x = carry & MAX_INT_NBR;
    carry >>= BITS_PER_GROUP;
  }
}

void SubtractBigNbr(const limb *pNbr1, const limb *pNbr2,
                    limb *pDiff, int nbrLen) {
  unsigned int borrow = 0U;
  const limb *ptrNbr1 = pNbr1;
  const limb *ptrNbr2 = pNbr2;
  const limb *ptrEndDiff = pDiff + nbrLen;
  for (limb *ptrDiff = pDiff; ptrDiff < ptrEndDiff; ptrDiff++) {
    borrow = (unsigned int)ptrNbr1->x -
      (unsigned int)ptrNbr2->x -
      (borrow >> BITS_PER_GROUP);
    unsigned int tmp = borrow & MAX_INT_NBR_U;
    ptrDiff->x = (int)tmp;
    ptrNbr1++;
    ptrNbr2++;
  }
}

// only called with constant divisor=2
void DivBigNbrByInt(const limb *pDividend, int divisor, limb *pQuotient, int nbrLen) {
  const limb* ptrDividend = pDividend;
  limb* ptrQuotient = pQuotient;
  unsigned int remainder = 0U;
  double dDivisor = (double)divisor;
  double dLimb = 2147483648.0;
  int nbrLenMinus1 = nbrLen - 1;
  ptrDividend += nbrLenMinus1;
  ptrQuotient += nbrLenMinus1;
  for (int ctr = nbrLenMinus1; ctr >= 0; ctr--) {
    unsigned int dividend = (remainder << BITS_PER_GROUP) +
      (unsigned int)ptrDividend->x;
    double dDividend = ((double)remainder * dLimb) + (double)ptrDividend->x;
    // quotient has correct value or 1 more.
    unsigned int quotient = (unsigned int)((dDividend / dDivisor) + 0.5);
    remainder = dividend - (quotient * (unsigned int)divisor);
    if (remainder >= (unsigned int)divisor)
    {     // remainder not in range 0 <= remainder < divisor. Adjust.
      quotient--;
      remainder += (unsigned int)divisor;
    }
    ptrQuotient->x = (int)quotient;
    ptrQuotient--;
    ptrDividend--;
  }
}

