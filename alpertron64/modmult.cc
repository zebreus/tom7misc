// This file is part of Alpertron Calculators.
//
// Copyright 2015-2021 Dario Alejandro Alpern
//
// Alpertron Calculators is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Alpertron Calculators is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Alpertron Calculators.  If not, see <http://www.gnu.org/licenses/>.

#include "modmult.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <assert.h>

#include <bit>
#include <memory>
#include <tuple>
#include <cstdint>

#include "bignbr.h"
#include "bignum/big.h"
#include "bignum/big-overloads.h"
#include "base/logging.h"
#include "bigconv.h"
#include "base/stringprintf.h"

static constexpr bool VERBOSE = false;
static constexpr bool SELF_CHECK = false;

// PERF: Can do this with a loop, plus bit tricks to avoid
// division.
std::tuple<int64_t, int64_t, int64_t>
ReferenceExtendedGCD64Internal(int64_t a, int64_t b) {
  if (a == 0) return std::make_tuple(b, 0, 1);
  const auto &[gcd, x1, y1] = ReferenceExtendedGCD64Internal(b % a, a);
  return std::make_tuple(gcd, y1 - (b / a) * x1, x1);
}

std::tuple<int64_t, int64_t, int64_t>
ReferenceExtendedGCD64(int64_t a, int64_t b) {
  const auto &[gcd, x, y] = ReferenceExtendedGCD64Internal(a, b);
  if (gcd < 0) return std::make_tuple(-gcd, -x, -y);
  else return std::make_tuple(gcd, x, y);
}

std::tuple<int64_t, int64_t, int64_t>
ExtendedGCD64Internal(int64_t a, int64_t b) {
  if (VERBOSE)
    printf("gcd(%lld, %lld)\n", a, b);

  a = abs(a);
  b = abs(b);

  if (a == 0) return std::make_tuple(b, 0, 1);
  if (b == 0) return std::make_tuple(a, 1, 0);


  // Remove common factors of 2.
  const int r = std::countr_zero<uint64_t>(a | b);
  a >>= r;
  b >>= r;

  int64_t alpha = a;
  int64_t beta = b;

  if (VERBOSE) {
    printf("Alpha: %lld, Beta: %lld\n", alpha, beta);
  }

  int64_t u = 1, v = 0, s = 0, t = 1;

  if (VERBOSE) {
    printf("2Loop %lld = %lld alpha + %lld beta | "
           "%lld = %lld alpha + %lld beta\n",
           a, u, v, b, s, t);
  }

  if (SELF_CHECK) {
    CHECK(a == u * alpha + v * beta) << a << " = "
                                     << u * alpha << " + "
                                     << v * beta << " = "
                                     << (u * alpha) + (v * beta);
    CHECK(b == s * alpha + t * beta);
  }

  int azero = std::countr_zero<uint64_t>(a);
  if (azero > 0) {

    int uvzero = std::countr_zero<uint64_t>(u | v);

    // shift away all the zeroes in a.
    a >>= azero;

    int all_zero = std::min(azero, uvzero);
    u >>= all_zero;
    v >>= all_zero;

    int rzero = azero - all_zero;
    if (VERBOSE)
      printf("azero %d uvzero %d all_zero %d rzero %d\n",
             azero, uvzero, all_zero, rzero);

    for (int i = 0; i < rzero; i++) {
      // PERF: The first time through, we know we will
      // enter the top branch.
      if ((u | v) & 1) {
        u += beta;
        v -= alpha;
      }

      u >>= 1;
      v >>= 1;
    }
  }

  while (a != b) {
    if (VERBOSE) {
      printf("Loop %lld = %lld alpha + %lld beta | "
             "%lld = %lld alpha + %lld beta\n",
             a, u, v, b, s, t);
    }

    if (SELF_CHECK) {
      CHECK(a == u * alpha + v * beta) << a << " = "
                                       << u * alpha << " + "
                                       << v * beta << " = "
                                       << (u * alpha) + (v * beta);
      CHECK(b == s * alpha + t * beta);

      CHECK((a & 1) == 1);
    }

    // Loop invariant.
    // PERF: I think that this loop could still be improved.
    // I explicitly skip some of the tests that gcc couldn't
    // figure out, but it still generates explicit move
    // instructions for the swap; we could just have six
    // states and track that manually.
    if ((a & 1) == 0) __builtin_unreachable();

    // one:
    if ((b & 1) == 0) {
    one_even:
      if (SELF_CHECK) { CHECK((b & 1) == 0); }

      b >>= 1;
      if (((s | t) & 1) == 0) {
        s >>= 1;
        t >>= 1;
      } else {
        s = (s + beta) >> 1;
        t = (t - alpha) >> 1;
      }

      // could cause a to equal b
      continue;
    }

    // two:
    if (b < a) {
      // printf("Swap.\n");
      std::swap(a, b);
      std::swap(s, u);
      std::swap(t, v);

      // we know a is odd, and now a < b, so we
      // go to case three.
      goto three;
    }

  three:
    b -= a;
    s -= u;
    t -= v;
    if (SELF_CHECK) {
      // we would only have b == a here if b was 2a.
      // but this is impossible since b was odd.
      CHECK(b != a);
      // but since we had odd - odd, we b is now even.
      CHECK((b & 1) == 0);
    }
    // so we know we enter that branch next.
    goto one_even;
  }

  return std::make_tuple(a << r, s, t);
}

