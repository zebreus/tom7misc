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
#include "quad.h"
#include "bigconv.h"

int main(int argc, char* argv[]) {
  if (argc != 8) {
    (void)printf("Enter 6 coefficients and teach flag (0 or 1):\n"
      "   x^2, xy, y^2, x, y, const teach.\n");
    return 1;
  }

  bool teach = argv[7][0] == '1';
  // quadText(argv[1], argv[2], argv[3], argv[4], argv[5], argv[6]);

  BigInt a(argv[1]);
  BigInt b(argv[2]);
  BigInt c(argv[3]);
  BigInt d(argv[4]);
  BigInt e(argv[5]);
  BigInt f(argv[6]);

  printf("\n** Quad %s %s %s %s %s %s\n",
         a.ToString().c_str(),
         b.ToString().c_str(),
         c.ToString().c_str(),
         d.ToString().c_str(),
         e.ToString().c_str(),
         f.ToString().c_str());

  std::string output;
  QuadBigInt(teach, a, b, c, d, e, f, &output);
  (void)printf("%s\n", output.c_str());

  return 0;
}
