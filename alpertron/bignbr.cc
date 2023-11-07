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

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#include "bignbr.h"
#include "modmult.h"

#include "base/logging.h"
#include "base/stringprintf.h"
#include "bigconv.h"

static constexpr bool VERBOSE = false;

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