std::tuple<int64_t, int64_t, int64_t>
ExtendedGCD64(int64_t a, int64_t b) {
  const auto &[gcd, x, y] = ExtendedGCD64Internal(a, b);
  if (SELF_CHECK) {
    CHECK(gcd >= 0);
  }
  // Negate coefficients if they start negative.
  return std::make_tuple(gcd, a < 0 ? -x : x, b < 0 ? -y : y);
}

static
void ComputeInversePower2(const limb *value, /*@out@*/limb *result,
                          int number_length);


// Note this reads *before* the limb pointer.
static double getMantissa(const limb *ptrLimb, int nbrLimbs) {
  assert(nbrLimbs >= 1);
  double dN = (double)(ptrLimb - 1)->x;
  double dInvLimb = 1.0 / (double)LIMB_RANGE;
  if (nbrLimbs > 1) {
    dN += (double)(ptrLimb - 2)->x * dInvLimb;
  }

  if (nbrLimbs > 2) {
    dN += (double)(ptrLimb - 3)->x * dInvLimb * dInvLimb;
  }

  return dN;
}

// Multiply big number in Montgomery notation by integer.
static void ModMultInt(limb* factorBig, int factorInt, limb* result,
                       const limb* pTestNbr, int nbrLen) {

  if (nbrLen == 1) {
    // "small" modular multiplication
    int factor1 = factorBig->x;
    int factor2 = factorInt;
    int mod = pTestNbr->x;
    if (mod < SMALL_NUMBER_BOUND) {
      result->x = factor1 * factor2 % mod;
    } else {
      // TestNbr has one limb but it is not small.
      result->x = (int64_t)factor1 * factor2 % mod;
    }
    return;
  }

  (factorBig + nbrLen)->x = 0;
  double dTestNbr = getMantissa(pTestNbr + nbrLen, nbrLen);
  double dFactorBig = getMantissa(factorBig + nbrLen, nbrLen);
  int TrialQuotient =
    (int)(unsigned int)floor((dFactorBig * (double)factorInt / dTestNbr) + 0.5);
  if ((unsigned int)TrialQuotient >= LIMB_RANGE) {
    // Maximum value for limb.
    TrialQuotient = MAX_VALUE_LIMB;
  }
  // Compute result as factorBig * factorInt - TrialQuotient * TestNbr
  limb *ptrFactorBig = factorBig;
  const limb *ptrTestNbr = pTestNbr;

  int64_t carry = 0;
  for (int i = 0; i <= nbrLen; i++) {
    carry += ((int64_t)ptrFactorBig->x * factorInt) -
      ((int64_t)TrialQuotient * ptrTestNbr->x);
    (result + i)->x = (int)carry & MAX_INT_NBR;
    carry >>= BITS_PER_GROUP;
    ptrFactorBig++;
    ptrTestNbr++;
  }

  while (((unsigned int)(result + nbrLen)->x & MAX_VALUE_LIMB) != 0U) {
    ptrFactorBig = result;
    ptrTestNbr = pTestNbr;
    unsigned int cy = 0;
    for (int i = 0; i <= nbrLen; i++) {
      cy += (unsigned int)ptrTestNbr->x + (unsigned int)ptrFactorBig->x;
      ptrFactorBig->x = UintToInt(cy & MAX_VALUE_LIMB);
      cy >>= BITS_PER_GROUP;
      ptrFactorBig++;
      ptrTestNbr++;
    }
  }
}

