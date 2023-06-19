
#include "factorize.h"

#include <cstdlib>
#include <array>
#include <cstdint>
#include <vector>
#include <utility>
#include <cmath>

using namespace std;

static uint64_t Sqrt64(uint64_t n) {
  if (n == 0) return 0;
  uint64_t r = std::sqrt((double)n);
  return r - (r * r - 1 >= n);
}

// PERF: Gaps between primes are <= 250 all the
// way up to 387096383. So we could have a lot more here.
static constexpr array<uint8_t, 999> PRIME_DELTAS = {
1,2,2,4,2,4,2,4,6,2,6,4,2,4,6,6,2,6,4,2,6,4,6,8,4,2,4,2,4,14,4,6,
2,10,2,6,6,4,6,6,2,10,2,4,2,12,12,4,2,4,6,2,10,6,6,6,2,6,4,2,10,14,4,2,
4,14,6,10,2,4,6,8,6,6,4,6,8,4,8,10,2,10,2,6,4,6,8,4,2,4,12,8,4,8,4,6,
12,2,18,6,10,6,6,2,6,10,6,6,2,6,6,4,2,12,10,2,4,6,6,2,12,4,6,8,10,8,10,8,
6,6,4,8,6,4,8,4,14,10,12,2,10,2,4,2,10,14,4,2,4,14,4,2,4,20,4,8,10,8,4,6,
6,14,4,6,6,8,6,12,4,6,2,10,2,6,10,2,10,2,6,18,4,2,4,6,6,8,6,6,22,2,10,8,
10,6,6,8,12,4,6,6,2,6,12,10,18,2,4,6,2,6,4,2,4,12,2,6,34,6,6,8,18,10,14,4,
2,4,6,8,4,2,6,12,10,2,4,2,4,6,12,12,8,12,6,4,6,8,4,8,4,14,4,6,2,4,6,2,
6,10,20,6,4,2,24,4,2,10,12,2,10,8,6,6,6,18,6,4,2,12,10,12,8,16,14,6,4,2,4,2,
10,12,6,6,18,2,16,2,22,6,8,6,4,2,4,8,6,10,2,10,14,10,6,12,2,4,2,10,12,2,16,2,
6,4,2,10,8,18,24,4,6,8,16,2,4,8,16,2,4,8,6,6,4,12,2,22,6,2,6,4,6,14,6,4,
2,6,4,6,12,6,6,14,4,6,12,8,6,4,26,18,10,8,4,6,2,6,22,12,2,16,8,4,12,14,10,2,
4,8,6,6,4,2,4,6,8,4,2,6,10,2,10,8,4,14,10,12,2,6,4,2,16,14,4,6,8,6,4,18,
8,10,6,6,8,10,12,14,4,6,6,2,28,2,10,8,4,14,4,8,12,6,12,4,6,20,10,2,16,26,4,2,
12,6,4,12,6,8,4,8,22,2,4,2,12,28,2,6,6,6,4,6,2,12,4,12,2,10,2,16,2,16,6,20,
16,8,4,2,4,2,22,8,12,6,10,2,4,6,2,6,10,2,12,10,2,10,14,6,4,6,8,6,6,16,12,2,
4,14,6,4,8,10,8,6,6,22,6,2,10,14,4,6,18,2,10,14,4,2,10,14,4,8,18,4,6,2,4,6,
2,12,4,20,22,12,2,4,6,6,2,6,22,2,6,16,6,12,2,6,12,16,2,4,6,14,4,2,18,24,10,6,
2,10,2,10,2,10,6,2,10,2,10,6,8,30,10,2,10,8,6,10,18,6,12,12,2,18,6,4,6,6,18,2,
10,14,6,4,2,4,24,2,12,6,16,8,6,6,18,16,2,4,6,2,6,6,10,6,12,12,18,2,6,4,18,8,
24,4,2,4,6,2,12,4,14,30,10,6,12,14,6,10,12,2,4,6,8,6,10,2,4,14,6,6,4,6,2,10,
2,16,12,8,18,4,6,12,2,6,6,6,28,6,14,4,8,10,8,12,18,4,2,4,24,12,6,2,16,6,6,14,
10,14,4,30,6,6,6,8,6,4,2,12,6,4,2,6,22,6,2,4,18,2,4,12,2,6,4,26,6,6,4,8,
10,32,16,2,6,4,2,4,2,10,14,6,4,8,10,6,20,4,2,6,30,4,8,10,6,6,8,6,12,4,6,2,
6,4,6,2,10,2,16,6,20,4,12,14,28,6,20,4,18,8,6,4,6,14,6,6,10,2,10,12,8,10,2,10,
8,12,10,24,2,4,8,6,4,8,18,10,6,6,2,6,10,12,2,10,6,6,6,8,6,10,6,2,6,6,6,10,
8,24,6,22,2,18,4,8,10,30,8,18,4,2,10,6,2,6,4,18,8,12,18,16,6,2,12,6,10,2,10,2,
6,10,14,4,24,2,16,2,10,2,10,20,4,2,4,8,16,6,6,2,12,16,8,4,6,30,2,10,2,6,4,6,
6,8,6,4,12,6,8,12,4,14,12,10,24,6,12,6,2,22,8,18,10,6,14,4,2,6,10,8,6,4,6,30,
14,10,2,12,10,2,16,2,18,24,18,6,16,18,6,2,18,4,6,2,10,8,10,6,6,8,4,6,2,10,2,12,
4,6,6,2,12,4,14,18,4,6,20,4,8,6,4,8,4,14,6,4,14,12,4,2,30,4,24,6,6,12,12,14,
6,4,2,4,18,6,12,
};

