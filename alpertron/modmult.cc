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

#include "bignbr.h"
#include "bignum/big.h"
#include "bignum/big-overloads.h"
#include "base/logging.h"
#include "bigconv.h"
#include "base/stringprintf.h"

static constexpr bool SELF_CHECK = false;
static constexpr bool VERBOSE = false;

[[maybe_unused]]
static std::string LimbString(limb *limbs, size_t num) {
  std::string out;
  for (size_t i = 0; i < num; i++) {
    if (i != 0) out.push_back(':');
    StringAppendF(&out, "%04x", limbs[i].x);
  }
  return out;
}

// Copies a fixed-width array of limbs to the bigint. Removes
// high limbs that are 0 (which are trailing in little-endian
// representation). I think this is "Uncompress" in the sense
// that BigInteger has a fixed buffer large enough for "any number",
// but in a way it is actually compression since the fixed-width
// represents zero high limbs, but this does not.
static void UncompressLimbsBigInteger(int number_length,
                                      const limb *ptrValues,
                                      /*@out@*/BigInteger *bigint) {
  assert(number_length >= 1);
  if (number_length == 1) {
    bigint->Limbs[0].x = ptrValues->x;
    bigint->nbrLimbs = 1;
  } else {
    int nbrLimbs;
    int numberLengthBytes = number_length * (int)sizeof(limb);
    (void)memcpy(bigint->Limbs.data(), ptrValues, numberLengthBytes);
    const limb *ptrValue1 = ptrValues + number_length;
    for (nbrLimbs = number_length; nbrLimbs > 1; nbrLimbs--) {
      ptrValue1--;
      if (ptrValue1->x != 0) {
        break;
      }
    }
    bigint->nbrLimbs = nbrLimbs;
  }

  // Port note: This didn't originally set the sign, but I think
  // that's just a bug. Note that a static BigInteger has positive
  // sign (0).
  bigint->sign = SIGN_POSITIVE;

  CHECK(bigint->nbrLimbs > 0);
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
  ModMult(params, tmp5, params.MontgomeryMultR2, tmp6);
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
  (void)memcpy(power, params.MontgomeryMultR1, lenBytes);  // power <- 1
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
  (void)memcpy(power, params.MontgomeryMultR1, NumberLengthBytes);
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

/* U' <- eU + fV, V' <- gU + hV                                        */
/* U <- U', V <- V'                                                    */
static void AddMult(limb* firstBig, int e, int f, limb* secondBig,
                    int g, int h, int nbrLen) {
  limb* ptrFirstBig = firstBig;
  limb* ptrSecondBig = secondBig;

  int64_t carryU = 0;
  int64_t carryV = 0;
  for (int ctr = 0; ctr <= nbrLen; ctr++) {
    int u = ptrFirstBig->x;
    int v = ptrSecondBig->x;
    carryU += (u * (int64_t)e) + (v * (int64_t)f);
    carryV += (u * (int64_t)g) + (v * (int64_t)h);
    ptrFirstBig->x = (int)(carryU & MAX_INT_NBR);
    ptrSecondBig->x = (int)(carryV & MAX_INT_NBR);
    ptrFirstBig++;
    ptrSecondBig++;
    carryU >>= BITS_PER_GROUP;
    carryV >>= BITS_PER_GROUP;
  }
}

// Perform first <- (first - second) / 2
// first must be greater than second.
static int HalveDifference(limb* first, const limb* second, int length) {
  int len = length;
  int i;
  // Perform first <- (first - second)/2.
  int borrow = first->x - second->x;
  int prevLimb = UintToInt((unsigned int)borrow & MAX_VALUE_LIMB);
  borrow >>= BITS_PER_GROUP;
  for (i = 1; i < len; i++) {
    int currLimb;
    borrow += (first + i)->x - (second + i)->x;
    currLimb = UintToInt((unsigned int)borrow & MAX_VALUE_LIMB);
    borrow >>= BITS_PER_GROUP;
    (first + i - 1)->x = UintToInt((((unsigned int)prevLimb >> 1) |
      ((unsigned int)currLimb << BITS_PER_GROUP_MINUS_1)) & MAX_VALUE_LIMB);
    prevLimb = currLimb;
  }

  (first + i - 1)->x = UintToInt((unsigned int)prevLimb >> 1);
  // Get length of result.
  len--;
  for (; len > 0; len--) {
    if ((first + len)->x != 0) {
      break;
    }
  }
  return len + 1;
}

static int modInv(int NbrMod, int currentPrime) {
  int T1;
  int T3;
  int V1 = 1;
  int V3 = NbrMod;
  int U1 = 0;
  int U3 = currentPrime;
  while (V3 != 0) {
    // Port note: U3 < V3 + V3 here used to possibly overflow
    if ((U3 >> 1) < V3) {
      // QQ = 1
      T1 = U1 - V1;
      T3 = U3 - V3;
    } else {
      int QQ = U3 / V3;
      T1 = U1 - (V1 * QQ);
      T3 = U3 - (V3 * QQ);
    }
    U1 = V1;
    U3 = V3;
    V1 = T1;
    V3 = T3;
  }
  return U1 + (currentPrime & (U1 >> 31));
}

static void InitHighUandV(
    limb *U, limb *V,
    int lenU, int lenV, double* pHighU, double* pHighV) {
  double highU;
  double highV;
  double dLimbRange = (double)LIMB_RANGE;
  if (lenV >= lenU) {
    highV = ((double)V[lenV - 1].x * dLimbRange) + (double)V[lenV - 2].x;
    if (lenV >= 3) {
      highV += (double)V[lenV - 3].x / dLimbRange;
    }

    if (lenV == lenU) {
      highU = ((double)U[lenV - 1].x * dLimbRange) + (double)U[lenV - 2].x;
    } else if (lenV == (lenU + 1)) {
      highU = (double)U[lenV - 2].x;
    } else {
      highU = 0;
    }

    if ((lenV <= (lenU + 2)) && (lenV >= 3)) {
      highU += (double)U[lenV - 3].x / dLimbRange;
    }
  } else {
    highU = ((double)U[lenU - 1].x * (double)LIMB_RANGE) + (double)U[lenU - 2].x;
    if (lenU >= 3) {
      highU += (double)U[lenU - 3].x / dLimbRange;
    }

    if (lenU == (lenV + 1)) {
      highV = (double)V[lenU - 2].x;
    } else {
      highV = 0;
    }

    if ((lenU <= (lenV + 2)) && (lenU >= 3)) {
      highV += (double)V[lenU - 3].x / dLimbRange;
    }
  }
  *pHighU = highU;
  *pHighV = highV;
}

/***********************************************************************/
/* NAME: ModInvBigNbr                                                  */
/*                                                                     */
/* PURPOSE: Find the inverse multiplicative modulo M.                  */
/* The algorithm terminates with inv = X^(-1) mod M.                   */
/*                                                                     */
/* This routine uses Kaliski Montgomery inverse algorithm              */
/* with changes by E. Savas and C. K. Koc.                             */
/* Step  #1: U <- M, V <- X, R <- 0, S <- 1, k <- 0                    */
/* Step  #2: while V > 0 do                                            */
/* Step  #3:   if U even then U <- U / 2, S <- 2S                      */
/* Step  #4:   elsif V even then V <- V / 2, R <- 2R                   */
/* Step  #5:   elsif U > V  then U <- (U - V) / 2, R <- R + S, S <- 2S */
/* Step  #6:   else V <- (V - U) / 2, S <- S + R, R <- 2R              */
/* Step  #7:   k <- k + 1                                              */
/* Step  #8. if R >= M then R <- R - M                                 */
/* Step  #9. R <- M - R                                                */
/* Step #10. R <- MonPro(R, R2)                                        */
/* Step #11. compute MonPro(R, 2^(m-k)) and return this value.         */
/*                                                                     */
/*  In order to reduce the calculations, several single precision      */
/*  variables are added:                                               */
/*                                                                     */
/* R' <- aR + bS, S' <-  cR + dS                                       */
/* U' <- aU - bV, V' <- -cU + dV                                       */
/***********************************************************************/
// Both modulus and num must have a 0 at params.modulus_length (one
// past their nominal size).
static bool ModInvBigNbr(const MontgomeryParams &params,
                         const limb* num, limb* inv) {
  int k;
  int steps;
  int a;
  int b;
  int c;
  int d;  // Coefficients used to update variables R, S, U, V.
  int i;
  int bitCount;
  int lenRS;
  int lenU;
  int lenV;
  unsigned int borrow;

  // Or inline 'em...
  const int modulus_length = params.modulus_length;
  const limb *modulus = params.modulus.data();

  printf("ModInvBigNbr len=%d\n", modulus_length);
  printf("num:\n");
  for (i = 0; i < 6; i++)
    printf("  %08x %c\n", num[i].x, i < modulus_length ? '*' : ' ' );
  printf("inv:\n");
  for (i = 0; i < 6; i++)
    printf("  %08x %c\n", inv[i].x, i < modulus_length ? '*' : ' ' );
  printf("mod:\n");
  for (i = 0; i < 6; i++)
    printf("  %08x %c\n", modulus[i].x, i < modulus_length ? '*' : ' ' );


  assert(modulus_length >= 1);
  if (modulus_length == 1) {
    inv->x = modInv(num->x, modulus->x);
    return true;
  }

  if (params.powerOf2Exponent != 0) {
    // modulus is a power of 2.
    unsigned int powerExp =
      (unsigned int)params.powerOf2Exponent % (unsigned int)BITS_PER_GROUP;
    ComputeInversePower2(num, inv, modulus_length);
    (inv + (params.powerOf2Exponent / BITS_PER_GROUP))->x &=
      UintToInt((1U << powerExp) - 1U);
    return true;
  }

  //  1. U <- M, V <- X, R <- 0, S <- 1, k <- 0
  const int size = (modulus_length + 1) * (int)sizeof(limb);
  CHECK(modulus[modulus_length].x == 0);
  // (modulus + modulus_length)->x = 0;

  CHECK(num[modulus_length].x == 0);

  limb U[modulus_length + 1];
  limb V[modulus_length + 1];
  (void)memcpy(U, modulus, size);
  (void)memcpy(V, num, size);

  // Maximum value of R and S can be up to 2*M, so one more limb is needed.
  // Note modulus_length+1 here does not work...
  limb R[MAX_LEN];
  limb S[MAX_LEN];
  (void)memset(R, 0, size);   // R <- 0
  (void)memset(S, 0, size);   // S <- 1
  S[0].x = 1;
  lenRS = 1;
  k = 0;
  steps = 0;
  // R' <- aR + bS, S' <- cR + dS
  a = 1;  // R' = R, S' = S.
  d = 1;
  b = 0;
  c = 0;

  // Find length of U.
  for (lenU = modulus_length - 1; lenU > 0; lenU--) {
    if (U[lenU].x != 0) {
      break;
    }
  }
  lenU++;

  // Find length of V.
  for (lenV = modulus_length - 1; lenV > 0; lenV--) {
    if (V[lenV].x != 0) {
      break;
    }
  }
  lenV++;

  uint32_t lowU = U[0].x;
  uint32_t lowV = V[0].x;

  // Initialize highU and highV.
  if ((lenU > 1) || (lenV > 1)) {
    double highU;
    double highV;
    InitHighUandV(U, V, lenU, lenV, &highU, &highV);
    //  2. while V > 0 do
    for (;;) {
      //  3.   if U even then U <- U / 2, S <- 2S
      if ((lowU & 1) == 0) {
        // U is even.
        lowU >>= 1;
        highV += highV;
        // R' <- aR + bS, S' <- cR + dS
        c *= 2;
        d *= 2;  // Multiply S by 2.
      } else if ((lowV & 1) == 0) {
        //  4.   elsif V even then V <- V / 2, R <- 2R
        // V is even.
        lowV >>= 1;
        highU += highU;
        // R' <- aR + bS, S' <- cR + dS
        a *= 2;
        b *= 2;  // Multiply R by 2.
      } else {
        //  5.   elsif U >= V  then U <- (U - V) / 2, R <- R + S, S <- 2S
        if (highU > highV) {
          // U > V. Perform U <- (U - V) / 2
          lowU = (lowU - lowV) / 2;
          highU -= highV;
          highV += highV;
          // R' <- aR + bS, S' <- cR + dS
          a += c;
          b += d;  // R <- R + S
          c *= 2;
          d *= 2;  // S <- 2S
        } else {
          //  6.   elsif V >= U then V <- (V - U) / 2, S <- S + R, R <- 2R
          // V >= U. Perform V <- (V - U) / 2
          lowV = (lowV - lowU) / 2;
          highV -= highU;
          highU += highU;
          // R' <- aR + bS, S' <- cR + dS
          c += a;
          d += b;  // S <- S + R
          a *= 2;
          b *= 2;  // R <- 2R
        }
      }

      //  7.   k <- k + 1
      // Adjust variables.
      steps++;
      if (steps == BITS_PER_GROUP_MINUS_1) {
        // compute now U and V and reset e, f, g and h.
        // U' <- eU + fV, V' <- gU + hV
        int len = ((lenU > lenV)? lenU : lenV);
        int lenBytes = (len - lenU + 1) * (int)sizeof(limb);
        (void)memset(&U[lenU].x, 0, lenBytes);
        lenBytes = (len - lenV + 1) * (int)sizeof(limb);
        (void)memset(&V[lenV].x, 0, lenBytes);
        lenBytes = (len + 1) * (int)sizeof(limb);
        // PERF: only need (len+1)
        limb Ubak[len + 1];
        limb Vbak[len + 1];
        (void)memcpy(Ubak, U, lenBytes);
        (void)memcpy(Vbak, V, lenBytes);
        AddMult(U, a, -b, V, -c, d, len);
        if ((((unsigned int)U[lenU].x | (unsigned int)V[lenV].x) &
             FOURTH_INT_RANGE_U) != 0U) {
          // Complete expansion of U and V required for all steps.
          //  2. while V > 0 do
          (void)memcpy(U, Ubak, lenBytes);
          (void)memcpy(V, Vbak, lenBytes);
          b = 0;
          c = 0;  // U' = U, V' = V.
          a = 1;
          d = 1;

          while ((lenV > 1) || (V[0].x > 0)) {
            //  3.   if U even then U <- U / 2, S <- 2S
            if ((U[0].x & 1) == 0) {
              // U is even.
              for (i = 0; i < lenU; i++) {
                // Loop that divides U by 2.
                U[i].x = UintToInt(
                    (((unsigned int)U[i].x >> 1) |
                     ((unsigned int)U[i + 1].x << BITS_PER_GROUP_MINUS_1)) &
                    MAX_VALUE_LIMB);
              }

              if (U[lenU - 1].x == 0) {
                lenU--;
              }
              // R' <- aR + bS, S' <- cR + dS
              c *= 2;
              d *= 2;  // Multiply S by 2.
            } else if ((V[0].x & 1) == 0) {
              //  4.   elsif V even then V <- V / 2, R <- 2R
              // V is even.
              for (i = 0; i < lenV; i++) {
                // Loop that divides V by 2.
                V[i].x = UintToInt(
                    (((unsigned int)V[i].x >> 1) |
                     ((unsigned int)V[i + 1].x << BITS_PER_GROUP_MINUS_1)) &
                    MAX_VALUE_LIMB);
              }

              if (V[lenV - 1].x == 0) {
                lenV--;
              }
              // R' <- aR + bS, S' <- cR + dS
              a *= 2;
              b *= 2;  // Multiply R by 2.
            } else {
              //  5.   elsif U >= V  then U <- (U - V) / 2, R <- R + S, S <- 2S
              len = ((lenU > lenV)? lenU : lenV);
              for (i = len - 1; i > 0; i--) {
                if (U[i].x != V[i].x) {
                  break;
                }
              }

              if (U[i].x > V[i].x) {     // U > V
                lenU = HalveDifference(U, V, len); // U <- (U - V) / 2
                // R' <- aR + bS, S' <- cR + dS
                a += c;
                b += d;  // R <- R + S
                c *= 2;
                d *= 2;  // S <- 2S
              } else {
                //  6.   elsif V >= U then V <- (V - U) / 2, S <- S + R, R <- 2R
                // V >= U
                lenV = HalveDifference(V, U, len); // V <- (V - U) / 2
                // R' <- aR + bS, S' <- cR + dS
                c += a;
                d += b;  // S <- S + R
                a *= 2;
                b *= 2;  // R <- 2R
              }
            }

            //  7.   k <- k + 1
            k++;
            if ((k % BITS_PER_GROUP_MINUS_1) == 0) {
              break;
            }
          }

          if ((lenV == 1) && (V[0].x == 0)) {
            break;
          }
        } else {
          k += steps;
          for (i = 0; i < lenU; i++) {
            // Loop that divides U by 2^BITS_PER_GROUP_MINUS_1.
            U[i].x = UintToInt(
                (((unsigned int)U[i].x >> BITS_PER_GROUP_MINUS_1) |
                 ((unsigned int)U[i + 1].x << 1)) &
                MAX_VALUE_LIMB);
          }
          U[lenU].x = 0;

          while ((lenU > 0) && (U[lenU - 1].x == 0)) {
            lenU--;
          }

          for (i = 0; i < lenV; i++) {
            // Loop that divides V by 2^BITS_PER_GROUP_MINUS_1.
            V[i].x = UintToInt((((unsigned int)V[i].x >> BITS_PER_GROUP_MINUS_1) |
                                ((unsigned int)V[i + 1].x << 1)) &
                               MAX_VALUE_LIMB);
          }

          V[lenV].x = 0;
          while ((lenV > 0) && (V[lenV - 1].x == 0)) {
            lenV--;
          }
        }

        steps = 0;
        AddMult(R, a, b, S, c, d, lenRS);
        if ((R[lenRS].x != 0) || (S[lenRS].x != 0)) {
          lenRS++;
        }
        lowU = U[0].x;
        lowV = V[0].x;
        b = 0;
        c = 0;  // U' = U, V' = V.
        a = 1;
        d = 1;
        if ((lenU == 0) || (lenV == 0) || ((lenV == 1) && (lenU == 1))) {
          break;
        }
        InitHighUandV(U, V, lenU, lenV, &highU, &highV);
      }
    }
  }

  if (lenU > 0) {
    //  2. while V > 0 do
    while (lowV > 0) {
      //  3.   if U even then U <- U / 2, S <- 2S
      if ((lowU & 1) == 0) {
        // U is even.
        lowU >>= 1;
        // R' <- aR + bS, S' <- cR + dS
        c *= 2;
        d *= 2;  // Multiply S by 2.
      } else if ((lowV & 1) == 0) {
        //  4.   elsif V even then V <- V / 2, R <- 2R
        // V is even.
        lowV >>= 1;
        // R' <- aR + bS, S' <- cR + dS
        a *= 2;
        b *= 2;  // Multiply R by 2.
      } else if (lowU > lowV) {
        //  5.   elsif U >= V  then U <- (U - V) / 2, R <- R + S, S <- 2S
        // U > V. Perform U <- (U - V) / 2
        lowU = (lowU - lowV) >> 1;
        // R' <- aR + bS, S' <- cR + dS
        a += c;
        b += d;  // R <- R + S
        c *= 2;
        d *= 2;  // S <- 2S
      } else {
        //  6.   elsif V >= U then V <- (V - U) / 2, S <- S + R, R <- 2R
        // V >= U. Perform V <- (V - U) / 2
        lowV = (lowV - lowU) >> 1;
        // R' <- aR + bS, S' <- cR + dS
        c += a;
        d += b;  // S <- S + R
        a *= 2;
        b *= 2;  // R <- 2R
      }

      //  7.   k <- k + 1
      steps++;
      if (steps >= BITS_PER_GROUP_MINUS_1) {
        // compute now R and S and reset a, b, c and d.
        // R' <- aR + bS, S' <- cR + dS
        AddMult(R, a, b, S, c, d, modulus_length + 1);
        b = 0;     // R' = R, S' = S.
        c = 0;
        a = 1;
        d = 1;
        k += steps;
        if (k > (modulus_length * 64)) {
          return false;  // Could not compute inverse.
        }
        steps = 0;
      }
    }
  }

  AddMult(R, a, b, S, c, d, modulus_length + 1);
  k += steps;
  //  8. if R >= M then R <- R - M
  for (i = modulus_length; i > 0; i--) {
    if (R[i].x != (modulus + i)->x) {
      break;
    }
  }

  if ((unsigned int)R[i].x >= (unsigned int)(modulus + i)->x) {
    // R >= M.
    borrow = 0U;
    for (i = 0; i <= modulus_length; i++) {
      borrow = (unsigned int)R[i].x - (unsigned int)(modulus + i)->x - borrow;
      R[i].x = UintToInt(borrow & MAX_VALUE_LIMB);
      borrow >>= BITS_PER_GROUP;
    }
  }

  //  9. R <- M - R
  borrow = 0U;
  for (i = 0; i <= modulus_length; i++) {
    borrow = (unsigned int)(modulus + i)->x - (unsigned int)R[i].x - borrow;
    R[i].x = UintToInt(borrow & MAX_VALUE_LIMB);
    borrow >>= BITS_PER_GROUP;
  }

  R[modulus_length].x = 0;
  // At this moment R = x^(-1)*2^k
  // 10. R <- MonPro(R, R2)
  ModMult(params, R, params.MontgomeryMultR2, R);

  R[modulus_length].x = 0;
  // At this moment R = x^(-1)*2^(k+m)
  // 11. return MonPro(R, 2^(m-k))
  (void)memset(S, 0, size);
  bitCount = (modulus_length * BITS_PER_GROUP) - k;

  if (bitCount < 0) {
    unsigned int shLeft;
    bitCount += modulus_length * BITS_PER_GROUP;
    shLeft = (unsigned int)bitCount % (unsigned int)BITS_PER_GROUP;
    S[bitCount / BITS_PER_GROUP].x = UintToInt(1U << shLeft);
    ModMult(params, R, S, inv);
  } else {
    unsigned int shLeft;
    shLeft = (unsigned int)bitCount % (unsigned int)BITS_PER_GROUP;
    S[bitCount / BITS_PER_GROUP].x = UintToInt(1U << shLeft);
    ModMult(params, R, S, inv);
    ModMult(params, inv, params.MontgomeryMultR2, inv);
  }

  return true;  // Inverse computed.
}

// PERF: This function is slow and used in inner loops.
// Compute modular division for odd moduli.
// params and modulus should match.
BigInt BigIntModularDivision(const MontgomeryParams &params,
                             BigInt Num, BigInt Den,
                             const BigInt &Mod) {

  const bool verbose = VERBOSE && (Num > Mod || Den > Mod);

  if (verbose) {
    printf("With (params), %s / %s mod %s",
           Num.ToString().c_str(),
           Den.ToString().c_str(),
           Mod.ToString().c_str());
  }

  // PERF: Fewer conversions of the modulus please!
  // PERF: Can dynamically size this, at least.
  // (Or, modulus could be part of params)

  if (SELF_CHECK) {
    limb TheModulus[MAX_LEN];
    int modulus_length = BigIntToLimbs(Mod, TheModulus);
    TheModulus[modulus_length].x = 0;

    CHECK(0 == memcmp(params.modulus.data(), TheModulus,
                      (modulus_length + 1) * sizeof (limb)));

    BigInteger mod;
    BigIntToBigInteger(Mod, &mod);
    CHECK(modulus_length == mod.nbrLimbs);
  }

  // Reduce Num modulo mod.
  Num %= Mod;
  if (Num < 0) Num += Mod;

  // Reduce Den modulo mod.
  Den %= Mod;
  if (Den < 0) Den += Mod;

  limb tmpDen[params.modulus_length];
  BigIntToFixedLimbs(Den, params.modulus_length, tmpDen);

  // ModInvBigNbr needs 1 padding word
  limb tmp3[params.modulus_length + 1];
  // tmp3 <- Den in Montgomery notation
  // tmpDen <- 1 / Den in Montg notation.
  ModMult(params, tmpDen, params.MontgomeryMultR2, tmp3);
  tmp3[params.modulus_length].x = 0;
  (void)ModInvBigNbr(params, tmp3, tmpDen);

  limb tmp4[params.modulus_length];
  BigIntToFixedLimbs(Num, params.modulus_length, tmp4);
  // tmp3 <- Num / Den in standard notation.
  ModMult(params, tmpDen, tmp4, tmp3);

  // Get Num/Den
  BigInt ret = LimbsToBigInt(tmp3, params.modulus_length);

  if (verbose) {
    printf("  = %s\n",
           ret.ToString().c_str());
  }

  return ret;
}

// On input:
// oddValue = odd modulus.
// resultModOdd = result mod odd value
// resultModPower2 = result mod 2^shRight
// result = pointer to result.
// From Knuth's TAOCP Vol 2, section 4.3.2:
// If c = result mod odd, d = result mod 2^k:
// compute result = c + (((d-c)*modinv(odd,2^k))%2^k)*odd
static BigInt ChineseRemainderTheorem(const MontgomeryParams &params,
                                      const BigInt &OddMod,
                                      int modulus_length,
                                      limb *resultModOdd,
                                      limb *resultModPower2,
                                      int shRight) {

  printf("CRT shright: %d mod:\n", shRight);
  for (int i = 0; i < 6; i++)
    printf("  %08x %c\n", params.modulus[i].x,
           i < params.modulus_length ? '*' : ' ');

  CHECK(modulus_length == params.modulus_length);

  BigInteger oddValue;
  BigIntToBigInteger(OddMod, &oddValue);

  BigInteger result;
  if (shRight == 0) {
    UncompressLimbsBigInteger(oddValue.nbrLimbs, resultModOdd, &result);
    return BigIntegerToBigInt(&result);
  }

  if (modulus_length > oddValue.nbrLimbs) {
    int lenBytes = (modulus_length - oddValue.nbrLimbs) * (int)sizeof(limb);
    (void)memset(&oddValue.Limbs[oddValue.nbrLimbs], 0, lenBytes);
  }

  limb tmp3[MAX_LEN];
  SubtractBigNbr(resultModPower2, resultModOdd, tmp3, modulus_length);
  limb tmp4[MAX_LEN];
  ComputeInversePower2(oddValue.Limbs.data(), tmp4, modulus_length);

  printf("CRT tmp4:\n");
  for (int i = 0; i < 6; i++)
    printf("  %08x %c\n", tmp4[i].x,
           i < modulus_length ? '*' : ' ');

  limb tmp5[MAX_LEN];
  ModMult(params, tmp4, tmp3, tmp5);

  (tmp5 + (shRight / BITS_PER_GROUP))->x &=
    (1 << (shRight % BITS_PER_GROUP)) - 1;

  printf("CRT tmp5:\n");
  for (int i = 0; i < 6; i++)
    printf("  %08x %c\n", tmp5[i].x,
           i < modulus_length ? '*' : ' ');

  if (modulus_length < oddValue.nbrLimbs) {
    printf("CRT pad0\n");
    int lenBytes = (oddValue.nbrLimbs - modulus_length) * (int)sizeof(limb);
    (void)memset(&tmp5[modulus_length], 0, lenBytes);

    printf("CRT tmp5 padded:\n");
    for (int i = 0; i < 6; i++)
      printf("  %08x %c\n", tmp5[i].x,
             i < modulus_length ? '*' : ' ');
  }

  printf("CRT oddValue:\n");
  for (int i = 0; i < 6; i++)
    printf("  %08x %c\n", oddValue.Limbs[i].x,
           i < modulus_length ? '*' : ' ');

  printf("CRT resultModOdd:\n");
  for (int i = 0; i < 6; i++)
    printf("  %08x %c\n", resultModOdd[i].x,
           i < modulus_length ? '*' : ' ');

  printf("modulus_length %d. oddValue.nbrLimbs %d\n",
         modulus_length, oddValue.nbrLimbs);

  const int odd_length = oddValue.nbrLimbs;
  BigInt Result = LimbsToBigInt(tmp5, modulus_length);
  Result *= BigIntegerToBigInt(&oddValue);
  Result += LimbsToBigInt(resultModOdd, odd_length);
  return Result;
}

// Compute modular division. ModInvBigNbr does not support even moduli,
// so the division is done separately by calculating the division modulo
// n/2^k (n odd) and 2^k and then merge the results using Chinese Remainder
// Theorem.
BigInt GeneralModularDivision(
    const BigInt &Num, const BigInt &Den, const BigInt &Mod) {

  printf("--------\n");
  printf("GMD:\n"
         "  num: %s\n"
         "  den: %s\n"
         "  mod: %s\n",
         Num.ToString().c_str(),
         Den.ToString().c_str(),
         Mod.ToString().c_str());

  const int shRight = BigInt::BitwiseCtz(Mod);
  const BigInt OddMod = Mod >> shRight;

  // Reduce Num modulo oddValue.
  BigInt TmpNum = Num % OddMod;
  if (TmpNum < 0) TmpNum += OddMod;

  // Reduce Den modulo oddValue.
  BigInt TmpDen = Den % OddMod;
  if (TmpDen < 0) TmpDen += OddMod;

  const std::unique_ptr<MontgomeryParams> params =
    GetMontgomeryParams(OddMod);
  // This is the modulus length for the right-shifted value.
  const int modulus_length = params->modulus_length;

  // XXX
  // limb tmp3[modulus_length + 1];
  limb tmp3[100] = {};
  BigIntToFixedLimbs(TmpDen, modulus_length, tmp3);
  // tmp3 <- Den in Montgomery notation
  ModMult(*params, tmp3, params->MontgomeryMultR2, tmp3);

  printf("[tmp3 <- Den] tmp3:\n");
  for (int i = 0; i < 6; i++)
    printf("  %08x %c\n", tmp3[i].x, i < modulus_length ? '*' : ' ');

  tmp3[modulus_length].x = 0;
  // tmp3 <- 1 / Den in Montg notation.
  (void)ModInvBigNbr(*params, tmp3, tmp3);

  printf("[tmp3 <- 1 / Den] tmp3:\n");
  for (int i = 0; i < 6; i++)
    printf("  %08x %c\n", tmp3[i].x, i < modulus_length ? '*' : ' ');

  limb tmp4[modulus_length];
  BigIntToFixedLimbs(TmpNum, modulus_length, tmp4);

  printf("[tmp4 <- num] tmp4:\n");
  for (int i = 0; i < 6; i++)
    printf("  %08x %c\n", tmp4[i].x, i < modulus_length ? '*' : ' ');

  // resultModOdd <- Num / Den in standard notation.
  limb resultModOdd[modulus_length];
  ModMult(*params, tmp3, tmp4, resultModOdd);

  printf("[modmult] resultModOdd:\n");
  for (int i = 0; i < 6; i++)
    printf("  %08x %c\n", resultModOdd[i].x,
           i < modulus_length ? '*' : ' ');

  // Compute inverse mod power of 2.
  if (shRight == 0) {
    // Original modulus is odd. Quotient already computed.
    printf("return modulus odd. numberlength: %d\n", modulus_length);
    return LimbsToBigInt(resultModOdd, modulus_length);
    // UncompressLimbsBigInteger(modulus_length, resultModOdd, quotient);
    // return;
  }

  // BigInteger den;
  // BigIntToBigInteger(Den, &den);

  const int new_modulus_length =
    (shRight + BITS_PER_GROUP_MINUS_1) / BITS_PER_GROUP;
  BigIntToFixedLimbs(Den, new_modulus_length, tmp3);
  // CompressLimbsBigInteger(new_modulus_length, tmp3, &den);

  printf("[Compute Inverse Power 2] tmp3:\n");
  for (int i = 0; i < 6; i++)
    printf("  %08x %c\n", tmp3[i].x,
           i < new_modulus_length ? '*' : ' ');

  ComputeInversePower2(tmp3, tmp4, new_modulus_length);

  // Port note: Original code just set powerOf2Exponent and number length
  // here, I think relying on the fact that modmult doesn't use the modulus
  // (now stale) in the case of a power of 2.
  std::unique_ptr<MontgomeryParams> crt_params =
    GetMontgomeryParamsPowerOf2(shRight);

  limb num[crt_params->modulus_length];
  BigIntToFixedLimbs(Num, crt_params->modulus_length, num);

  printf("[Get resultModPower2] num:\n");
  for (int i = 0; i < 6; i++)
    printf("  %08x %c\n", num[i].x,
           i < crt_params->modulus_length ? '*' : ' ');
  printf("[Get resultModPower2] tmp4:\n");
  for (int i = 0; i < 6; i++)
    printf("  %08x %c\n", tmp4[i].x,
           i < crt_params->modulus_length ? '*' : ' ');

  // resultModPower2 <- Num / Dev mod 2^k.
  // XXX
  limb resultModPower2[100] = {};
  // limb resultModPower2[crt_params->modulus_length];
  ModMult(*crt_params, num, tmp4, resultModPower2);

  printf("[before CRT] resultModOdd:\n");
  for (int i = 0; i < 6; i++)
    printf("  %08x %c\n", resultModOdd[i].x,
           i < new_modulus_length ? '*' : ' ');

  printf("[before CRT] resultModPower2:\n");
  for (int i = 0; i < 6; i++)
    printf("  %08x %c\n", resultModPower2[i].x,
           i < new_modulus_length ? '*' : ' ');

  BigInt ret = ChineseRemainderTheorem(*crt_params,
                                       OddMod,
                                       new_modulus_length,
                                       resultModOdd, resultModPower2,
                                       shRight);

  printf("CRT ret: %s\n", ret.ToString().c_str());
  return ret;
}

// Find the inverse of value mod 2^(number_length*BITS_PER_GROUP)
void ComputeInversePower2(const limb *value, limb *result, int number_length) {
  limb tmp[number_length * 2];
  limb tmp2[number_length * 2];
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
    MultiplyLimbs(value, tmp2, tmp, currLen);    // tmp <- N * x
    Cy = 2U - (unsigned int)tmp[0].x;
    tmp[0].x = UintToInt(Cy & MAX_VALUE_LIMB);

    // tmp <- 2 - N * x
    for (int j = 1; j < currLen; j++) {
      Cy = (unsigned int)(-tmp[j].x) - (Cy >> BITS_PER_GROUP);
      tmp[j].x = UintToInt(Cy & MAX_VALUE_LIMB);
    }
    // tmp <- x * (2 - N * x)
    MultiplyLimbs(tmp2, tmp, tmp2, currLen);
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

  printf("[CIP2 final multiply] tmp2:\n");
  for (int i = 0; i < 6; i++)
    printf("  %08x %c\n", tmp2[i].x,
           i < number_length ? '*' : ' ');
  printf("[CIP2 final multiply] tmp:\n");
  for (int i = 0; i < 6; i++)
    printf("  %08x %c\n", tmp[i].x,
           i < number_length ? '*' : ' ');

  // tmp <- x * (2 - N * x)
  MultiplyLimbs(tmp2, tmp, tmp2, number_length);
  memcpy(result, tmp2, number_length * sizeof(limb));
}

// PERF can avoid some copying if we repeat guts of above
// (or factor into an internal version that takes a buffer of the
// appropriate 2x length)
BigInt GetInversePower2(const BigInt &Value, int number_length) {
  const int value_length = BigIntNumLimbs(Value);
  // PERF might not need to convert the entire value? Below we
  // truncate to number_length. Or this may have a secret precondition
  // that value_length <= number_length.
  const int storage_length = std::max(value_length, number_length);
  if (VERBOSE) {
    printf("Value %s. number_len %d. value_len %d\n",
           Value.ToString().c_str(), number_length, value_length);
  }
  limb value[storage_length];
  BigIntToFixedLimbs(Value, storage_length, value);

  limb result[number_length];
  ComputeInversePower2(value, result, number_length);
  return LimbsToBigInt(result, number_length);
}

// No coverage :(
std::unique_ptr<MontgomeryParams>
GetMontgomeryParamsPowerOf2(int powerOf2) {
  std::unique_ptr<MontgomeryParams> params =
    std::make_unique<MontgomeryParams>();

  // Would be better to just generate this directly into
  // params->modulus...
  BigInt Pow2 = BigInt(1) << powerOf2;

  const int modulus_length = BigIntNumLimbs(Pow2);
  params->modulus_length = modulus_length;
  params->modulus.resize(modulus_length + 1);
  BigIntToFixedLimbs(Pow2, modulus_length, params->modulus.data());
  params->modulus[modulus_length].x = 0;
  params->powerOf2Exponent = powerOf2;

  // XXX check what size we guarantee for R1, R2. We just need to store
  // a padded "1" here for both.
  (void)memset(params->MontgomeryMultR1, 0,
               (modulus_length + 1) * sizeof (limb));
  (void)memset(params->MontgomeryMultR2, 0,
               (modulus_length + 1) * sizeof (limb));
  params->MontgomeryMultR1[0].x = 1;
  params->MontgomeryMultR2[0].x = 1;
  return params;
}

// Compute Nbr <- Nbr mod Modulus.
// Modulus has NumberLength limbs.
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

  (Nbr + i)->x = carry & MAX_INT_NBR;
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

  // We don't bother with montgomery form for single-word numbers.
  if (params->modulus_length == 1 && (params->modulus[0].x & 1) != 0) {
    params->MontgomeryMultR1[0].x = 1;
    params->MontgomeryMultR2[0].x = 1;
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
          params->modulus_length * (int)sizeof(limb);
        (void)memset(params->MontgomeryMultR1, 0, NumberLengthBytes);
        (void)memset(params->MontgomeryMultR2, 0, NumberLengthBytes);
        params->MontgomeryMultR1[0].x = 1;
        params->MontgomeryMultR2[0].x = 1;

        return;
      }
    }
  }

  // Compute MontgomeryMultN as 1/modulus (mod 2^k) using Newton method,
  // which doubles the precision for each iteration.
  // In the formula above: k = BITS_PER_GROUP * modulus_length.
  ComputeInversePower2(params->modulus.data(),
                       params->MontgomeryMultN,
                       params->modulus_length);

  params->MontgomeryMultN[params->modulus_length].x = 0;

  // Compute MontgomeryMultR1 as 1 in Montgomery notation,
  // this is 2^(modulus_length*BITS_PER_GROUP) % modulus.
  int j = params->modulus_length;
  params->MontgomeryMultR1[j].x = 1;
  do {
    j--;
    params->MontgomeryMultR1[j].x = 0;
  } while (j > 0);
  AdjustModN(params->MontgomeryMultR1,
             params->modulus.data(),
             params->modulus_length);

  params->MontgomeryMultR1[params->modulus_length].x = 0;

  const int PaddedNumberLengthBytes =
    (params->modulus_length + 1) * (int)sizeof(limb);
  (void)memcpy(params->MontgomeryMultR2, params->MontgomeryMultR1,
               PaddedNumberLengthBytes);

  // Port note: NumberLengthR1 used to be a member of the struct (I
  // guess before that a global variable), but it's only used during
  // initialization. Other code assumes R1 is modulus_length size,
  // which seems right.
  for (int NumberLengthR1 = params->modulus_length;
       NumberLengthR1 > 0;
       NumberLengthR1--) {
    if (params->MontgomeryMultR1[NumberLengthR1 - 1].x != 0) {
      break;
    }
  }

  // Compute MontgomeryMultR2 as 2^(2*modulus_length*BITS_PER_GROUP) % modulus.
  for (int i = params->modulus_length; i > 0; i--) {
    const int NumberLengthBytes =
      params->modulus_length * (int)sizeof(limb);
    (void)memmove(&params->MontgomeryMultR2[1],
                  &params->MontgomeryMultR2[0],
                  NumberLengthBytes);
    params->MontgomeryMultR2[0].x = 0;
    AdjustModN(params->MontgomeryMultR2,
               params->modulus.data(),
               params->modulus_length);
  }
}