// Compute power = base^exponent (mod modulus)
// Assumes GetMontgomeryParams routine for modulus already called.
// This works only for odd moduli.
BigInt BigIntModularPower(const MontgomeryParams &params,
                          const BigInt &Base, const BigInt &Exponent) {

  if (VERBOSE) {
    BigInt Modulus = LimbsToBigInt(params.modulus.data(),
                                   params.modulus_length);
    printf("[%d] BIMP %s^%s mod %s\n",
           params.modulus_length,
           Base.ToString().c_str(),
           Exponent.ToString().c_str(),
           Modulus.ToString().c_str());
  }

  limb tmp5[params.modulus_length];
  BigIntToFixedLimbs(Base, params.modulus_length, tmp5);

  limb tmp6[params.modulus_length];
  // Convert base to Montgomery notation.
  ModMult(params, tmp5, params.R2.data(), tmp6);
  ModPow(params, tmp6, Exponent, tmp5);

  const int lenBytes = params.modulus_length * (int)sizeof(limb);
  limb tmp4[params.modulus_length];
  // Convert power to standard notation.
  // (This appears to compute tmp6 <- 1 * tmp5 % modulus ?
  // I guess 1 is not literally the identity because we do
  // the multiplication in montgomery form. -tom7)
  if (VERBOSE) {
    printf("memset %d bytes (modulus_length = %d)\n",
           lenBytes, params.modulus_length);
    fflush(stdout);
  }
  (void)memset(tmp4, 0, lenBytes);
  tmp4[0].x = 1;
  ModMult(params, tmp4, tmp5, tmp6);

  return LimbsToBigInt(tmp6, params.modulus_length);
}

// Input: base = base in Montgomery notation.
//        exp  = exponent.
//        nbrGroupsExp = number of limbs of exponent.
// Output: power = power in Montgomery notation.
void ModPow(const MontgomeryParams &params,
            const limb* base, const BigInt &Exp, limb* power) {
  // We could probably extract bits directly?
  const int nbrGroupsExp = BigIntNumLimbs(Exp);
  limb exp[nbrGroupsExp];
  BigIntToFixedLimbs(Exp, nbrGroupsExp, exp);

  // Port note: Original code copied 1 additional limb here. Just
  // seems wrong to me (power limbs should not need to exceed modulus
  // size); might be related to some superstitious zero padding?
  int lenBytes = params.modulus_length * (int)sizeof(limb);
  (void)memcpy(power, params.R1.data(), lenBytes);  // power <- 1
  for (int index = nbrGroupsExp - 1; index >= 0; index--) {
    int groupExp = (exp + index)->x;
    for (unsigned int mask = HALF_INT_RANGE_U; mask > 0U; mask >>= 1) {
      ModMult(params, power, power, power);
      if (((unsigned int)groupExp & mask) != 0U) {
        ModMult(params, power, base, power);
      }
    }
  }
}

