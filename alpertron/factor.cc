
#include "factor.h"

#include <vector>
#include <utility>
#include <memory>

#include "bigconv.h"
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

std::unique_ptr<Factors> BigFactor(const BigInteger *toFactor) {
  BigInt num = BigIntegerToBigInt(toFactor);

  std::vector<std::pair<BigInt, int>> factors = BigIntFactor(num);

  // PERF we could compute the storage amount. But better would be
  // to just port callers to BigIntFactor.
  auto ret = std::make_unique<Factors>();
  ret->storage.resize(20000);
  int pos = 0;
  for (const auto &[b, e] : factors) {
    int size = BigIntToArray(b, ret->storage.data() + pos);
    ret->product.push_back({.array = ret->storage.data() + pos,
                            .multiplicity = e});
    pos += size;
  }

  return ret;
}
