// This file is part of Alpertron Calculators.
//
// Copyright 2017-2021 Dario Alejandro Alpern
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

#ifndef _QUADMODLL_H
#define _QUADMODLL_H

#include <cstdint>
#include <vector>
#include <utility>

// With A,B,C = 1,0,1
// Returns the values that are solutions (i.e. what the original
// code would have called the callback on.)
std::vector<uint64_t> SolveEquation(
    uint64_t n,
    // factors of N.
    const std::vector<std::pair<uint64_t, int>> &factors,
    bool *interesting_coverage = nullptr);

// Exposed for testing.
int64_t SqrtModP(uint64_t base,
                 uint64_t prime,
                 int64_t discr,
                 uint64_t term,
                 int nbrBitsSquareRoot);

#endif