BigInt ModPowBaseInt(const MontgomeryParams &params,
                     int base, const BigInt &Exp) {
  const int nbrGroupsExp = BigIntNumLimbs(Exp);
  limb exp[nbrGroupsExp];
  BigIntToFixedLimbs(Exp, nbrGroupsExp, exp);

  // XXX switch to modulus_length? ModMultInt does write an extra zero
  // explicitly.
  limb power[params.modulus_length + 1];

  int NumberLengthBytes = (params.modulus_length + 1) * (int)sizeof(limb);
  // power <- 1
  (void)memcpy(power, params.R1.data(), NumberLengthBytes);
  for (int index = nbrGroupsExp - 1; index >= 0; index--) {
    int groupExp = exp[index].x;
    for (unsigned int mask = HALF_INT_RANGE_U; mask > 0U; mask >>= 1) {
      ModMult(params, power, power, power);
      if (((unsigned int)groupExp & mask) != 0U) {
        ModMultInt(power, base, power,
                   params.modulus.data(),
                   params.modulus_length);
      }
    }
  }

  return LimbsToBigInt(power, params.modulus_length);
}

// Find the inverse of value mod 2^(number_length*BITS_PER_GROUP)
// Writes number_length limbs to result.
static void ComputeInversePower2(
    const limb *value, limb *result, int number_length) {
  limb tmp[number_length * 2];
  limb tmp2[number_length * 2];
  // Routine below expects zero padding (first multiply is length 2).
  memset(tmp2, 0, number_length * 2 * sizeof(limb));
  unsigned int Cy;
  // 2 least significant bits of inverse correct.
  const uint32_t N = value->x;
  uint32_t x = N;
  x = x * (2 - (N * x));       // 4 least significant bits of inverse correct.
  x = x * (2 - (N * x));       // 8 least significant bits of inverse correct.
  x = x * (2 - (N * x));       // 16 least significant bits of inverse correct.
  x = x * (2 - (N * x));       // 32 least significant bits of inverse correct.
  tmp2->x = UintToInt((unsigned int)x & MAX_VALUE_LIMB);

  for (int currLen = 2; currLen < number_length; currLen <<= 1) {

    if (VERBOSE) {
      printf("[CIP2 loop top] curr %d < %d, value | tmp2:\n",
             currLen, number_length);
      for (int z = 0; z < number_length; z++) {
        printf("  %08x | %08x %c\n",
               value[z].x, tmp2[z].x, z < currLen ? '#' : ' ');
      }
    }

    MultiplyLimbs(value, tmp2, tmp, currLen);    // tmp <- N * x

    if (VERBOSE) {
      printf("[CIP after mult] tmp:\n");
      for (int z = 0; z < number_length; z++) {
        printf("  %08x %c\n",
               tmp[z].x, z < (currLen * 2) ? '#' : ' ');
      }
    }

    Cy = 2U - (unsigned int)tmp[0].x;
    tmp[0].x = UintToInt(Cy & MAX_VALUE_LIMB);
    if (VERBOSE) {
      printf("  Cy = %08x tmp[0] = %d\n", Cy, tmp[0].x);
    }
    // tmp <- 2 - N * x
    for (int j = 1; j < currLen; j++) {
      Cy = (unsigned int)(-tmp[j].x) - (Cy >> BITS_PER_GROUP);
      tmp[j].x = UintToInt(Cy & MAX_VALUE_LIMB);
      if (VERBOSE)
        printf("  Cy = %08x tmp[%d] = %d\n", Cy, j, tmp[j].x);
    }
    // tmp <- x * (2 - N * x)
    MultiplyLimbs(tmp2, tmp, tmp2, currLen);
  }

  if (VERBOSE) {
    printf("[CIP2 last approx] tmp2:\n");
    for (int i = 0; i < 6; i++)
      printf("  %08x %c\n", tmp2[i].x,
             i < number_length ? '*' : ' ');
    printf("[CIP2 last approx] tmp:\n");
    for (int i = 0; i < 6; i++)
      printf("  %08x %c\n", tmp[i].x,
             i < number_length ? '*' : ' ');
  }

  // Perform last approximation to inverse.
  MultiplyLimbs(value, tmp2, tmp, number_length);    // tmp <- N * x
  Cy = 2U - (unsigned int)tmp[0].x;
  tmp[0].x = UintToInt(Cy & MAX_VALUE_LIMB);
  // tmp <- 2 - N * x
  for (int j = 1; j < number_length; j++) {
    Cy = (unsigned int)(-tmp[j].x) - (Cy >> BITS_PER_GROUP);
    tmp[j].x = UintToInt(Cy & MAX_VALUE_LIMB);
  }

  if (VERBOSE) {
    printf("[CIP2 final multiply] tmp2:\n");
    for (int i = 0; i < 6; i++)
      printf("  %08x %c\n", tmp2[i].x,
             i < number_length ? '*' : ' ');
    printf("[CIP2 final multiply] tmp:\n");
    for (int i = 0; i < 6; i++)
      printf("  %08x %c\n", tmp[i].x,
             i < number_length ? '*' : ' ');
  }

  // tmp <- x * (2 - N * x)
  MultiplyLimbs(tmp2, tmp, tmp2, number_length);
  memcpy(result, tmp2, number_length * sizeof(limb));
}

