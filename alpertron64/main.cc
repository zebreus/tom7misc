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
#include <vector>
#include <utility>
#include <cstdint>

#include "quad.h"
#include "bignum/big.h"
#include "bignum/big-overloads.h"
#include "factorization.h"

#include "base/do-not-optimize.h"
#include "base/logging.h"

static Solutions Run(
    int64_t f,
    const std::vector<std::pair<uint64_t, int>> &factors) {
  Solutions solutions;

  #if RUN_LOOP
  constexpr int TIMES = 1000000;
  for (int i = 0; i < TIMES; i++) {
  #endif

  solutions = SolveQuad(f, factors);
  #if RUN_LOOP
  DoNotOptimize(solutions);
  }
  #endif
  return solutions;
}

int main(int argc, char* argv[]) {
  if (argc != 2) {
    printf("Enter 1 coefficient:\n"
           "   x^2 + y^2 = F\n");
    return 1;
  }

  const int64_t f = atoll(argv[1]);

  printf("\n** Solve x^2 + y^2 = %lld\n", f);

  std::vector<std::pair<uint64_t, int>> factors =
    Factorization::Factorize(f);

  Solutions solutions = Run(f, factors);
  CHECK(!solutions.interesting_coverage);

  if (solutions.points.empty()) {
    printf("The equation does not have integer solutions.\n");
  } else {
    for (const PointSolution &point : solutions.points) {
      printf("Solution:\n"
             "x = %lld\n"
             "y = %lld\n",
             point.X,
             point.Y);
    }
  }

  return 0;
}
