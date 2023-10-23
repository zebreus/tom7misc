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
#include "bigconv.h"

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

enum eExprErr BigIntRemainder(
    const BigInteger *pDividend,
    const BigInteger *pDivisor, BigInteger *pRemainder) {
  BigInt numer = BigIntegerToBigInt(pDividend);
  BigInt denom = BigIntegerToBigInt(pDivisor);
  if (BigInt::Eq(denom, 0)) {
    // Some code calls this with a 0 denominator. Not clear what it
    // wants the result to be, but make sure it's at least a valid
    // BigInteger. (?)
    CHECK(pRemainder->nbrLimbs > 0);
    return EXPR_DIVIDE_BY_ZERO;
  }
  // XXX: Some nearby code suggests that the original bignbr code might
  // have had different behavior for negative numbers. Check.

  // BigInt rem_ref = BigInt::QuotRem(numer, denom).second;
  BigInt rem = BigInt::CMod(numer, denom);
  // CHECK(BigInt::Eq(rem, rem_ref));
  BigIntToBigInteger(rem, pRemainder);
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

void BigIntDivide2(BigInteger *pArg) {
  int nbrLimbs = pArg->nbrLimbs;
  int ctr = nbrLimbs - 1;
  unsigned int carry;
  assert(nbrLimbs >= 1);
  limb *ptrLimb = &pArg->limbs[ctr];
  carry = 0;
  for (; ctr >= 0; ctr--) {
    carry = (carry << BITS_PER_GROUP) + (unsigned int)ptrLimb->x;
    ptrLimb->x = (int)(carry >> 1);
    ptrLimb--;
    carry &= 1;
  }
  if ((nbrLimbs > 1) && (pArg->limbs[nbrLimbs - 1].x == 0)) {
    // Most significant limb is zero, so reduce size by one limb.
    pArg->nbrLimbs--;
  }

  CHECK(pArg->nbrLimbs > 0);
}

enum eExprErr BigIntMultiplyPower2(BigInteger *pArg, int powerOf2) {
  int ctr;
  int nbrLimbs = pArg->nbrLimbs;
  assert(nbrLimbs >= 1);
  limb *ptrLimbs = pArg->limbs;
  int limbsToShiftLeft = powerOf2 / BITS_PER_GROUP;
  int bitsToShiftLeft = powerOf2 % BITS_PER_GROUP;
  if ((nbrLimbs + limbsToShiftLeft) >= MAX_LEN) {
    return EXPR_INTERM_TOO_HIGH;
  }

  for (; bitsToShiftLeft > 0; bitsToShiftLeft--) {
    unsigned int carry = 0U;
    for (ctr = 0; ctr < nbrLimbs; ctr++) {
      carry += (unsigned int)(ptrLimbs + ctr)->x << 1;
      (ptrLimbs + ctr)->x = UintToInt(carry & MAX_VALUE_LIMB);
      carry >>= BITS_PER_GROUP;
    }

    if (carry != 0U) {
      (ptrLimbs + ctr)->x = (int)carry;
      nbrLimbs++;
    }
  }

  nbrLimbs += limbsToShiftLeft;

  // Shift left entire limbs.
  if (limbsToShiftLeft > 0) {
    int bytesToMove = (nbrLimbs - limbsToShiftLeft) * (int)sizeof(limb);
    (void)memmove(&pArg->limbs[limbsToShiftLeft], pArg->limbs, bytesToMove);
    bytesToMove = limbsToShiftLeft * (int)sizeof(limb);
    (void)memset(pArg->limbs, 0, bytesToMove);
  }

  pArg->nbrLimbs = nbrLimbs;
  CHECK(pArg->nbrLimbs > 0);
  return EXPR_OK;
}

void BigIntGcd(const BigInteger *pArg1, const BigInteger *pArg2,
               BigInteger *pResult) {
  BigInt g = BigInt::GCD(BigIntegerToBigInt(pArg1),
                         BigIntegerToBigInt(pArg2));
  BigIntToBigInteger(g, pResult);
}

static void addToAbsValue(limb *pLimbs, int *pNbrLimbs, int addend) {
  limb* ptrLimbs = pLimbs;
  int nbrLimbs = *pNbrLimbs;
  ptrLimbs->x += addend;
  if ((unsigned int)ptrLimbs->x < LIMB_RANGE)
  {     // No overflow. Go out of routine.
    return;
  }
  ptrLimbs->x -= (int)LIMB_RANGE;
  for (int ctr = 1; ctr < nbrLimbs; ctr++)
  {
    ptrLimbs++;        // Point to next most significant limb.
    if (ptrLimbs->x != MAX_INT_NBR)
    {   // No overflow. Go out of routine.
      (ptrLimbs->x)++;   // Add carry.
      return;
    }
    ptrLimbs->x = 0;
  }
  (*pNbrLimbs)++;        // Result has an extra limb.
  (ptrLimbs + 1)->x = 1;   // Most significant limb must be 1.
}

static void subtFromAbsValue(limb *pLimbs, int *pNbrLimbs, int subt) {
  int nbrLimbs = *pNbrLimbs;
  limb* ptrLimb = pLimbs;
  pLimbs->x -= subt;
  if (pLimbs->x < 0)
  {
    int ctr = 0;
    do
    {      // Loop that adjust number if there is borrow.
      unsigned int tempLimb = (unsigned int)ptrLimb->x & MAX_VALUE_LIMB;
      ptrLimb->x = (int)tempLimb;
      ctr++;
      if (ctr == nbrLimbs)
      {    // All limbs processed. Exit loop.
        break;
      }
      ptrLimb++;                // Point to next most significant limb.
      ptrLimb->x--;
    } while (ptrLimb->x < 0);   // Continue loop if there is borrow.
    if ((nbrLimbs > 1) && ((pLimbs + nbrLimbs - 1)->x == 0))
    {
      nbrLimbs--;
    }
  }
  *pNbrLimbs = nbrLimbs;
}

void subtractdivide(BigInteger *pBigInt, int subt, int divisor) {
  int nbrLimbs = pBigInt->nbrLimbs;
  assert(nbrLimbs >= 1);
  // Point to most significant limb.
  double dDivisor = (double)divisor;
  double dInvDivisor = 1.0 / dDivisor;
  double dLimb = (double)LIMB_RANGE;

  if (subt >= 0) {
    if (pBigInt->sign == SIGN_POSITIVE) {
      // Subtract subt to absolute value.
      subtFromAbsValue(pBigInt->limbs, &nbrLimbs, subt);
    } else {
      // Add subt to absolute value.
      addToAbsValue(pBigInt->limbs, &nbrLimbs, subt);
    }
  } else {
    if (pBigInt->sign == SIGN_POSITIVE) {
      // Subtract subt to absolute value.
      addToAbsValue(pBigInt->limbs, &nbrLimbs, -subt);
    } else {
      // Add subt to absolute value.
      subtFromAbsValue(pBigInt->limbs, &nbrLimbs, -subt);
    }
  }

  if (divisor == 2) {
    // Use shifts for divisions by 2.
    limb* ptrDest = pBigInt->limbs;
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

  } else {
    int remainder = 0;
    limb* pLimbs = pBigInt->limbs + nbrLimbs - 1;

    // Divide number by divisor.
    for (int ctr = nbrLimbs - 1; ctr >= 0; ctr--) {
      unsigned int dividend = ((unsigned int)remainder << BITS_PER_GROUP) +
        (unsigned int)pLimbs->x;
      double dDividend = ((double)remainder * dLimb) + (double)pLimbs->x;
      double dQuotient = (dDividend * dInvDivisor) + 0.5;
      // quotient has correct value or 1 more.
      unsigned int quotient = (unsigned int)dQuotient;
      remainder = UintToInt(dividend - (quotient * (unsigned int)divisor));
      if (remainder < 0) {
        // remainder not in range 0 <= remainder < divisor. Adjust.
        quotient--;
        remainder += divisor;
      }
      pLimbs->x = (int)quotient;
      pLimbs--;
    }
  }

  if ((nbrLimbs > 1) && (pBigInt->limbs[nbrLimbs - 1].x == 0)) {
    // Most significant limb is now zero, so discard it.
    nbrLimbs--;
  }
  pBigInt->nbrLimbs = nbrLimbs;
  CHECK(pBigInt->nbrLimbs > 0);
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

// Copies a fixed-width array of limbs to the bigint. Removes
// high limbs that are 0 (which are trailing in little-endian
// representation). I think this is "Uncompress" in the sense
// that BigInteger has a fixed buffer large enough for "any number",
// but in a way it is actually compression since the fixed-width
// represents zero high limbs, but this does not.
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
  } else {
    int numberLengthBytes = number_length * (int)sizeof(limb);
    int nbrLimbs = bigint->nbrLimbs;
    assert(nbrLimbs >= 1);
    if (nbrLimbs > number_length) {
      (void)memcpy(ptrValues, bigint->limbs, numberLengthBytes);
    } else {
      int nbrLimbsBytes = nbrLimbs * (int)sizeof(limb);
      (void)memcpy(ptrValues, bigint->limbs, nbrLimbsBytes);
      nbrLimbsBytes = numberLengthBytes - nbrLimbsBytes;
      (void)memset(ptrValues + nbrLimbs, 0, nbrLimbsBytes);
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

void BigIntMultiplyBy2(BigInteger *nbr) {
  unsigned int prevLimb;
  limb *ptrDest = &nbr->limbs[0];
  int nbrLimbs = nbr->nbrLimbs;
  assert(nbrLimbs >= 1);
  prevLimb = 0U;
  for (int ctr = 0; ctr < nbrLimbs; ctr++) {
    // Process starting from least significant limb.
    unsigned int curLimb = (unsigned int)ptrDest->x;
    ptrDest->x = UintToInt(((curLimb << 1) |
                            (prevLimb >> BITS_PER_GROUP_MINUS_1)) &
                           MAX_VALUE_LIMB);
    ptrDest++;
    prevLimb = curLimb;
  }

  if ((prevLimb & HALF_INT_RANGE_U) != 0U) {
    ptrDest->x = 1;
    nbr->nbrLimbs++;
  }
}

// Find power of 2 that divides the number.
// output: pNbrLimbs = pointer to number of limbs
//         pShRight = pointer to power of 2.
void DivideBigNbrByMaxPowerOf2(int *pShRight, limb *number, int *pNbrLimbs) {
  int power2 = 0;
  int index;
  int index2;
  unsigned int shRight;
  unsigned int shLeft;
  int nbrLimbs = *pNbrLimbs;
  assert(nbrLimbs >= 1);

  // Start from least significant limb (number zero).
  for (index = 0; index < nbrLimbs; index++) {
    if (number[index].x != 0) {
      break;
    }
    power2 += BITS_PER_GROUP;
  }

  if (index == nbrLimbs) {
    // Input number is zero.
    *pShRight = power2;
    return;
  }

  for (unsigned int mask = 1U; mask <= MAX_VALUE_LIMB; mask *= 2) {
    if (((unsigned int)number[index].x & mask) != 0U) {
      break;
    }
    power2++;
  }

  // Divide number by this power.
  shRight = (unsigned int)power2 % (unsigned int)BITS_PER_GROUP; // Shift right bit counter

  if (((unsigned int)number[nbrLimbs - 1].x & (0U - (1U << shRight))) != 0U) {
    // Most significant bits set.
    *pNbrLimbs = nbrLimbs - index;
  } else {
    // Most significant bits not set.
    *pNbrLimbs = nbrLimbs - index - 1;
  }
      // Move number shRg bits to the right.
  shLeft = (unsigned int)BITS_PER_GROUP - shRight;
  for (index2 = index; index2 < (nbrLimbs-1); index2++) {
    number[index2].x = UintToInt((((unsigned int)number[index2].x >> shRight) |
                        ((unsigned int)number[index2+1].x << shLeft)) &
                        MAX_VALUE_LIMB);
  }

  if (index2 < nbrLimbs) {
    number[index2].x = UintToInt(((unsigned int)number[index2].x >> shRight)
      & MAX_VALUE_LIMB);
  }

  if (index > 0) {
    // Move limbs to final position.
    int lenBytes = (nbrLimbs - index) * (int)sizeof(limb);
    (void)memmove(number, &number[index], lenBytes);
  }

  *pShRight = power2;
}

bool BigIntIsZero(const BigInteger *value) {
  return value->nbrLimbs == 1 && value->limbs[0].x == 0;
}

bool BigIntIsOne(const BigInteger* value) {
  if ((value->nbrLimbs == 1) && (value->limbs[0].x == 1) && (value->sign == SIGN_POSITIVE))
  {
    return true;     // Number is zero.
  }
  return false;      // Number is not zero.
}

bool BigIntEqual(const BigInteger *value1, const BigInteger *value2) {
  int nbrLimbs;
  const limb *ptrValue1;
  const limb *ptrValue2;
  assert(value1->nbrLimbs >= 1);
  assert(value2->nbrLimbs >= 1);
  if ((value1->nbrLimbs != value2->nbrLimbs) ||
      (value1->sign != value2->sign)) {
    return false;    // Numbers are not equal.
  }
  nbrLimbs = value1->nbrLimbs;
  ptrValue1 = value1->limbs;
  ptrValue2 = value2->limbs;

  for (int index = 0; index < nbrLimbs; index++) {
    if (ptrValue1->x != ptrValue2->x) {
      return false;  // Numbers are not equal.
    }
    ptrValue1++;
    ptrValue2++;
  }
  return true;       // Numbers are equal.
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

int BigIntJacobiSymbol(const BigInteger *upper, const BigInteger *lower) {
  BigInt a = BigIntegerToBigInt(upper);
  BigInt b = BigIntegerToBigInt(lower);
  return BigInt::Jacobi(a, b);
}

void SquareRoot(const limb *argument, limb *sqRoot, int len, int *pLenSqRoot) {
  BigInt a = LimbsToBigInt(argument, len);
  BigInt r = BigInt::Sqrt(a);

  *pLenSqRoot = BigIntToLimbs(r, sqRoot);
}

enum eExprErr BigIntDivide(const BigInteger *pDividend,
                           const BigInteger *pDivisor,
                           BigInteger *pQuotient) {
  BigInt denom = BigIntegerToBigInt(pDivisor);
  if (BigInt::Eq(denom, 0)) {
    return EXPR_DIVIDE_BY_ZERO;
  }

  BigInt numer = BigIntegerToBigInt(pDividend);

  BigInt quot = BigInt::Div(numer, denom);
  BigIntToBigInteger(quot, pQuotient);

  return EXPR_OK;
}

static constexpr bool VERBOSE = false;

void multiply(const limb* factor1, const limb* factor2, limb* result,
              int len, int* pResultLen) {
  multiplyWithBothLen(factor1, factor2, result, len, len, pResultLen);
}

void multiplyWithBothLen(const limb* factor1, const limb* factor2, limb* result,
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
}

void ChSignBigNbr(limb *nbr, int length) {
  int carry = 0;
  const limb *ptrEndNbr = nbr + length;
  for (limb *ptrNbr = nbr; ptrNbr < ptrEndNbr; ptrNbr++) {
    carry -= ptrNbr->x;
    ptrNbr->x = carry & MAX_INT_NBR;
    carry >>= BITS_PER_GROUP;
  }
}

void AddBigNbr(const limb *pNbr1, const limb*pNbr2, limb*pSum, int nbrLen) {
  unsigned int carry = 0U;
  const limb*ptrNbr1 = pNbr1;
  const limb*ptrNbr2 = pNbr2;
  const limb*ptrEndSum = pSum + nbrLen;
  for (limb*ptrSum = pSum; ptrSum < ptrEndSum; ptrSum++) {
    unsigned int tmp;
    carry = (carry >> BITS_PER_GROUP) + (unsigned int)ptrNbr1->x +
      (unsigned int)ptrNbr2->x;
    tmp = carry & MAX_INT_NBR_U;
    ptrSum->x = (int)tmp;
    ptrNbr1++;
    ptrNbr2++;
  }
}

void SubtractBigNbr(const limb *pNbr1, const limb*pNbr2,
                    limb*pDiff, int nbrLen) {
  unsigned int borrow = 0U;
  const limb*ptrNbr1 = pNbr1;
  const limb*ptrNbr2 = pNbr2;
  const limb*ptrEndDiff = pDiff + nbrLen;
  for (limb*ptrDiff = pDiff; ptrDiff < ptrEndDiff; ptrDiff++)
  {
    unsigned int tmp;
    borrow = (unsigned int)ptrNbr1->x - (unsigned int)ptrNbr2->x -
      (borrow >> BITS_PER_GROUP);
    tmp = borrow & MAX_INT_NBR_U;
    ptrDiff->x = (int)tmp;
    ptrNbr1++;
    ptrNbr2++;
  }
}

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

void MultBigNbr(const limb *pFactor1, const limb *pFactor2,
                limb *pProd, int nbrLen) {
  limb* ptrProd = pProd;
  double dRangeLimb = (double)(1U << BITS_PER_GROUP);
  double dInvRangeLimb = 1.0 / dRangeLimb;
  int low = 0;
  double dAccumulator = 0.0;
  for (int i = 0; i < nbrLen; i++) {
    for (int j = 0; j <= i; j++) {
      int factor1 = (pFactor1 + j)->x;
      int factor2 = (pFactor2 + i - j)->x;
      low += factor1*factor2;
      dAccumulator += (double)factor1 * (double)factor2;
    }
    low &= MAX_INT_NBR;    // Trim extra bits.
    ptrProd->x = low;
    ptrProd++;

    // Subtract or add 0x20000000 so the multiplication by dVal is not
    // nearly an integer. In that case, there would be an error of +/- 1.
    if (low < HALF_INT_RANGE) {
      dAccumulator =
        floor((dAccumulator + (double)FOURTH_INT_RANGE)*dInvRangeLimb);
    } else {
      dAccumulator =
        floor((dAccumulator - (double)FOURTH_INT_RANGE)*dInvRangeLimb);
    }
    low = (int)(dAccumulator - floor(dAccumulator * dInvRangeLimb) * dRangeLimb);
  }
  ptrProd->x = low;
  (ptrProd+1)->x = (int)floor(dAccumulator/dRangeLimb);
}