// Compute Nbr <- Nbr mod Modulus.
// Modulus has NumberLength limbs.
// This writes one past the end of Nbr.
static void AdjustModN(limb *Nbr, const limb *Modulus, int nbrLen) {
  double dInvModulus = 1/getMantissa(Modulus+nbrLen, nbrLen);
  double dNbr = getMantissa(Nbr + nbrLen + 1, nbrLen + 1) * LIMB_RANGE;
  int TrialQuotient = (int)(unsigned int)floor((dNbr * dInvModulus) + 0.5);
  if ((unsigned int)TrialQuotient >= LIMB_RANGE) {
    // Maximum value for limb.
    TrialQuotient = MAX_VALUE_LIMB;
  }

  // Compute Nbr <- Nbr - TrialQuotient * Modulus
  int64_t carry = 0;
  int i;
  for (i = 0; i <= nbrLen; i++) {
    carry += (int64_t)(Nbr+i)->x - ((Modulus+i)->x * (int64_t)TrialQuotient);
    (Nbr + i)->x = UintToInt((unsigned int)carry & MAX_VALUE_LIMB);
    carry >>= BITS_PER_GROUP;
  }

  // Port note: The original code would continue writing the carry
  // to Nbr[nbrLen + 1], i.e. two past the end. This isn't used below
  // and doesn't seem to be used elsewhere either; I removed it
  // because it seems surprising and likely a mistake.
  // (Nbr + i)->x = carry & MAX_INT_NBR;

  if (((unsigned int)Nbr[nbrLen].x & MAX_VALUE_LIMB) != 0U) {
    unsigned int cy = 0;
    for (i = 0; i < nbrLen; i++) {
      cy += (unsigned int)(Nbr + i)->x + (unsigned int)(Modulus+i)->x;
      (Nbr + i)->x = UintToInt(cy & MAX_VALUE_LIMB);
      cy >>= BITS_PER_GROUP;
    }
    (Nbr + nbrLen)->x = 0;
  }
}

