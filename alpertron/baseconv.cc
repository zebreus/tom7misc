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

#include "baseconv.h"

#include <string.h>
#include <math.h>
#include <stdint.h>

#include "bignbr.h"
#include "bigconv.h"
#include "base/logging.h"

static constexpr bool VERBOSE = false;

void int2dec(char **pOutput, int nbr) {
  char *ptrOutput = *pOutput;
  bool significantZero = false;
  unsigned int div = 1000000000U;
  unsigned int value = (unsigned int)nbr;
  while (div > 0U)
  {
    int digit;

    digit = value/div;
    if ((digit > 0) || significantZero)
    {
      significantZero = true;
      *ptrOutput = (char)(digit + '0');
      ptrOutput++;
    }
    value %= div;
    div /= 10;
  }
  if (!significantZero)
  {
    *ptrOutput = '0';
    ptrOutput++;
  }
  *pOutput = ptrOutput;
}

// Converts using GMP.
void Bin2Dec(char **ppDecimal, const limb *binary, int nbrLimbs, int groupLength) {
  BigInt b = LimbsToBigInt(binary, nbrLimbs);
  std::string s = b.ToString();
  char* ptrDecimal = *ppDecimal;
  for (char c : s)
    *ptrDecimal++ = c;
  *ppDecimal = ptrDecimal;
}

void BigInteger2Dec(char **ppDecimal, const BigInteger *pBigInt) {
  BigInt b = BigIntegerToBigInt(pBigInt);
  std::string s = b.ToString();
  char* ptrDecimal = *ppDecimal;
  for (char c : s)
    *ptrDecimal++ = c;
  *ppDecimal = ptrDecimal;
}
