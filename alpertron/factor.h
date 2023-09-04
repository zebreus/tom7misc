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

#ifndef _FACTOR_H
#define _FACTOR_H

#include <memory>
#include <vector>

#include "bignbr.h"

struct sFactorz {
  int *array = nullptr;
  int multiplicity = 0;
};

struct Factors {
  Factors() {}
  // int pointers point into storage.
  std::vector<sFactorz> product;

  // Private outside of factoring itself.
  std::vector<int> storage;
private:
  // Not copyable, because of internal array pointers.
  Factors(const Factors &) = delete;
  Factors &operator =(const Factors&) = delete;
};

std::unique_ptr<Factors> BigFactor(const BigInteger *toFactor);

#endif