// With modulus length and modulus filled in. Other fields in unspecified
// state.
static void InitMontgomeryParams(MontgomeryParams *params) {
  CHECK((int)params->modulus.size() == params->modulus_length + 1);

  // Unless we detect a power of two below (and it is more than one
  // limb, etc.).
  params->powerOf2Exponent = 0;

  // We don't bother with montgomery form for (odd) single-word numbers.
  if (params->modulus_length == 1 && (params->modulus[0].x & 1) != 0) {
    // Original code doesn't set Ninv?
    // params->Ninv.resize(2);
    // params->N[0].x = 1;

    printf("modulus length 1\n");

    params->R1.resize(2);
    params->R2.resize(2);
    params->R1[0].x = 1;
    params->R2[0].x = 1;
    return;
  }

  // Check if the modulus is a power of two. First we do a quick check if
  // the most significant word has a single bit set, then loop to check
  // the less-significant words for zero.
  {
    static_assert(sizeof (limb) == 4);
    const uint32_t value = params->modulus[params->modulus_length - 1].x;
    if (std::popcount<uint32_t>(value) == 1) {
      // Single bit in most significant word. Are the other words
      // zero?
      const bool all_zero = [&]() {
          for (int j = 0; j < params->modulus_length - 1; j++)
            if (params->modulus[j].x != 0)
              return false;
          return true;
        }();

      if (all_zero) {
        // Modulus is a power of 2 then.

        const int msw_zero_bits = std::countr_zero<uint32_t>(value);

        params->powerOf2Exponent =
          (params->modulus_length - 1) * BITS_PER_GROUP +
          msw_zero_bits;

        const int NumberLengthBytes =
          (params->modulus_length + 1) * (int)sizeof(limb);
        params->R1.resize(params->modulus_length + 1);
        params->R2.resize(params->modulus_length + 1);
        (void)memset(params->R1.data(), 0, NumberLengthBytes);
        (void)memset(params->R2.data(), 0, NumberLengthBytes);
        params->R1[0].x = 1;
        params->R2[0].x = 1;

        return;
      }
    }
  }

  // Compute Ninv as 1/modulus (mod 2^k) using Newton method,
  // which doubles the precision for each iteration.
  // In the formula above: k = BITS_PER_GROUP * modulus_length.
  params->Ninv.resize(params->modulus_length + 1);
  memset(params->Ninv.data(), 0,
         (params->modulus_length + 1) * sizeof (limb));
  ComputeInversePower2(params->modulus.data(),
                       params->Ninv.data(),
                       params->modulus_length);
  params->Ninv[params->modulus_length].x = 0;

  params->R1.resize(params->modulus_length + 1);
  params->R2.resize(params->modulus_length + 1);

  // Compute R1 as 1 in Montgomery notation.
  // This is 2^(modulus_length*BITS_PER_GROUP) % modulus.
  int j = params->modulus_length;
  params->R1[j].x = 1;
  do {
    j--;
    params->R1[j].x = 0;
  } while (j > 0);

  AdjustModN(params->R1.data(),
             params->modulus.data(),
             params->modulus_length);
  params->R1[params->modulus_length].x = 0;

  const int PaddedNumberLengthBytes =
    (params->modulus_length + 1) * (int)sizeof(limb);
  (void)memcpy(params->R2.data(), params->R1.data(),
               PaddedNumberLengthBytes);

  // Port note: NumberLengthR1 used to be a member of the struct (I
  // guess before that a global variable), but it's only used during
  // initialization. Other code assumes R1 is modulus_length size,
  // which seems right.
  for (int NumberLengthR1 = params->modulus_length;
       NumberLengthR1 > 0;
       NumberLengthR1--) {
    if (params->R1[NumberLengthR1 - 1].x != 0) {
      break;
    }
  }

  // Compute R2 as 2^(2*modulus_length*BITS_PER_GROUP) % modulus.
  for (int i = params->modulus_length; i > 0; i--) {
    const int NumberLengthBytes =
      params->modulus_length * (int)sizeof(limb);
    (void)memmove(&params->R2[1],
                  &params->R2[0],
                  NumberLengthBytes);
    params->R2[0].x = 0;
    AdjustModN(params->R2.data(),
               params->modulus.data(),
               params->modulus_length);
  }
}

// XXX test failure (modpowbaseint)
constexpr bool USE_SIMPLE_MODULUS = false;