// Let R be a power of 2 of at least len limbs.
// Compute R1 = MontgomeryR1 and N = MontgomeryN using the formulas:
// R1 = R mod M
// N = M^(-1) mod R
// This routine is only valid for odd or power of 2 moduli.
std::unique_ptr<MontgomeryParams>
GetMontgomeryParams(int modulus_length, const limb *modulus) {
  std::unique_ptr<MontgomeryParams> params =
    std::make_unique<MontgomeryParams>();
  // BigInt m = LimbsToBigInt(modulus, modulus_length);

  CHECK(modulus[modulus_length].x == 0);

  params->modulus_length = modulus_length;
  params->modulus.resize(modulus_length + 1);
  memcpy(params->modulus.data(), modulus,
         (modulus_length + 1) * sizeof (limb));

  InitMontgomeryParams(params.get());
  return params;
}

std::unique_ptr<MontgomeryParams>
GetMontgomeryParams(const BigInt &Modulus) {
  std::unique_ptr<MontgomeryParams> params =
    std::make_unique<MontgomeryParams>();

  params->modulus_length = BigIntNumLimbs(Modulus);
  // With space for required padding.
  params->modulus.resize(params->modulus_length + 1);
  BigIntToFixedLimbs(Modulus, params->modulus_length, params->modulus.data());
  params->modulus[params->modulus_length].x = 0;

  InitMontgomeryParams(params.get());
  return params;
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

  printf("ModMult params modulus_length: %d, pow2: %d\n",
         params.modulus_length, params.powerOf2Exponent);

  if (VERBOSE) {
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
    printf("pow2 exponent = %d\n", params.powerOf2Exponent);

    // PERF: We could store the mask instead of the power?
    const BigInt Mask = (BigInt(1) << params.powerOf2Exponent) - 1;

    const BigInt f1 = LimbsToBigInt(factor1, params.modulus_length);
    const BigInt f2 = LimbsToBigInt(factor2, params.modulus_length);

    printf("pow2 factor1 %s\n", f1.ToString().c_str());
    printf("pow2 factor2 %s\n", f2.ToString().c_str());

    BigInt Product = (f1 * f2);

    printf("pow2 product %s\n", Product.ToString().c_str());

    Product &= Mask;
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

#if 0
    // PERF hot path. modulus_length of 0 should be impossible, so we
    // could just be reading the ints and doing this native.
    BigInt f1 = LimbsToBigInt(factor1, params.modulus_length);
    BigInt f2 = LimbsToBigInt(factor2, params.modulus_length);

    BigInt modulus = LimbsToBigInt(params.modulus.data(),
                                   params.modulus_length);
    // Hmm, no BigInt modular multiplication :/
    BigInt r = BigInt::Mod(BigInt::Times(f1, f2), modulus);
    BigIntToFixedLimbs(r, params.modulus_length, product);
#endif

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
    MultiplyLimbs(aux3, params.MontgomeryMultN, aux, params.modulus_length);
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
