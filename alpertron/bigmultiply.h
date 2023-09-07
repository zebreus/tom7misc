#ifndef _BIGMULTIPLY_H
#define _BIGMULTIPLY_H

#include "bignbr.h"
#include "bignum/big.h"

void BigMultiply(const limb* factor1, const limb* factor2, limb* result,
                 int len, int* pResultLen);

void BigMultiplyWithBothLen(const limb* factor1, const limb* factor2, limb* result,
                            int len1, int len2, int* pResultLen);

#endif
