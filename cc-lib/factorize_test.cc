#include "factorize.h"

#include <bit>
#include <cmath>
#include <initializer_list>
#include <cstdint>
#include <vector>
#include <utility>
#include <array>

#include "base/logging.h"
#include "base/stringprintf.h"
#include "timer.h"
#include "crypt/lfsr.h"

using namespace std;

string FTOS(const std::vector<std::pair<uint64_t, int>> &fs) {
  string s;
  for (const auto &[b, i] : fs) {
    StringAppendF(&s, "%llu^%d ", b, i);
  }
  return s;
}

// https://www.nuprl.org/MathLibrary/integer_sqrt/

static uint64_t Sqrt64Nuprl(uint64_t xx) {
  if (xx <= 1) return xx;
  // z = xx / 4
  uint64_t z = xx >> 2;
  uint64_t r2 = 2 * Sqrt64Nuprl(z);
  uint64_t r3 = r2 + 1;
  return (xx < r3 * r3) ? r2 : r3;
}

uint64_t Sqrt64Embedded(uint64_t a) {
  uint64_t rem = 0;
  uint64_t root = 0;

  for (int i = 0; i < 32; i++) {
    root <<= 1;
    rem <<= 2;
    rem += a >> 62;
    a <<= 2;

    if (root < rem) {
      root++;
      rem -= root;
      root++;
    }
  }

  return root >> 1;
}

/*
// C++20 also provides std::bit_width in its <bit> header
unsigned char bit_width(unsigned long long x) {
    return x == 0 ? 1 : 64 - __builtin_clzll(x);
}
*/

static uint64_t Sqrt64BitGuess(const uint64_t n) {
  unsigned char shift = std::bit_width<uint64_t>(n);
  shift += shift & 1; // round up to next multiple of 2

  uint64_t result = 0;

  do {
    shift -= 2;
    result <<= 1; // make space for the next guessed bit
    result |= 1;  // guess that the next bit is 1
    result ^= result * result > (n >> shift); // revert if guess too high
  } while (shift != 0);

  return result;
}

static uint64_t Sqrt64Double(uint64_t n) {
  if (n == 0) return 0;
  uint64_t r = std::sqrt((double)n);
  return r - (r * r - 1 >= n);
}

static uint64_t Sqrt64Tglas(uint64_t n) {
  if (n == 0) return 0;
  uint64_t r = 0;
  for (uint64_t b = uint64_t{1} << ((std::bit_width(n) - 1) >> 1);
       b != 0;
       b >>= 1) {
    const uint64_t k = (b + 2 * r) * b;
    r |= (n >= k) * b;
    n -= (n >= k) * k;
  }
  return r;
}

#define Sqrt64 Sqrt64Tglas

static void TestSqrt() {
   for (uint64_t x : std::initializer_list<uint64_t>{
       0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
         182734987, 298375827, 190238482, 19023904, 11111111,
         923742, 27271917, 127, 1, 589589581, 55555555,
         0x0FFFFFFFFFFFFFF, 0x0FFFFFFFFFFFFFE, 0x0FFFFFFFFFFFFFD,
         65537, 10002331, 100004389, 1000004777, 10000007777,
         65537ULL * 10002331ULL,
         10002331ULL * 100004389ULL,
         100004389ULL * 1000004777ULL,
         1000004777ULL * 10000007777ULL,
         10000055547037150728ULL,
         10000055547037150727ULL,
         10000055547037150726ULL,
         10000055547037150725ULL,
         10000055547037150724ULL,
         18446744073709551557ULL,
         29387401978, 2374287337, 9391919, 4474741071,
         18374, 1927340972, 29292922, 131072, 7182818}) {
     uint64_t root = Sqrt64(x);
     // What we want is the floor.
     CHECK(root * root <= x) << root << " " << x;

     // Result should fit in 32 bits. But we can't actually square
     // 2^32-1 because it would overflow. However, since that root+1
     // squared would be larger than any 64-bit number, it's correct
     // if we get it here and passed the previous check.
     CHECK(root <= 4294967295);
     if (root != 4294967295) {
       CHECK((root + 1ULL) * (root + 1ULL) > x)
         << root << " " << x << "(" << (root + 1ULL) << ")";
     }
   }
 }

