
#include "bigconv.h"

#include <string>
#include <cstring>

#include "base/logging.h"
#include "bignum/big.h"
#include "bignbr.h"
#include "base/stringprintf.h"

using namespace std;

#ifndef BIG_USE_GMP
# error This program requires GMP mode for bignum.
#endif

BigInt LimbsToBigInt(const limb *limbs, int num_limbs) {
  CHECK(limbs != nullptr);
  CHECK(num_limbs > 0);
  BigInt out;
  mpz_import(out.GetRep(), num_limbs,
             // words are little-endian
             -1,
             // word size
             sizeof (int),
             // native byte-order
             0,
             // "nails": high bits to skip in each word
             1,
             limbs);
  return out;
}

int BigIntNumLimbs(const BigInt &b) {
  static constexpr int bits_per_limb = 31;

  // Number of bits in b.
  const size_t num_bits = mpz_sizeinbase(b.GetRep(), 2);

  // Round up if needed.
  return (num_bits + bits_per_limb - 1) / bits_per_limb;
}

int BigIntToLimbs(const BigInt &b, limb *limbs) {
  size_t count = 0;
  mpz_export(limbs, &count,
             // words are little endian.
             -1,
             // word size
             sizeof(int),
             // native byte-order
             0,
             // 31 bits per word
             1,
             b.GetRep());
  if (count == 0) {
    // BigInteger wants one limb always.
    limbs[0].x = 0;
    return 1;
  }
  return count;
}

void BigIntToFixedLimbs(const BigInt &b, size_t num_limbs, limb *limbs) {
  size_t count = 0;
  mpz_export(limbs, &count,
             // words are little endian.
             -1,
             // word size
             sizeof(int),
             // native byte-order
             0,
             // 31 bits per word
             1,
             b.GetRep());

  // This also handles the case where there were no limbs.
  while (count < num_limbs) {
    limbs[count].x = 0;
    count++;
  }
}
