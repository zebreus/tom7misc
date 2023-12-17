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
  uint32_t borrow = 0U;
  for (int idx = 0; idx < nbrLen; idx++) {
    borrow = (uint32_t)pNbr1[idx].x -
      (uint32_t)pNbr2[idx].x -
      (borrow >> BITS_PER_GROUP);
    uint32_t tmp = borrow & MAX_INT_NBR_U;
    pDiff[idx].x = tmp;
  }
}

std::vector<std::pair<BigInt, int>>
BigIntFactor(const BigInt &to_factor) {
  // PERF: We generally factor the same number more than once. It would
  // be helpful to cache the last call here, or explicitly thread factors
  // through the code (from quad to quadmodll, I think).
  // fprintf(stderr, "Factor %s\n", to_factor.ToString().c_str());

  std::vector<std::pair<BigInt, int>> factors =
    BigInt::PrimeFactorization(to_factor);

  // Alpertron wants 1 in the list in this case.
  if (factors.empty()) factors.emplace_back(BigInt(1), 1);
  return factors;
}
