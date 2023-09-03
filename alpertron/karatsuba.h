#ifndef _KARATSUBA_H
#define _KARATSUBA_H

#include "bignbr.h"

void multiply(const limb* factor1, const limb* factor2, limb* result,
  int len, int* pResultLen);

void multiplyWithBothLen(const limb* factor1, const limb* factor2, limb* result,
  int len1, int len2, int* pResultLen);

#endif
