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

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>

#include "bignbr.h"

#include "bignum/big.h"
#include "bigconv.h"
#include "base/logging.h"

// XXX just move this to bignbr.cc

enum eExprErr BigIntDivide(const BigInteger *pDividend, const BigInteger *pDivisor,
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

