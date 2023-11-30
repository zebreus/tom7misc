
#include "quad.h"

#include <bit>
#include <array>
#include <string>
#include <cstdio>
#include <cstdint>

#include "base/logging.h"
#include "base/stringprintf.h"
#include "threadutil.h"
#include "bignum/big.h"
#include "bignum/big-overloads.h"
#include "timer.h"
#include "periodically.h"
#include "ansi.h"
#include "atomic-util.h"
#include "util.h"
#include "auto-histo.h"
#include "crypt/lfsr.h"
#include "arcfour.h"
#include "randutil.h"
#include "factorization.h"

#include "sos-util.h"

using namespace std;

// Pure powers.
static void SimpleMaxValues() {

  for (int64_t base = 2; base < 99; base++) {

    uint64_t pow = 1;
    for (;;) {
      uint64_t next_pow = pow * base;
      if (next_pow <= pow) break;
      if (next_pow & (1ULL << 63)) break;
      pow = next_pow;

      std::vector<std::pair<uint64_t, int>> factors =
        Factorization::Factorize(pow);

      Solutions sol = SolveQuad(pow, factors);
      (void)sol;
    }

  }
  printf("OK\n");
}


// Test numbers of the form a^x * b^y for small a,b and
// large x,y. We get a little extra mileage by testing
// non-prime a,b as well.
static void ProductOfTwo() {
  for (int64_t a = 2; a < 99; a++) {
    for (int64_t b = a + 1; b < 99; b++) {

      // number of factors of a. we cover
      // pure powers in SimpleMaxValues.
      BigInt ap(1);
      for (int af = 1; af < 63; af++) {
        ap = ap * a;
        if (ap >= (1ULL << 63)) break;

        for (int bf = 1; bf < 63; bf++) {
          BigInt bp = ap * BigInt::Pow(BigInt(b), bf);
          if (bp >= (1ULL << 63)) break;

          // Now we have some maximal a^x * b^y. Try it.
          std::optional<int64_t> pow = bp.ToInt();
          CHECK(pow.has_value());
          std::vector<std::pair<uint64_t, int>> factors =
            Factorization::Factorize(pow.value());

          Solutions sol = SolveQuad(pow.value(), factors);
          (void)sol;
        }
      }
    }
  }

  printf("OK\n");
}

int main(int argc, char **argv) {
  ANSI::Init();

  SimpleMaxValues();
  ProductOfTwo();

  // TODO: Test individual large factors.

  return 0;
}
