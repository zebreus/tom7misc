
#include "factor.h"

#include <vector>
#include <utility>

#include "bignum/big.h"

// Factor using bignum library.

std::vector<std::pair<BigInt, int>>
BigIntFactor(const BigInt &to_factor) {
  std::vector<std::pair<BigInt, int>> factors =
    BigInt::PrimeFactorization(to_factor);

  // Alpertron wants 1 in the list in this case.
  if (factors.empty()) factors.emplace_back(BigInt(1), 1);
  return factors;
}