std::unique_ptr<MontgomeryParams>
GetMontgomeryParams(uint64_t modulus) {
  std::unique_ptr<MontgomeryParams> params =
    std::make_unique<MontgomeryParams>();

  const BigInt Modulus(modulus);
  // printf("Modulus: %llu = %s\n", modulus, Modulus.ToString().c_str());

  if (USE_SIMPLE_MODULUS) {
    params->simple_modulus = modulus;
    // Enough to hold any 64-bit number.

    // const int mod_len = BigIntNumLimbs(Modulus);
    constexpr int mod_len = 3;
    params->modulus_length = mod_len;
    params->modulus.resize(mod_len + 1);
    params->R1.resize(mod_len + 1);
    params->R2.resize(mod_len + 1);
    BigIntToFixedLimbs(Modulus, mod_len + 1, params->modulus.data());
    BigIntToFixedLimbs(BigInt(1), mod_len + 1, params->R1.data());
    BigIntToFixedLimbs(BigInt(1), mod_len + 1, params->R2.data());
    // Don't need to initialize Ninv.
    // We use native even if it's a power of 2.
    params->powerOf2Exponent = 0;

    return params;
  } else {
    params->modulus_length = BigIntNumLimbs(Modulus);
    // With space for required padding.
    params->modulus.resize(params->modulus_length + 1);
    BigIntToFixedLimbs(Modulus, params->modulus_length, params->modulus.data());
    params->modulus[params->modulus_length].x = 0;

    InitMontgomeryParams(params.get());
    return params;
  }
}


void AddBigNbrModN(const limb *num1, const limb *num2, limb *sum,
                   const limb *modulus_array, int number_length) {
  BigInt f1 = LimbsToBigInt(num1, number_length);
  BigInt f2 = LimbsToBigInt(num2, number_length);
  BigInt modulus = LimbsToBigInt(modulus_array, number_length);
  BigInt r = BigInt::Mod(BigInt::Plus(f1, f2), modulus);
  BigIntToFixedLimbs(r, number_length, sum);
}

void SubtBigNbrModN(const limb *num1, const limb *num2, limb *diff,
                    const limb *modulus_array, int number_length) {
  BigInt f1 = LimbsToBigInt(num1, number_length);
  BigInt f2 = LimbsToBigInt(num2, number_length);
  BigInt modulus = LimbsToBigInt(modulus_array, number_length);
  BigInt r = BigInt::Mod(BigInt::Minus(f1, f2), modulus);
  BigIntToFixedLimbs(r, number_length, diff);
}

static void endBigModmult(const limb *prodNotAdjusted, limb *product,
                          int modulus_length, const limb *modulus_array) {
  unsigned int cy = 0;
  // Compute hi(T) - hi(mN)
  // Where hi(number) is the high half of number.
  int index = modulus_length;
  int count;
  for (count = 0; count < modulus_length; count++) {
    cy = (unsigned int)(product + index)->x -
      (unsigned int)(prodNotAdjusted+index)->x -
      (cy >> BITS_PER_GROUP);
    (product + count)->x = UintToInt(cy & MAX_VALUE_LIMB);
    index++;
  }
  // Check whether this number is less than zero.
  if ((int)cy < 0) {
    // The number is less than zero. Add TestNbr.
    cy = 0;
    for (count = 0; count < modulus_length; count++) {
      cy = (unsigned int)(product + count)->x +
        (unsigned int)modulus_array[count].x +
        (cy >> BITS_PER_GROUP);
      (product + count)->x = UintToInt(cy & MAX_VALUE_LIMB);
    }
  }
}