static void TestPrimeFactors() {

  {
    std::vector<std::pair<uint64_t, int>> factors =
      Factorize::PrimeFactorization(31337);

    CHECK(factors.size() == 1);
    CHECK(factors[0].second == 1);
    CHECK(factors[0].first == 31337);
  }

  {
    uint64_t x = 31337 * 71;
    std::vector<std::pair<uint64_t, int>> factors =
      Factorize::PrimeFactorization(x);

    CHECK(factors.size() == 2) << FTOS(factors);
    CHECK(factors[0].first == 71);
    CHECK(factors[0].second == 1) << factors[0].second;
    CHECK(factors[1].first == 31337);
    CHECK(factors[1].second == 1) << factors[0].second;
  }

  {
    uint64_t sq31337 = 31337 * 31337;
    std::vector<std::pair<uint64_t, int>> factors =
      Factorize::PrimeFactorization(sq31337);

    CHECK(factors.size() == 1) << FTOS(factors);
    CHECK(factors[0].first == 31337);
    CHECK(factors[0].second == 2) << factors[0].second;
  }

  {
    uint64_t x = 1;
    // Must all be distinct and prime. Careful about overflow!
    const array f = {
      2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 37, 41, 43, 47, 419,
    };

    for (int factor : f) {
      x *= factor;
      // printf("%llu\n", x);
    }

    std::vector<std::pair<uint64_t, int>> factors =
      Factorize::PrimeFactorization(x);

    CHECK(factors.size() == f.size()) << FTOS(factors);
    for (int i = 0; i < (int)f.size(); i++) {
      CHECK(factors[i].second == 1);
      CHECK((int)factors[i].first == f[i]);
    }
  }

#if 0
// If we add the max_factor arg back in; consider something here.
  {
    // 100-digit prime; trial factoring will not succeed!
    uint64_t p1("207472224677348520782169522210760858748099647"
              "472111729275299258991219668475054965831008441"
              "6732550077");
    uint64_t p2("31337");

    uint64_t x = uint64_t::Times(p1, p2);

    // Importantly, we set a max factor (greater than p2, and
    // greater than the largest value in the primes list).
    std::vector<std::pair<uint64_t, int>> factors =
      Factorize::PrimeFactorization(x, 40000);

    CHECK(factors.size() == 2);
    CHECK(factors[0].second == 1);
    CHECK(factors[1].second == 1);
    CHECK(uint64_t::Eq(factors[0].first, p2));
    CHECK(uint64_t::Eq(factors[1].first, p1));
  }
#endif
}

static void BenchSqrt() {
  Timer timer;
  uint64_t result = 0;

  for (int i = 0; i < 2000; i++) {
    uint32_t a = 0xDEAD;
    uint32_t b = 0xDECEA5ED;
    for (int j = 0; j < 100000; j++) {
      uint64_t input = ((uint64_t)a) << 32 | b;
      result += Sqrt64(input);
      a = LFSRNext32(LFSRNext32(a));
      b = LFSRNext32(b);
    }
  }
  double seconds = timer.Seconds();
  printf("Result %llx\n", result);
  printf("Sqrts %.3f seconds.\n", seconds);
  printf("-------------------\n");
}

static void BenchFactorize() {
  Timer timer;
  uint64_t result = 0;
  for (uint64_t num :
         std::initializer_list<uint64_t>{
         182734987, 298375827, 190238482, 19023904, 11111111,
           923742, 27271917, 127, 1, 589589581, 55555555,
           0x0FFFFFFFFFFFFFF, 0x0FFFFFFFFFFFFFE, 0x0FFFFFFFFFFFFFD,
           // primes
           65537, 10002331, 100004389, 1000004777, 10000007777,
           65537ULL * 10002331ULL,
           10002331ULL * 100004389ULL,
           100004389ULL * 1000004777ULL,
           // big, slow.
           1000004777ULL * 10000007777ULL,
           10000055547037150728ULL,
           10000055547037150727ULL,
           10000055547037150726ULL,
           10000055547037150725ULL,
           10000055547037150724ULL,
           // largest 64-bit prime; this is pretty much the worst
           // case for the trial division algorithm.
           18446744073709551557ULL,
           29387401978, 2374287337, 9391919, 4474741071,
           18374, 1927340972, 29292922, 131072, 7182818}) {
    auto factors = Factorize::PrimeFactorization(num);
    printf("%llu: %s\n", num, FTOS(factors).c_str());
    for (const auto &[p, e] : factors) result += p * e;
  }
  double seconds = timer.Seconds();
  printf("Result %llx\n", result);
  printf("Factored the list in %.3f seconds.\n", seconds);
}

int main(int argc, char **argv) {
  TestSqrt();
  TestPrimeFactors();

  BenchSqrt();

  BenchFactorize();

  printf("OK\n");
}