std::vector<std::pair<uint64_t, int>>
Factorize::PrimeFactorization(uint64_t x) {
  // TODO: Allow as argument
  uint64_t max_factor = x;

  // Simple trial division.
  // It would not be hard to incorporate Fermat's method too,
  // for cases that the number has factors close to its square
  // root too (this may be common?).

  // Factors in increasing order.
  std::vector<std::pair<uint64_t, int>> factors;

  if (x <= 1) return {};

  // Add the factor, or increment its exponent if it is the
  // one already at the end. This requires that the factors
  // are added in ascending order (which they are).
  auto PushFactor = [&factors](uint64_t b) {
      if (!factors.empty() &&
          factors.back().first == b) {
        factors.back().second++;
      } else {
        factors.emplace_back(b, 1);
      }
    };

  // Current value being factored; as we find factors, we divide
  // this target.
  uint64_t cur = x;
  // First, using the prime list.
  uint32_t prime = 2;
  for (int delta_idx = 0; delta_idx < (int)PRIME_DELTAS.size(); /* in loop */) {
    if (prime > max_factor)
      break;

    uint64_t rem = cur % prime;
    if (rem == 0) {
      cur = cur / prime;
      if (max_factor > cur)
        max_factor = cur;
      PushFactor(prime);
      // But don't get next prime, as it may appear as a
      // factor many times.
    } else {
      prime += PRIME_DELTAS[delta_idx++];
    }
  }

  // Once we exhausted the prime list, do the same
  // but with odd numbers up to the square root. Note
  // that the last prime is not actually tested above; it's
  // the initial value for this loop.
  uint64_t divisor = prime;
  uint64_t sqrtcur = Sqrt64(cur) + 1;
  for (;;) {
    // skip if a multiple of 3.
    if (divisor > max_factor)
      break;

    if (divisor > sqrtcur)
      break;

    const uint64_t rem = cur % divisor;
    if (rem == 0) {
      cur = cur / divisor;
      sqrtcur = Sqrt64(cur) + 1;
      PushFactor(divisor);
      // But don't increment i, as it may appear as a
      // factor many times.
    } else {
      do {
        // Always skip even numbers; this is trivial.
        divisor += 2;
        // But also skip divisors with small factors. Modding by
        // small constants can be compiled into multiplication
        // tricks, so this is faster than doing another proper
        // loop.
      } while ((divisor % 3) == 0 ||
               (divisor % 5) == 0 ||
               (divisor % 7) == 0 ||
               (divisor % 11) == 0 ||
               (divisor % 13) == 0 ||
               (divisor % 17) == 0);
    }
  }

  // And the number itself, which we now know is prime
  // (unless we reached the max factor).
  if (cur != 1) {
    PushFactor(cur);
  }

  return factors;
}
