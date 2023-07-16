
#ifndef _CC_LIB_FACTORIZE_H
#define _CC_LIB_FACTORIZE_H

#include <vector>
#include <utility>
#include <cstdint>

struct Factorize {
  // Prime factorization with trial division (not fast). Input must be > 1.
  // Output is pairs of [prime, exponent] in sorted order (by prime).
  static std::vector<std::pair<uint64_t, int>> PrimeFactorization(
      uint64_t n);

  // Writes to parallel arrays bases,exponents. Returns the number of
  // elements written. There is always at least one factor, and the
  // largest number of factors is 20 (because 21! > 2^64).
  static int PrimeFactorizationPreallocated(
      uint64_t n,
      uint64_t *bases,
      uint8_t *exponents);

  // Make sure bases are unique (by adding their exponents), and sort
  // in ascending order. Note that the factorization functions here
  // already produce unique factors.
  static void NormalizeFactors(std::vector<std::pair<uint64_t, int>> *factors);

  // Exact primality test (Lucas).
  static bool IsPrime(uint64_t n);

  // Simpler, slower reference version. Generally just useful for testing.
  static std::vector<std::pair<uint64_t, int>> ReferencePrimeFactorization(
      uint64_t n);
};

#endif
