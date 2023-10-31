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

#ifndef _BIGNBR_H
#define _BIGNBR_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include "bignum/big.h"

// Tom increased this constant by a factor of 10!
#define MAX_LEN       25000  // 200000 digits
#define BITS_PER_GROUP         31
#define BITS_PER_GROUP_MINUS_1 30
#define HALF_INT_RANGE        0x40000000
#define HALF_INT_RANGE_U      0x40000000U
#define FOURTH_INT_RANGE      0x20000000
#define FOURTH_INT_RANGE_U    0x20000000U
#define MAX_INT_NBR           0x7FFFFFFF
#define MAX_INT_NBR_U         0x7FFFFFFFU
#define LIMB_RANGE            0x80000000U
#define MAX_VALUE_LIMB        0x7FFFFFFFU
#define SMALL_NUMBER_BOUND    32768

struct limb {
  int x;
};

enum eExprErr {
  EXPR_OK = 0,
  EXPR_DIVIDE_BY_ZERO,
  EXPR_INVALID_PARAM,
  EXPR_INTERM_TOO_HIGH,
};

enum eSign {
  SIGN_POSITIVE = 0,
  SIGN_NEGATIVE,
};

typedef struct BigInteger {
  // PERF unnecessary
  BigInteger() { nbrLimbs = 0; limbs[0].x = 0xDEAD; }
  int nbrLimbs;
  enum eSign sign;
  limb limbs[MAX_LEN];
} BigInteger;

// Multiply two limb arrays of the same size, writing to the result.
// Writes 2 * len limbs to the output, including zero padding if necessary.
void MultiplyLimbs(const limb* factor1, const limb* factor2, limb* result,
                   int len);

void AddBigInt(const limb *pAddend1, const limb *pAddend2, limb *pSum, int nbrLimbs);
bool BigIntIsZero(const BigInteger *value);
void BigIntChSign(BigInteger *value);
void BigIntAdd(const BigInteger *pAddend1, const BigInteger *pAddend2, BigInteger *pSum);
void BigIntSubt(const BigInteger* pMinuend, const BigInteger* pSubtrahend, BigInteger* pDifference);
void BigIntNegate(const BigInteger *pSrc, BigInteger *pDest);

eExprErr BigIntMultiply(
    const BigInteger *pFactor1, const BigInteger *pFactor2,
    BigInteger *pProduct);

eExprErr BigIntPower(const BigInteger *pBase, const BigInteger *pExponent,
                     BigInteger *pPower);

eExprErr BigIntPowerIntExp(const BigInteger *pBase, int exponent,
                           BigInteger *pPower);

void BigIntDivideBy2(BigInteger *nbr);
void BigIntGcd(const BigInteger *pArg1, const BigInteger *pArg2, BigInteger *pResult);
void MultInt(BigInteger *pResult, const BigInteger *pMult, int factor);
void multadd(BigInteger *pResult, int iMult, const BigInteger *pMult, int addend);
void BigIntPowerOf2(BigInteger *pResult, int expon);

void addbigint(BigInteger *pResult, int addend);
void CopyBigInt(BigInteger *pDest, const BigInteger *pSrc);
int getNbrLimbs(const limb *bigNbr);
void BigIntAnd(const BigInteger *firstArg, const BigInteger *secondArg, BigInteger *result);
void IntArray2BigInteger(
    int number_length, const int *ptrValues, /*@out@*/BigInteger *bigint);
void BigInteger2IntArray(int number_length,
                         /*@out@*/int *ptrValues, const BigInteger *bigint);

// Copies a fixed-width array of limbs to the bigint. Removes
// high limbs that are 0 (which are trailing in little-endian
// representation). I think this is "Uncompress" in the sense
// that BigInteger has a fixed buffer large enough for "any number",
// but in a way it is actually compression since the fixed-width
// represents zero high limbs, but this does not.
void UncompressLimbsBigInteger(int number_length,
                               const limb *ptrValues,
                               /*@out@*/BigInteger *bigint);

// Copies the BigInteger's limbs into a fixed number of limbs in ptrValues.
// If the bigint has more limbs than number_length, it's just truncated
// (so we get the result mod 2^(number_length*bits_per_limb)). Pads the
// array with zeroes.
void CompressLimbsBigInteger(int number_length,
                             /*@out@*/limb *ptrValues,
                             const BigInteger *bigint);

void intToBigInteger(BigInteger *bigint, int value);

void ChSignLimbs(limb *nbr, int length);
void AddBigNbr(const limb *pNbr1, const limb *pNbr2, limb *pSum, int nbrLen);
void SubtractBigNbr(const limb *pNbr1, const limb *pNbr2, limb *pDiff, int nbrLen);
void DivBigNbrByInt(const limb *pDividend, int divisor, limb *pQuotient, int nbrLen);
void MultBigNbr(const limb *pFactor1, const limb *pFactor2, limb *pProd, int nbrLen);
void IntToBigNbr(int value, limb *bigNbr, int nbrLength);
int BigNbrToBigInt(const BigInteger *pBigNbr, limb *pBigInt);
void BigIntToBigNbr(BigInteger *pBigNbr, const limb *pBigInt, int nbrLenBigInt);
void AdjustBigIntModN(limb *Nbr, const limb *Mod, int nbrLen);

static inline int UintToInt(unsigned int value) {
  return (int)value;
}

#endif
