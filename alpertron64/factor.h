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

#ifndef _ALPERTRON_FACTOR_H
#define _ALPERTRON_FACTOR_H

#include <vector>
#include <utility>

#include "bignum/big.h"

// Same as BigInt::PrimeFactorization, but with {1, 1} for 1.
std::vector<std::pair<BigInt, int>>
BigIntFactor(const BigInt &to_factor);

#endif