// Multiply two numbers in Montgomery notation.
//
// For large numbers the REDC algorithm is:
// m <- ((T mod R)N') mod R
// t <- (T - mN) / R
// if t < 0 then
//   return t + N
// else
//   return t
// end if
//
// PERF: This is about 25% of the program's execution, and implicated
// in other expensive stuff (ModPowBaseInt, ModularDivision). It'd be
// good to port this directly to BigInt, or restore the tricks that
// used to be here for fixed size multiplications.
//
//
// If modulus is bigger than one limb, this expects the arguments to
// be in Montgomery form. It would be better if this were expressed
// by the MontgomeryParams, not a shared secret.
// https://en.wikipedia.org/wiki/Montgomery_modular_multiplication
void ModMult(const MontgomeryParams &params,
             const limb* factor1, const limb* factor2,
             limb* product) {

  if (params.simple_modulus > 0) {
    CHECK(USE_SIMPLE_MODULUS);

    const BigInt f1 = LimbsToBigInt(factor1, params.modulus_length);
    const BigInt f2 = LimbsToBigInt(factor2, params.modulus_length);

    int64_t r = (f1 * f2) % params.simple_modulus;
    if (r < 0) r += params.simple_modulus;
    BigIntToFixedLimbs(BigInt(r), params.modulus_length, product);
    return;
  }

  if (VERBOSE) {
    printf("ModMult params modulus_length: %d, pow2: %d\n",
           params.modulus_length, params.powerOf2Exponent);

    const BigInt f1 = LimbsToBigInt(factor1, params.modulus_length);
    const BigInt f2 = LimbsToBigInt(factor2, params.modulus_length);
    const BigInt modulus = LimbsToBigInt(params.modulus.data(),
                                         params.modulus_length);

    printf("[%d] %s * %s mod %s = ",
           params.modulus_length,
           f1.ToString().c_str(), f2.ToString().c_str(),
           modulus.ToString().c_str());
  }

  if (params.powerOf2Exponent != 0) {
    if (VERBOSE)
      printf("pow2 exponent = %d\n", params.powerOf2Exponent);

    // PERF: We could store the mask instead of the power?
    const BigInt Mask = (BigInt(1) << params.powerOf2Exponent) - 1;

    const BigInt f1 = LimbsToBigInt(factor1, params.modulus_length);
    const BigInt f2 = LimbsToBigInt(factor2, params.modulus_length);

    if (VERBOSE) {
      printf("pow2 factor1 %s\n", f1.ToString().c_str());
      printf("pow2 factor2 %s\n", f2.ToString().c_str());
      BigInt Product = (f1 * f2);
      printf("pow2 product %s\n", Product.ToString().c_str());
    }

    const BigInt Product = (f1 * f2) & Mask;
    BigIntToFixedLimbs(Product, params.modulus_length, product);

    if (VERBOSE) {
      const BigInt ret = LimbsToBigInt(product, params.modulus_length);
      printf("(A) %s\n",
             ret.ToString().c_str());
    }

    return;
  }

  if (params.modulus_length <= 1) {
    CHECK(params.modulus_length == 1);

    const uint64_t f1 = factor1[0].x;
    const uint64_t f2 = factor2[0].x;
    const uint32_t m = params.modulus[0].x;

    uint32_t r = (f1 * f2) % m;

    // because m must be less than this
    CHECK(r <= MAX_INT_NBR_U);

    product[0].x = r;

    if (VERBOSE) {
      const BigInt ret = LimbsToBigInt(product, params.modulus_length);
      printf("(B) %s\n",
             ret.ToString().c_str());
    }

    return;
  }

  {
    // PERF: All MultiplyLimbs does is multiply using BigInt, so we can
    // probably just do the first several steps without converting.

    limb aux[2 * params.modulus_length], aux2[2 * params.modulus_length];
    // Port note: Original code uses product instead of temporary,
    // but then we'd require the product to be 2*modulus_length.
    limb aux3[2 * params.modulus_length];
    // Compute T
    MultiplyLimbs(factor1, factor2, aux3, params.modulus_length);
    // Compute m
    MultiplyLimbs(aux3, params.Ninv.data(), aux, params.modulus_length);
    // Compute mN
    MultiplyLimbs(aux, params.modulus.data(), aux2, params.modulus_length);
    // This is likely the last step of REDC, the conditional
    // subtraction.
    endBigModmult(aux2, aux3, params.modulus_length, params.modulus.data());

    // Copy back to product.
    memcpy(product, aux3, params.modulus_length * sizeof(limb));

    if (VERBOSE) {
      const BigInt ret = LimbsToBigInt(product, params.modulus_length);
      printf("(C) %s\n",
             ret.ToString().c_str());
    }

    return;
  }
}
