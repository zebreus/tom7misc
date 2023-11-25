#ifndef _MODMULT_H
#define _MODMULT_H

#include <memory>
#include <tuple>
#include <cstdint>
#include <vector>

#include "bignbr.h"

#include "base/int128.h"
#include "base/logging.h"

// These used to be globals. Now calling GetMontgomeryParams* creates them.
struct MontgomeryParams {
  // R is the power of 2 for Montgomery multiplication and reduction.

  // This is the inverse of the modulus mod R.
  // It's not computed (empty) when we aren't using montgomery form.
  std::vector<limb> Ninv;
  // This is the representation of 1 in Montgomery form.
  std::vector<limb> R1;
  std::vector<limb> R2;
  // modulus_length + 1 limbs, with a zero at the end.
  std::vector<limb> modulus;

  int modulus_length = 0;
  // If nonzero, the modulus is this power of two.
  int powerOf2Exponent = 0;

  // if simple_modulus > 0, then R1=R2=1, and we simply
  // do native modular multiplication.
  uint64_t simple_modulus = 0;
};

// Same, but just using a bigint for the modulus. The returned
// parameters have the fixed modulus limbs and length.
std::unique_ptr<MontgomeryParams>
GetMontgomeryParams(uint64_t modulus);

// The form of the numbers is determined by the params, so to use this you
// must be consistently using params. If modulus_length == 1,
// we assume regular numbers. If a power of 2, same (but this uses a fast
// method, as mod by power of 2 is easy). Otherwise, everything is in
// montgomery form.
// product <- factor1 * factor2 mod modulus
void ModMult(const MontgomeryParams &params,
             const limb *factor1, const limb *factor2,
             limb *product);

// XXX doc
inline uint64_t ModMult64(const MontgomeryParams &params,
                          uint64_t a, uint64_t b) {
  CHECK(params.simple_modulus > 0);

  // PERF: If the inputs are small enough, can just do a 64-bit
  // operation. We could test that the modulus is 32 bit when
  // constructing MontgomeryParams, for example?
  uint128_t aa = a;
  uint128_t bb = b;

  // PERF could check if product fits in 64 bits?

  uint128_t full_product = aa * bb;
  uint128_t residue = full_product % (uint128_t)params.simple_modulus;

  CHECK(Uint128High64(residue) == 0);

  return Uint128Low64(residue);
}

// Returns (gcd, x, y)
// where we have ax * by = gcd = gcd(a, b)
std::tuple<int64_t, int64_t, int64_t>
ExtendedGCD64(int64_t a, int64_t b);

// Slower recursive version of above for reference.
std::tuple<int64_t, int64_t, int64_t>
RefrerenceExtendedGCD64(int64_t a, int64_t b);

// compute a^1 mod b    for a,b coprime
inline int64_t ModularInverse64(int64_t a, int64_t b) {
  // if (a < 0 && b < 0) { a = -a; b = -b; }

  const int64_t absb = abs(b);

  const auto &[gcd, x, y] = ExtendedGCD64(a, absb);
  CHECK(gcd == 1) << "Precondition. gcd("
                  << a << ", " << b << "): " << gcd;
  // Now we have
  // ax + by = gcd = 1
  // and so
  // ax + by = 1 (mod b)
  //
  // ax = 1  (mod b)
  //
  // so x is a^1 mod b.

  if (x < 0) return x + absb;
  return x;
}

// Called with uint64_t n, so we could use that?
inline int Jacobi64(int64_t a, int64_t n) {
  CHECK(n > 0 && (n & 1) == 1) << "Precondition. n: " << n;

  int t = 1;
  a %= n;
  if (a < 0) a += n;

  while (a != 0) {

    // PERF could do this with countr_zero?
    // n is not changing in this loop.
    while ((a & 1) == 0) {
      a >>= 1;
      const int r = n % 8;
      if (r == 3 || r == 5) {
        t = -t;
      }
    }

    std::swap(n, a);
    if (a % 4 == 3 && n % 4 == 3) {
      t = -t;
    }

    a %= n;
  }

  if (n == 1) {
    return t;
  } else {
    return 0;
  }
}

inline int64_t DivFloor64(int64_t numer, int64_t denom) {
  // There's probably a version without %, but I verified
  // that gcc will do these both with one IDIV.
  int64_t q = numer / denom;
  int64_t r = numer % denom;
  // sign of remainder: -1, 0 or 1
  // int64_t sgn_r = (r > 0) - (r < 0);
  int64_t sgn_r = (r>>63) | ((uint64_t)-r >> 63);
  // The negated sign of the denominator. Note the denominator
  // can't be zero.
  int64_t sgn_neg_denom = denom < 0 ? 1 : -1;
  if (sgn_r == sgn_neg_denom) {
    return q - 1;
  }
  return q;

  // TODO: Benchmark these.
  //
  // An alternate version produces shorter code, but gcc generates
  // a branch for testing that r is nonzero:
  // if (r && ((r ^ denom) & (1LL << 63))) ...
  //
  // And the clearest expression generates loads of branches:
  // if ((r < 0 && denom > 0) || (r > 0 && denom < 0)) ...
}

// Returns base^exp mod n (which comes from MontgomeryParams).
BigInt ModPowBaseInt(const MontgomeryParams &params,
                     int base, const BigInt &Exp);

void ModPow(const MontgomeryParams &params,
            const limb *base, const BigInt &Exp, limb *power);

BigInt BigIntModularPower(const MontgomeryParams &params,
                          const BigInt &base, const BigInt &exponent);

void AddBigNbrModN(const limb *Nbr1, const limb *Nbr2, limb *Sum, const limb *TestNbr,
                   int number_length);
void SubtBigNbrModN(const limb *Nbr1, const limb *Nbr2, limb *Sum, const limb *TestNbr,
                    int number_length);

#endif
