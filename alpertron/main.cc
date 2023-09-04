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
#include <stdlib.h>
#include <string.h>

#include "bignbr.h"
#include "globals.h"
#include "highlevel.h"
#include "baseconv.h"

extern bool teach;

static bool ParseBigInteger(const char *str, BigInteger *big) {
  big->sign = SIGN_POSITIVE;
  if (str[0] == '-') {
    str++;
    big->sign = SIGN_NEGATIVE;
  }

  int nbrDigits = (int)strlen(str);
  Dec2Bin(str, big->limbs, nbrDigits, &big->nbrLimbs);

  return true;
}

int main(int argc, char* argv[]) {
  if (argc != 8) {
    (void)printf("Enter 6 coefficients and teach flag (0 or 1):\n"
      "   x^2, xy, y^2, x, y, const teach.\n");
    return 1;
  }

  teach = argv[7][0] == '1';
  // quadText(argv[1], argv[2], argv[3], argv[4], argv[5], argv[6]);

  BigInteger a, b, c, d, e, f;
  if (!ParseBigInteger(argv[1], &a)) printf("can't parse a\n");
  if (!ParseBigInteger(argv[2], &b)) printf("can't parse b\n");
  if (!ParseBigInteger(argv[3], &c)) printf("can't parse c\n");
  if (!ParseBigInteger(argv[4], &d)) printf("can't parse d\n");
  if (!ParseBigInteger(argv[5], &e)) printf("can't parse e\n");
  if (!ParseBigInteger(argv[6], &f)) printf("can't parse f\n");

  quadBigInt(&a, &b, &c, &d, &e, &f);
  (void)printf("%s\n", output);

  return 0;
}
