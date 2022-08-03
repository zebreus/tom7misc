
#include "big.h"

#include <array>
#include <vector>
#include <cstdint>
#include <utility>

// TODO: The "factor" program from GNU coreutils would
// be a good source for algorithmic improvements to this.

using namespace std;

static constexpr array<uint16_t, 100> PRIMES = {
  2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59, 61, 67, 71, 73, 79, 83, 89, 97, 101, 103, 107, 109, 113, 127, 131, 137, 139, 149, 151, 157, 163, 167, 173, 179, 181, 191, 193, 197, 199, 211, 223, 227, 229, 233, 239, 241, 251, 257, 263, 269, 271, 277, 281, 283, 293, 307, 311, 313, 317, 331, 337, 347, 349, 353, 359, 367, 373, 379, 383, 389, 397, 401, 409, 419, 421, 431, 433, 439, 443, 449, 457, 461, 463, 467, 479, 487, 491, 499, 503, 509, 521, 523, 541,
};

std::vector<std::pair<BigInt, int>>
BigInt::PrimeFactorization(const BigInt &x, int64_t mf) {
  // Simple trial division.
  // It would not be hard to incorporate Fermat's method too,
  // for cases that the number has factors close to its square
  // root too (this may be common?).

  // Factors in increasing order.
  std::vector<std::pair<BigInt, int>> factors;

  BigInt cur = x;
  BigInt zero(0);
  BigInt two(2);

  BigInt max_factor(mf);

  // Add the factor, or increment its exponent if it is the
  // one already at the end. This requires that the factors
  // are added in ascending order (which they are).
  auto PushFactor = [&factors](const BigInt &b) {
      if (!factors.empty() &&
          BigInt::Eq(factors.back().first, b)) {
        factors.back().second++;
      } else {
        factors.push_back(make_pair(b, 1));
      }
    };

  // First, using the prime list.
  for (int i = 0; i < (int)PRIMES.size(); /* in loop */) {
    BigInt prime(PRIMES[i]);
    const auto [q, r] = BigInt::QuotRem(cur, prime);
    if (BigInt::Eq(r, zero)) {
      cur = q;
      PushFactor(prime);
      // But don't increment i, as it may appear as a
      // factor many times.
    } else {
      i++;
    }
  }

  // Once we exhausted the prime list, do the same
  // but with odd numbers up to the square root.
  BigInt divisor((int64_t)PRIMES.back());
  divisor = BigInt::Plus(divisor, two);
  for (;;) {
    // TODO: Would be faster to compute ceil(sqrt(cur)) each
    // time we have a new cur, right?
    BigInt sq = BigInt::Times(divisor, divisor);
    if (BigInt::Greater(sq, cur))
      break;

    // TODO: Is it faster to skip ones with small factors?
    const auto [q, r] = BigInt::QuotRem(cur, divisor);
    if (BigInt::Eq(r, zero)) {
      cur = q;
      PushFactor(divisor);
      // But don't increment i, as it may appear as a
      // factor many times.
    } else {
      divisor = BigInt::Plus(divisor, two);
      if (mf >= 0 && BigInt::Greater(divisor, max_factor))
        break;
    }
  }

  // And the number itself, which we now know is prime
  // (unless we reached the max factor).
  if (!BigInt::Eq(cur, BigInt(1))) {
    PushFactor(cur);
  }

  return factors;
}
