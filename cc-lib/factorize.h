
#ifndef _CC_LIB_FACTORIZE_H
#define _CC_LIB_FACTORIZE_H

#include <vector>
#include <utility>
#include <cstdint>

struct Factorize {
  // Prime factorization with trial division (not fast). Input must be > 1.
  // Output is pairs of [prime, exponent] in sorted order (by prime).
  static std::vector<std::pair<uint64_t, int>> PrimeFactorization(uint64_t n);
};

#endif
