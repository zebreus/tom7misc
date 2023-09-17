
#include "factor.h"

#include "bigconv.h"
#include "bignum/big.h"

// Factor using bignum library.


std::unique_ptr<Factors> BigFactor(const BigInteger *toFactor) {
  BigInt num = BigIntegerToBigInt(toFactor);

  std::vector<std::pair<BigInt, int>> factors =
    BigInt::PrimeFactorization(num);

  // Alpertron wants 1 in the list in this case.
  if (factors.empty()) factors.emplace_back(BigInt(1), 1);

  // PERF we could compute the storage amount.
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
