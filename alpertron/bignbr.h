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

enum eSign {
  SIGN_POSITIVE = 0,
  SIGN_NEGATIVE,
};

// Multiply two limb arrays of the same size, writing to the result.
// Writes 2 * len limbs to the output, including zero padding if necessary.
void MultiplyLimbs(const limb* factor1, const limb* factor2, limb* result,
                   int len);

// Modular. Perhaps should be in modmult.
void SubtractBigNbr(const limb *pNbr1, const limb *pNbr2, limb *pDiff, int nbrLen);

static inline int UintToInt(unsigned int value) {
  return (int)value;
}

#endif
