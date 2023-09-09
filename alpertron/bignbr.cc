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
#include "multiply.h"
#include "modmult.h"

#define LOG_2            0.69314718055994531

void CopyBigInt(BigInteger *pDest, const BigInteger *pSrc) {
  if (pDest != pSrc)
  {
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

void AddBigInt(const limb *pAddend1, const limb *pAddend2, limb *pSum, int nbrLimbs)
{
  const limb *ptrAddend1 = pAddend1;
  const limb *ptrAddend2 = pAddend2;
  limb *ptrSum = pSum;
  assert(nbrLimbs >= 1);
  unsigned int carry = 0;
  for (int i = 0; i < nbrLimbs; i++)
  {
    carry = (carry >> BITS_PER_GROUP) + (unsigned int)ptrAddend1->x +
                                        (unsigned int)ptrAddend2->x;
    ptrAddend1++;
    ptrAddend2++;
    ptrSum->x = (int)carry & MAX_INT_NBR;
    ptrSum++;
  }
}

// If address of num and result match, BigIntDivide will overwrite num, so it must be executed after processing num.
void floordiv(const BigInteger *num, const BigInteger *den, BigInteger *result) {
  BigInteger rem;
  (void)BigIntRemainder(num, den, &rem);
  if ((((num->sign == SIGN_NEGATIVE) && (den->sign == SIGN_POSITIVE)) ||
    ((num->sign == SIGN_POSITIVE) && !BigIntIsZero(num) && (den->sign == SIGN_NEGATIVE))) && !BigIntIsZero(&rem))
  {
    (void)BigIntDivide(num, den, result);
    addbigint(result, -1);
  }
  else
  {
    (void)BigIntDivide(num, den, result);
  }
}

void BigIntChSign(BigInteger *value)
{
  if ((value->nbrLimbs == 1) && (value->limbs[0].x == 0))
  {    // Value is zero. Do not change sign.
    return;
  }
  if (value->sign == SIGN_POSITIVE)
  {
    value->sign = SIGN_NEGATIVE;
  }
  else
  {
    value->sign = SIGN_POSITIVE;
  }
}

static void InternalBigIntAdd(const BigInteger *pAdd1, const BigInteger *pAdd2,
  BigInteger *pSum, enum eSign addend2sign)
{
  const BigInteger* pAddend1 = pAdd1;
  const BigInteger* pAddend2 = pAdd2;
  int ctr;
  int nbrLimbs;
  const limb *ptrAddend1;
  const limb *ptrAddend2;
  limb *ptrSum;
  const BigInteger *pTemp;
  enum eSign addend1Sign = pAddend1->sign;
  enum eSign addend2Sign = addend2sign;
  enum eSign tmpSign;
  assert(pAddend1->nbrLimbs >= 1);
  assert(pAddend2->nbrLimbs >= 1);
  if (pAddend1->nbrLimbs < pAddend2->nbrLimbs)
  {
    tmpSign = addend1Sign;
    addend1Sign = addend2Sign;
    addend2Sign = tmpSign;
    pTemp = pAddend1;
    pAddend1 = pAddend2;
    pAddend2 = pTemp;
  }           // At this moment, the absolute value of addend1 is greater than
              // or equal than the absolute value of addend2.
  else if (pAddend1->nbrLimbs == pAddend2->nbrLimbs)
  {
    for (ctr = pAddend1->nbrLimbs - 1; ctr >= 0; ctr--)
    {
      if (pAddend1->limbs[ctr].x != pAddend2->limbs[ctr].x)
      {
        break;
      }
    }
    if ((ctr >= 0) && (pAddend1->limbs[ctr].x < pAddend2->limbs[ctr].x))
    {
      tmpSign = addend1Sign;
      addend1Sign = addend2Sign;
      addend2Sign = tmpSign;
      pTemp = pAddend1;
      pAddend1 = pAddend2;
      pAddend2 = pTemp;
    }           // At this moment, the absolute value of addend1 is greater than
                // or equal than the absolute value of addend2.
  }
  else
  {             // Nothing to do.
  }
  nbrLimbs = pAddend2->nbrLimbs;
  ptrAddend1 = pAddend1->limbs;
  ptrAddend2 = pAddend2->limbs;
  ptrSum = pSum->limbs;
  if (addend1Sign == addend2Sign)
  {             // Both addends have the same sign. Sum their absolute values.
    unsigned int carry = 0;
    unsigned int limbValue;
    for (ctr = 0; ctr < nbrLimbs; ctr++)
    {
      carry = (carry >> BITS_PER_GROUP) + (unsigned int)ptrAddend1->x +
        (unsigned int)ptrAddend2->x;
      ptrAddend1++;
      ptrAddend2++;
      limbValue = carry & MAX_INT_NBR_U;
      ptrSum->x = (int)limbValue;
      ptrSum++;
    }
    nbrLimbs = pAddend1->nbrLimbs;
    for (; ctr < nbrLimbs; ctr++)
    {
      carry = (carry >> BITS_PER_GROUP) + (unsigned int)ptrAddend1->x;
      ptrAddend1++;
      limbValue = carry & MAX_INT_NBR_U;
      ptrSum->x = (int)limbValue;
      ptrSum++;
    }
    if (carry >= LIMB_RANGE)
    {
      ptrSum->x = 1;
      nbrLimbs++;
    }
  }
  else
  {           // Both addends have different sign. Subtract their absolute values.
    unsigned int borrow = 0U;
    for (ctr = 0; ctr < nbrLimbs; ctr++)
    {
      borrow = (unsigned int)ptrAddend1->x - (unsigned int)ptrAddend2->x -
        (borrow >> BITS_PER_GROUP);
      ptrSum->x = UintToInt(borrow & MAX_VALUE_LIMB);
      ptrAddend1++;
      ptrAddend2++;
      ptrSum++;
    }
    nbrLimbs = pAddend1->nbrLimbs;
    for (; ctr < nbrLimbs; ctr++)
    {
      borrow = (unsigned int)ptrAddend1->x - (borrow >> BITS_PER_GROUP);
      ptrSum->x = UintToInt(borrow & MAX_VALUE_LIMB);
      ptrAddend1++;
      ptrSum++;
    }
    while ((nbrLimbs > 1) && (pSum->limbs[nbrLimbs - 1].x == 0))
    {     // Loop that deletes non-significant zeros.
      nbrLimbs--;
    }
  }
  pSum->nbrLimbs = nbrLimbs;
  pSum->sign = addend1Sign;
  if ((pSum->nbrLimbs == 1) && (pSum->limbs[0].x == 0))
  {          // Result is zero.
    pSum->sign = SIGN_POSITIVE;
  }
}

void BigIntAdd(const BigInteger* pAddend1, const BigInteger* pAddend2, BigInteger* pSum)
{
  InternalBigIntAdd(pAddend1, pAddend2, pSum, pAddend2->sign);
}

void BigIntNegate(const BigInteger *pSrc, BigInteger *pDest)
{
  if (pSrc != pDest)
  {
    CopyBigInt(pDest, pSrc);
  }
  BigIntChSign(pDest);
}

void BigIntSubt(const BigInteger *pMinuend, const BigInteger *pSubtrahend, BigInteger *pDifference)
{
  if (pSubtrahend->sign == SIGN_POSITIVE)
  {
    InternalBigIntAdd(pMinuend, pSubtrahend, pDifference, SIGN_NEGATIVE);
  }
  else
  {
    InternalBigIntAdd(pMinuend, pSubtrahend, pDifference, SIGN_POSITIVE);
  }
}

enum eExprErr BigIntMultiply(const BigInteger *pFact1, const BigInteger *pFact2, BigInteger *pProduct)
{
  const BigInteger* pFactor1 = pFact1;
  const BigInteger* pFactor2 = pFact2;
  int nbrLimbsFactor1 = pFactor1->nbrLimbs;
  int nbrLimbsFactor2 = pFactor2->nbrLimbs;
  int nbrLimbs;
  const BigInteger *temp;
  assert(pFactor1->nbrLimbs >= 1);
  assert(pFactor2->nbrLimbs >= 1);
  if ((pFactor1->nbrLimbs == 1) || (pFactor2->nbrLimbs == 1))
  {       // At least one the factors has only one limb.
    int factor2;
    if (pFactor1->nbrLimbs == 1)
    {     // Force the second factor to have only one limb.
      temp = pFactor1;
      pFactor1 = pFactor2;
      pFactor2 = temp;
    }
      // Multiply BigInteger by integer.
    factor2 = ((pFactor2->sign == SIGN_POSITIVE)? pFactor2->limbs[0].x : -pFactor2->limbs[0].x);
    multint(pProduct, pFactor1, factor2);
    return EXPR_OK;
  }
  // The maximum number that can be represented is 2^664380 ~ 10^200000
  if ((pFactor1->nbrLimbs + pFactor2->nbrLimbs) > ((664380 / BITS_PER_GROUP) + 1)) {
    return EXPR_INTERM_TOO_HIGH;
  }
  multiplyWithBothLen(&pFactor1->limbs[0], &pFactor2->limbs[0], &pProduct->limbs[0],
                      nbrLimbsFactor1, nbrLimbsFactor2, &nbrLimbs);
  nbrLimbs = nbrLimbsFactor1 + nbrLimbsFactor2;
  if (pProduct->limbs[nbrLimbs - 1].x == 0)
  {
    nbrLimbs--;
  }
  pProduct->nbrLimbs = nbrLimbs;
  if ((nbrLimbs == 1) && (pProduct->limbs[0].x == 0))
  {
    pProduct->sign = SIGN_POSITIVE;
  }
  else
  {
    if (pFactor1->sign == pFactor2->sign)
    {
      pProduct->sign = SIGN_POSITIVE;
    }
    else
    {
      pProduct->sign = SIGN_NEGATIVE;
    }
  }
  return EXPR_OK;
}

enum eExprErr BigIntRemainder(const BigInteger *pDividend,
  const BigInteger *pDivisor, BigInteger *pRemainder)
{
  enum eExprErr rc;
  assert(pDividend->nbrLimbs >= 1);
  assert(pDivisor->nbrLimbs >= 1);
  if (BigIntIsZero(pDivisor))
  {   // If divisor = 0, then remainder is the dividend.
    CopyBigInt(pRemainder, pDividend);
    return EXPR_OK;
  }
  BigInteger Temp2, Base;
  CopyBigInt(&Temp2, pDividend);
  rc = BigIntDivide(pDividend, pDivisor, &Base);   // Get quotient of division.
  if (rc != EXPR_OK)
  {
    return rc;
  }
  rc = BigIntMultiply(&Base, pDivisor, &Base);
  if (rc != EXPR_OK)
  {
    return rc;
  }
  BigIntSubt(&Temp2, &Base, pRemainder);
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

static double logBigNbr(const BigInteger *pBigNbr) {
  int nbrLimbs;
  double logar;
  nbrLimbs = pBigNbr->nbrLimbs;
  assert(nbrLimbs >= 1);
  if (nbrLimbs == 1)
  {
    logar = log((double)(pBigNbr->limbs[0].x));
  }
  else
  {
    int nbrBits;
    double value = pBigNbr->limbs[nbrLimbs - 2].x +
                  (double)pBigNbr->limbs[nbrLimbs - 1].x * LIMB_RANGE;
    if (nbrLimbs == 2)
    {
      logar = log(value);
    }
    else
    {
      logar = log(value + (double)pBigNbr->limbs[nbrLimbs - 3].x / LIMB_RANGE);
    }
    nbrBits = (nbrLimbs - 2) * BITS_PER_GROUP;
    logar += (double)nbrBits * LOG_2;
  }
  return logar;
}

enum eExprErr BigIntPowerIntExp(const BigInteger *pBase, int exponent, BigInteger *pPower)
{
  double base;
  enum eExprErr rc;
  assert(pBase->nbrLimbs >= 1);
  if (BigIntIsZero(pBase))
  {     // Base = 0 -> power = 0
    pPower->limbs[0].x = 0;
    pPower->nbrLimbs = 1;
    pPower->sign = SIGN_POSITIVE;
    return EXPR_OK;
  }
  base = logBigNbr(pBase);
  if (base*(double)exponent > 460510)
  {   // More than 20000 digits. 46051 = log(10^20000)
    return EXPR_INTERM_TOO_HIGH;
  }
  BigInteger Base;
  CopyBigInt(&Base, pBase);
  pPower->sign = SIGN_POSITIVE;
  pPower->nbrLimbs = 1;
  pPower->limbs[0].x = 1;
  for (unsigned int mask = HALF_INT_RANGE_U; mask != 0U; mask >>= 1)
  {
    if (((unsigned int)exponent & mask) != 0U)
    {
      for (unsigned int mask2 = mask; mask2 != 0U; mask2 >>= 1)
      {
        rc = BigIntMultiply(pPower, pPower, pPower);
        if (rc != EXPR_OK)
        {
          return rc;
        }
        if (((unsigned int)exponent & mask2) != 0U)
        {
          rc = BigIntMultiply(pPower, &Base, pPower);
          if (rc != EXPR_OK)
          {
            return rc;
          }
        }
      }
      break;
    }
  }
  return EXPR_OK;
}

enum eExprErr BigIntPower(const BigInteger *pBase, const BigInteger *pExponent, BigInteger *pPower)
{
  assert(pBase->nbrLimbs >= 1);
  assert(pExponent->nbrLimbs >= 1);
  if (pExponent->sign == SIGN_NEGATIVE)
  {     // Negative exponent not accepted.
    return EXPR_INVALID_PARAM;
  }
  if (pExponent->nbrLimbs > 1)
  {     // Exponent too high.
    if ((pBase->nbrLimbs == 1) && (pBase->limbs[0].x < 2))
    {   // If base equals -1, 0 or 1, set power to the value of base.
      pPower->limbs[0].x = pBase->limbs[0].x;
      pPower->nbrLimbs = 1;
      if ((pBase->sign == SIGN_NEGATIVE) && ((pExponent->limbs[0].x & 1) != 0))
      {   // Base negative and exponent odd means power negative.
        pPower->sign = SIGN_NEGATIVE;
      }
      else
      {
        pPower->sign = SIGN_POSITIVE;
      }
      return EXPR_OK;
    }
    return EXPR_INTERM_TOO_HIGH;
  }
  return BigIntPowerIntExp(pBase, pExponent->limbs[0].x, pPower);
}

// GCD of two numbers:
// Input: a, b positive integers
// Output : g and d such that g is odd and gcd(a, b) = g×2d
//   d : = 0
//   while a and b are both even do
//     a : = a / 2
//     b : = b / 2
//     d : = d + 1
//   while a != b do
//     if a is even then a : = a / 2
//     else if b is even then b : = b / 2
//     else if a > b then a : = (a – b) / 2
//     else b : = (b – a) / 2
//   g : = a
//     output g, d

void BigIntDivide2(BigInteger *pArg)
{
  int nbrLimbs = pArg->nbrLimbs;
  int ctr = nbrLimbs - 1;
  unsigned int carry;
  assert(nbrLimbs >= 1);
  limb *ptrLimb = &pArg->limbs[ctr];
  carry = 0;
  for (; ctr >= 0; ctr--)
  {
    carry = (carry << BITS_PER_GROUP) + (unsigned int)ptrLimb->x;
    ptrLimb->x = (int)(carry >> 1);
    ptrLimb--;
    carry &= 1;
  }
  if ((nbrLimbs > 1) && (pArg->limbs[nbrLimbs - 1].x == 0))
  {     // Most significant limb is zero, so reduce size by one limb.
    pArg->nbrLimbs--;
  }
}

enum eExprErr BigIntMultiplyPower2(BigInteger *pArg, int powerOf2)
{
  int ctr;
  int nbrLimbs = pArg->nbrLimbs;
  assert(nbrLimbs >= 1);
  limb *ptrLimbs = pArg->limbs;
  int limbsToShiftLeft = powerOf2 / BITS_PER_GROUP;
  int bitsToShiftLeft = powerOf2 % BITS_PER_GROUP;
  if ((nbrLimbs + limbsToShiftLeft) >= MAX_LEN)
  {
    return EXPR_INTERM_TOO_HIGH;
  }
  for (; bitsToShiftLeft > 0; bitsToShiftLeft--)
  {
    unsigned int carry = 0U;
    for (ctr = 0; ctr < nbrLimbs; ctr++)
    {
      carry += (unsigned int)(ptrLimbs + ctr)->x << 1;
      (ptrLimbs + ctr)->x = UintToInt(carry & MAX_VALUE_LIMB);
      carry >>= BITS_PER_GROUP;
    }
    if (carry != 0U)
    {
      (ptrLimbs + ctr)->x = (int)carry;
      nbrLimbs++;
    }
  }
  nbrLimbs += limbsToShiftLeft;
  // Shift left entire limbs.
  if (limbsToShiftLeft > 0)
  {
    int bytesToMove = (nbrLimbs - limbsToShiftLeft) * (int)sizeof(limb);
    (void)memmove(&pArg->limbs[limbsToShiftLeft], pArg->limbs, bytesToMove);
    bytesToMove = limbsToShiftLeft * (int)sizeof(limb);
    (void)memset(pArg->limbs, 0, bytesToMove);
  }
  pArg->nbrLimbs = nbrLimbs;
  return EXPR_OK;
}

static bool TestBigNbrEqual(const BigInteger *pNbr1, const BigInteger *pNbr2) {
  const limb *ptrLimbs1 = pNbr1->limbs;
  const limb *ptrLimbs2 = pNbr2->limbs;
  assert(pNbr1->nbrLimbs >= 1);
  assert(pNbr2->nbrLimbs >= 1);
  if (pNbr1->nbrLimbs != pNbr2->nbrLimbs)
  {        // Sizes of numbers are different.
    return false;
  }
  if (pNbr1->sign != pNbr2->sign)
  {        // Sign of numbers are different.
    if ((pNbr1->nbrLimbs == 1) && (pNbr1->limbs[0].x == 0) && (pNbr2->limbs[0].x == 0))
    {              // Both numbers are zero.
      return true;
    }
    return false;
  }

           // Check whether both numbers are equal.
  for (int ctr = pNbr1->nbrLimbs - 1; ctr >= 0; ctr--)
  {
    if ((ptrLimbs1 + ctr)->x != (ptrLimbs2 + ctr)->x)
    {      // Numbers are different.
      return false;
    }
  }        // Numbers are equal.
  return true;
}

void BigIntGcd(const BigInteger *pArg1, const BigInteger *pArg2, BigInteger *pResult)
{
  int power2;
  if (BigIntIsZero(pArg1))
  {               // First argument is zero, so the GCD is second argument.
    CopyBigInt(pResult, pArg2);
    return;
  }
  if (BigIntIsZero(pArg2))
  {               // Second argument is zero, so the GCD is first argument.
    CopyBigInt(pResult, pArg1);
    return;
  }
  BigInteger Base, Power;
  // Reuse Base and Power temporary variables.
  CopyBigInt(&Base, pArg1);
  CopyBigInt(&Power, pArg2);
  Base.sign = SIGN_POSITIVE;
  Power.sign = SIGN_POSITIVE;
  power2 = 0;
  while (((Base.limbs[0].x | Power.limbs[0].x) & 1) == 0)
  {  // Both values are even
    BigIntDivide2(&Base);
    BigIntDivide2(&Power);
    power2++;
  }
  while (TestBigNbrEqual(&Base, &Power) == 0)
  {    // Main GCD loop.
    if ((Base.limbs[0].x & 1) == 0)
    {          // Number is even. Divide it by 2.
      BigIntDivide2(&Base);
      continue;
    }
    if ((Power.limbs[0].x & 1) == 0)
    {          // Number is even. Divide it by 2.
      BigIntDivide2(&Power);
      continue;
    }
    BigIntSubt(&Base, &Power, pResult);
    if (pResult->sign == SIGN_POSITIVE)
    {
      CopyBigInt(&Base, pResult);
      BigIntDivide2(&Base);
    }
    else
    {
      CopyBigInt(&Power, pResult);
      Power.sign = SIGN_POSITIVE;
      BigIntDivide2(&Power);
    }
  }
  CopyBigInt(pResult, &Base);
  (void)BigIntMultiplyPower2(pResult, power2);
  pResult->sign = SIGN_POSITIVE;
}

static void addToAbsValue(limb *pLimbs, int *pNbrLimbs, int addend)
{
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

static void subtFromAbsValue(limb *pLimbs, int *pNbrLimbs, int subt)
{
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

void subtractdivide(BigInteger *pBigInt, int subt, int divisor)
{
  int nbrLimbs = pBigInt->nbrLimbs;
  assert(nbrLimbs >= 1);
  // Point to most significant limb.
  double dDivisor = (double)divisor;
  double dInvDivisor = 1.0 / dDivisor;
  double dLimb = (double)LIMB_RANGE;

  if (subt >= 0)
  {
    if (pBigInt->sign == SIGN_POSITIVE)
    {               // Subtract subt to absolute value.
      subtFromAbsValue(pBigInt->limbs, &nbrLimbs, subt);
    }
    else
    {               // Add subt to absolute value.
      addToAbsValue(pBigInt->limbs, &nbrLimbs, subt);
    }
  }
  else
  {
    if (pBigInt->sign == SIGN_POSITIVE)
    {               // Subtract subt to absolute value.
      addToAbsValue(pBigInt->limbs, &nbrLimbs, -subt);
    }
    else
    {               // Add subt to absolute value.
      subtFromAbsValue(pBigInt->limbs, &nbrLimbs, -subt);
    }
  }
  if (divisor == 2)
  {      // Use shifts for divisions by 2.
    limb* ptrDest = pBigInt->limbs;
    unsigned int curLimb = (unsigned int)ptrDest->x;
    for (int ctr = 1; ctr < nbrLimbs; ctr++)
    {  // Process starting from least significant limb.
      unsigned int nextLimb = (unsigned int)(ptrDest + 1)->x;
      ptrDest->x = UintToInt(((curLimb >> 1) | (nextLimb << BITS_PER_GROUP_MINUS_1)) &
        MAX_VALUE_LIMB);
      ptrDest++;
      curLimb = nextLimb;
    }
    ptrDest->x = UintToInt((curLimb >> 1) & MAX_VALUE_LIMB);
  }
  else
  {
    int remainder = 0;
    limb* pLimbs = pBigInt->limbs + nbrLimbs - 1;
    // Divide number by divisor.
    for (int ctr = nbrLimbs - 1; ctr >= 0; ctr--)
    {
      unsigned int dividend = ((unsigned int)remainder << BITS_PER_GROUP) +
        (unsigned int)pLimbs->x;
      double dDividend = ((double)remainder * dLimb) + (double)pLimbs->x;
      double dQuotient = (dDividend * dInvDivisor) + 0.5;
      unsigned int quotient = (unsigned int)dQuotient;   // quotient has correct value or 1 more.
      remainder = UintToInt(dividend - (quotient * (unsigned int)divisor));
      if (remainder < 0)
      {     // remainder not in range 0 <= remainder < divisor. Adjust.
        quotient--;
        remainder += divisor;
      }
      pLimbs->x = (int)quotient;
      pLimbs--;
    }
  }
  if ((nbrLimbs > 1) && (pBigInt->limbs[nbrLimbs - 1].x == 0))
  {   // Most significant limb is now zero, so discard it.
    nbrLimbs--;
  }
  pBigInt->nbrLimbs = nbrLimbs;
}

void addbigint(BigInteger *pResult, int addend)
{
  int intAddend = addend;
  enum eSign sign;
  int nbrLimbs = pResult->nbrLimbs;
  assert(nbrLimbs >= 1);
  limb *pResultLimbs = pResult->limbs;
  sign = pResult->sign;
  if (intAddend < 0)
  {
    intAddend = -intAddend;
    if (sign == SIGN_POSITIVE)
    {
      sign = SIGN_NEGATIVE;
    }
    else
    {
      sign = SIGN_POSITIVE;
    }
  }
  if (sign == SIGN_POSITIVE)
  {   // Add addend to absolute value of pResult.
    addToAbsValue(pResultLimbs, &nbrLimbs, intAddend);
  }
  else
  {  // Subtract addend from absolute value of pResult.
    if (nbrLimbs == 1)
    {
      pResultLimbs->x -= intAddend;
      if (pResultLimbs->x < 0)
      {
        pResultLimbs->x = -pResultLimbs->x;
        BigIntNegate(pResult, pResult);
      }
    }
    else
    {     // More than one limb.
      subtFromAbsValue(pResultLimbs, &nbrLimbs, intAddend);
    }
  }
  pResult->nbrLimbs = nbrLimbs;
}

void multint(BigInteger *pResult, const BigInteger *pMult, int factor)
{
  int64_t carry;
  int intMult = factor;
  bool factorPositive = true;
  int nbrLimbs = pMult->nbrLimbs;
  assert(nbrLimbs >= 1);
  const limb *pLimb = pMult->limbs;
  limb *pResultLimb = pResult->limbs;
  if (intMult == 0)
  {   // Any number multiplied by zero is zero.
    intToBigInteger(pResult, 0);
    return;
  }
  if (intMult < 0)
  {     // If factor is negative, indicate it and compute its absolute value.
    factorPositive = false;
    intMult = -intMult;
  }
  carry = 0;
  for (int ctr = 0; ctr < nbrLimbs; ctr++)
  {

    carry += (int64_t)pLimb->x * (int64_t)intMult;
    pResultLimb->x = UintToInt((unsigned int)carry & MAX_VALUE_LIMB);
    pResultLimb++;
    carry >>= BITS_PER_GROUP;

    pLimb++;
  }
  if (carry != 0)
  {
    pResultLimb->x = (int)carry;
    nbrLimbs++;
  }
  pResult->nbrLimbs = nbrLimbs;
  pResult->sign = pMult->sign;
  if (!factorPositive)
  {
    BigIntNegate(pResult, pResult);
  }
}

void multadd(BigInteger *pResult, int iMult, const BigInteger *pMult, int addend)
{
  multint(pResult, pMult, iMult);
  addbigint(pResult, addend);
}

// number_length here is used to zero-pad the output -tom7
void IntArray2BigInteger(int number_length, const int *ptrValues, BigInteger *bigint) {
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
  if (number_length == 1)
  {
    *pValues = ((bigint->sign == SIGN_POSITIVE)? 1: -1);
    *(pValues + 1) = srcLimb->x;
  }
  else
  {
    int nbrLimbs;
    nbrLimbs = getNbrLimbs(number_length, srcLimb);
    *pValues = ((bigint->sign == SIGN_POSITIVE)? nbrLimbs : -nbrLimbs);
    pValues++;
    for (int ctr = 0; ctr < nbrLimbs; ctr++)
    {
      *pValues = srcLimb->x;
      pValues++;
      srcLimb++;
    }
  }
}

void UncompressLimbsBigInteger(int number_length,
                               const limb *ptrValues, /*@out@*/BigInteger *bigint) {
  assert(number_length >= 1);
  if (number_length == 1)
  {
    bigint->limbs[0].x = ptrValues->x;
    bigint->nbrLimbs = 1;
  }
  else
  {
    int nbrLimbs;
    const limb *ptrValue1;
    int numberLengthBytes = number_length * (int)sizeof(limb);
    (void)memcpy(bigint->limbs, ptrValues, numberLengthBytes);
    ptrValue1 = ptrValues + number_length;
    for (nbrLimbs = number_length; nbrLimbs > 1; nbrLimbs--)
    {
      ptrValue1--;
      if (ptrValue1->x != 0)
      {
        break;
      }
    }
    bigint->nbrLimbs = nbrLimbs;
  }
}

void CompressLimbsBigInteger(int number_length,
                             /*@out@*/limb *ptrValues, const BigInteger *bigint) {
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
  for (int ctr = 1; ctr < nbrLimbs; ctr++)
  {  // Process starting from least significant limb.
    unsigned int nextLimb = (unsigned int)(ptrDest + 1)->x;
    ptrDest->x = UintToInt(((curLimb >> 1) | (nextLimb << BITS_PER_GROUP_MINUS_1)) &
      MAX_VALUE_LIMB);
    ptrDest++;
    curLimb = nextLimb;
  }
  ptrDest->x = UintToInt((curLimb >> 1) & MAX_VALUE_LIMB);
  if ((nbrLimbs > 1) && (nbr->limbs[nbrLimbs - 1].x == 0))
  {
    nbr->nbrLimbs--;
  }
}

void BigIntMultiplyBy2(BigInteger *nbr)
{
  unsigned int prevLimb;
  limb *ptrDest = &nbr->limbs[0];
  int nbrLimbs = nbr->nbrLimbs;
  assert(nbrLimbs >= 1);
  prevLimb = 0U;
  for (int ctr = 0; ctr < nbrLimbs; ctr++)
  {  // Process starting from least significant limb.
    unsigned int curLimb = (unsigned int)ptrDest->x;
    ptrDest->x = UintToInt(((curLimb << 1) | (prevLimb >> BITS_PER_GROUP_MINUS_1)) &
      MAX_VALUE_LIMB);
    ptrDest++;
    prevLimb = curLimb;
  }
  if ((prevLimb & HALF_INT_RANGE_U) != 0U)
  {
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
  for (index = 0; index < nbrLimbs; index++)
  {
    if (number[index].x != 0)
    {
      break;
    }
    power2 += BITS_PER_GROUP;
  }
  if (index == nbrLimbs)
  {   // Input number is zero.
    *pShRight = power2;
    return;
  }
  for (unsigned int mask = 1U; mask <= MAX_VALUE_LIMB; mask *= 2)
  {
    if (((unsigned int)number[index].x & mask) != 0U)
    {
      break;
    }
    power2++;
  }
  // Divide number by this power.
  shRight = (unsigned int)power2 % (unsigned int)BITS_PER_GROUP; // Shift right bit counter
  if (((unsigned int)number[nbrLimbs - 1].x & (0U - (1U << shRight))) != 0U)
  {   // Most significant bits set.
    *pNbrLimbs = nbrLimbs - index;
  }
  else
  {   // Most significant bits not set.
    *pNbrLimbs = nbrLimbs - index - 1;
  }
      // Move number shRg bits to the right.
  shLeft = (unsigned int)BITS_PER_GROUP - shRight;
  for (index2 = index; index2 < (nbrLimbs-1); index2++)
  {
    number[index2].x = UintToInt((((unsigned int)number[index2].x >> shRight) |
                        ((unsigned int)number[index2+1].x << shLeft)) &
                        MAX_VALUE_LIMB);
  }
  if (index2 < nbrLimbs)
  {
    number[index2].x = UintToInt(((unsigned int)number[index2].x >> shRight)
      & MAX_VALUE_LIMB);
  }
  if (index > 0)
  {   // Move limbs to final position.
    int lenBytes = (nbrLimbs - index) * (int)sizeof(limb);
    (void)memmove(number, &number[index], lenBytes);
  }
  *pShRight = power2;
}

int BigIntJacobiSymbol(const BigInteger *upper, const BigInteger *lower)
{
  int t;
  int power2;
  BigInteger a;
  BigInteger m;
  BigInteger tmp;
  CopyBigInt(&m, lower);               // m <- lower
  DivideBigNbrByMaxPowerOf2(&power2, m.limbs, &m.nbrLimbs);
  (void)BigIntRemainder(upper, lower, &a);   // a <- upper % lower
  t = 1;
  if (upper->sign == SIGN_NEGATIVE) {
    a.sign = SIGN_POSITIVE;
    if ((m.limbs[0].x & 3) == 3) {
      t = -1;
    }
  }
  while (!BigIntIsZero(&a)) {
    // a != 0
    while ((a.limbs[0].x & 1) == 0) {
      // a is even.
      BigIntDivideBy2(&a);              // a <- a / 2
      if (((m.limbs[0].x & 7) == 3) || ((m.limbs[0].x & 7) == 5)) {
        // m = 3 or m = 5 (mod 8)
        t = -t;
      }
    }
    CopyBigInt(&tmp, &a);               // Exchange a and m.
    CopyBigInt(&a, &m);
    CopyBigInt(&m, &tmp);
    if ((a.limbs[0].x & m.limbs[0].x & 3) == 3) {
      // a = 3 and m = 3 (mod 4)
      t = -t;
    }
    (void)BigIntRemainder(&a, &m, &tmp);
    // a <- a % m
    CopyBigInt(&a, &tmp);
  }
  if ((m.nbrLimbs == 1) && (m.limbs[0].x == 1)) {
    // Absolute value of m is 1.
    return t;
  }
  return 0;
}

bool BigIntIsZero(const BigInteger *value) {
  if ((value->nbrLimbs == 1) && (value->limbs[0].x == 0))
  {
    return true;     // Number is zero.
  }
  return false;      // Number is not zero.
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
  if ((value1->nbrLimbs != value2->nbrLimbs) || (value1->sign != value2->sign))
  {
    return false;    // Numbers are not equal.
  }
  nbrLimbs = value1->nbrLimbs;
  ptrValue1 = value1->limbs;
  ptrValue2 = value2->limbs;
  for (int index = 0; index < nbrLimbs; index++)
  {
    if (ptrValue1->x != ptrValue2->x)
    {
      return false;  // Numbers are not equal.
    }
    ptrValue1++;
    ptrValue2++;
  }
  return true;       // Numbers are equal.
}

double getMantissa(const limb *ptrLimb, int nbrLimbs) {
  assert(nbrLimbs >= 1);
  double dN = (double)(ptrLimb - 1)->x;
  double dInvLimb = 1.0 / (double)LIMB_RANGE;
  if (nbrLimbs > 1)
  {
    dN += (double)(ptrLimb - 2)->x * dInvLimb;
  }
  if (nbrLimbs > 2)
  {
    dN += (double)(ptrLimb - 3)->x * dInvLimb * dInvLimb;
  }
  return dN;
}

void BigIntPowerOf2(BigInteger *pResult, int exponent)
{
  unsigned int power2 = (unsigned int)exponent % (unsigned int)BITS_PER_GROUP;
  int nbrLimbs = exponent / BITS_PER_GROUP;
  if (nbrLimbs > 0)
  {
    int nbrLimbsBytes = nbrLimbs * (int)sizeof(limb);
    (void)memset(pResult->limbs, 0, nbrLimbsBytes);
  }
  pResult->limbs[nbrLimbs].x = UintToInt(1U << power2);
  pResult->nbrLimbs = nbrLimbs + 1;
  pResult->sign = SIGN_POSITIVE;
}

static void ConvertToTwosComplement(BigInteger *value)
{
  int idx;
  int nbrLimbs;
  limb *ptrLimb;
  assert(value->nbrLimbs >= 1);
  if (value->sign == SIGN_POSITIVE)
  {    // If number is positive, no conversion is needed.
    while (value->nbrLimbs > 1)
    {
      if (value->limbs[value->nbrLimbs - 1].x != 0)
      {
        break;
      }
      value->nbrLimbs--;
    }
    return;
  }
  nbrLimbs = value->nbrLimbs;
  ptrLimb = &value->limbs[0];
  for (idx = 0; idx < nbrLimbs; idx++)
  {
    if (ptrLimb->x != 0)
    {
      break;
    }
    ptrLimb++;
  }
  if (idx < nbrLimbs)
  {
    ptrLimb->x = UintToInt(LIMB_RANGE - (unsigned int)ptrLimb->x);
    ptrLimb++;
  }
  for (; idx < nbrLimbs; idx++)
  {
    ptrLimb->x = MAX_INT_NBR - ptrLimb->x;
    ptrLimb++;
  }
}


void BigIntAnd(const BigInteger* firstArgum,
               const BigInteger* secondArgum, BigInteger* result) {
  const BigInteger* firstArg;
  const BigInteger* secondArg;
  int idx;
  int carryFirst = 0;
  int carrySecond = 0;
  int limbFirst;
  int limbSecond;
  assert(firstArgum->nbrLimbs >= 1);
  assert(secondArgum->nbrLimbs >= 1);
  if (firstArgum->nbrLimbs < secondArgum->nbrLimbs) {
    // After the exchange, firstArg has not fewer limbs than secondArg.
    firstArg = secondArgum;
    secondArg = firstArgum;
  } else {
    firstArg = firstArgum;
    secondArg = secondArgum;
  }

  for (idx = 0; idx < secondArg->nbrLimbs; idx++) {
    limbFirst = firstArg->limbs[idx].x;
    limbSecond = secondArg->limbs[idx].x;

    if (firstArg->sign == SIGN_NEGATIVE) {
      carryFirst -= limbFirst;
      limbFirst = carryFirst & MAX_INT_NBR;
      carryFirst >>= 31;
    }

    if (secondArg->sign == SIGN_NEGATIVE) {
      carrySecond -= limbSecond;
      limbSecond = carrySecond & MAX_INT_NBR;
      carrySecond >>= 31;
    }

    result->limbs[idx].x = limbFirst & limbSecond;
  }
  if (secondArg->sign == SIGN_POSITIVE) {
    limbSecond = 0;
  } else {
    limbSecond = -1;
  }
  for (; idx < firstArg->nbrLimbs; idx++) {
    limbFirst = firstArg->limbs[idx].x;
    if (firstArg->sign == SIGN_NEGATIVE) {
      carryFirst -= limbFirst;
      limbFirst = carryFirst & MAX_INT_NBR;
      carryFirst >>= 31;
    }

    result->limbs[idx].x = limbFirst & limbSecond;
  }
  // Generate sign of result for "and" operation.
  if ((firstArg->sign == SIGN_NEGATIVE) && (secondArg->sign == SIGN_NEGATIVE)) {
    result->sign = SIGN_NEGATIVE;
  } else {
    result->sign = SIGN_POSITIVE;
  }

  result->nbrLimbs = firstArg->nbrLimbs;
  ConvertToTwosComplement(result);
}


