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

#include <memory>

#include "bignbr.h"
#include "bignum/big.h"
#include "bignum/big-overloads.h"
#include "base/logging.h"
#include "bigconv.h"
#include "base/stringprintf.h"

static constexpr bool SELF_CHECK = true;

static std::string LimbString(limb *limbs, size_t num) {
  string out;
  for (size_t i = 0; i < num; i++) {
    if (i != 0) out.push_back(":");
    StringAppendF(&out, "%04x", limbs[i].x);
  }
  return out;
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
                          int modulus_length, const limb *modulus,
                          const BigInt &base_, const BigInt &exponent_) {

  {
    // BigInt Base = BigIntegerToBigInt(base);
    // BigInt Exp = BigIntegerToBigInt(exponent);
    BigInt Modulus = LimbsToBigInt(modulus, modulus_length);
    printf("[%d] BIMP %s^%s mod %s\n",
           modulus_length,
           base_.ToString().c_str(),
           exponent_.ToString().c_str(),
           Modulus.ToString().c_str());
  }

  // XXX PERF no, directly convert to limbs
  BigInteger base, exponent;
  BigIntToBigInteger(base_, &base);
  BigIntToBigInteger(exponent_, &exponent);

  // Note we don't have any coverage with modulus_length > 3.
  // if (modulus_length > 3)
  //   fprintf(stderr, "BIMP coverage %d\n", modulus_length);
  limb tmp5[modulus_length];
  CompressLimbsBigInteger(modulus_length, tmp5, &base);
  limb tmp6[modulus_length];
  // Convert base to Montgomery notation.
  ModMult(params,
          tmp5, params.MontgomeryMultR2,
          modulus_length, modulus,
          tmp6);
  ModPow(params, modulus_length, modulus,
         tmp6, exponent.limbs, exponent.nbrLimbs, tmp5);

  const int lenBytes = modulus_length * (int)sizeof(limb);
  limb tmp4[modulus_length];
  // Convert power to standard notation.
  // (This appears to compute tmp6 <- 1 * tmp5 % modulus ?
  // I guess 1 is not literally the identity because we do
  // the multiplication in montgomery form. -tom7)
  printf("memset %d bytes (modulus_length = %d)\n", lenBytes, modulus_length);
  fflush(stdout);
  (void)memset(tmp4, 0, lenBytes);
  tmp4[0].x = 1;
  ModMult(params,
          tmp4, tmp5,
          modulus_length, modulus,
          tmp6);

  // XXX PERF no, convert directly
  BigInteger power;
  UncompressLimbsBigInteger(modulus_length, tmp6, &power);
  return BigIntegerToBigInt(&power);
}

// Input: base = base in Montgomery notation.
//        exp  = exponent.
//        nbrGroupsExp = number of limbs of exponent.
// Output: power = power in Montgomery notation.
void ModPow(const MontgomeryParams &params,
            int modulus_length, const limb *modulus,
            const limb* base, const limb* exp, int nbrGroupsExp, limb* power) {
  // Why plus 1??
  int lenBytes = (modulus_length + 1) * (int)sizeof(limb);
  (void)memcpy(power, params.MontgomeryMultR1, lenBytes);  // power <- 1
  for (int index = nbrGroupsExp - 1; index >= 0; index--) {
    int groupExp = (exp + index)->x;
    for (unsigned int mask = HALF_INT_RANGE_U; mask > 0U; mask >>= 1) {
      ModMult(params,
              power, power,
              modulus_length, modulus,
              power);
      if (((unsigned int)groupExp & mask) != 0U) {
        ModMult(params,
                power, base,
                modulus_length, modulus,
                power);
      }
    }
  }
}

void ModPowBaseInt(const MontgomeryParams &params,
                   int modulus_length, const limb *modulus,
                   int base, const BigInt &Exp,
                   limb* power) {

  BigInteger exp;
  BigIntToBigInteger(Exp, &exp);

  int NumberLengthBytes = (modulus_length + 1) * (int)sizeof(limb);
  // power <- 1
  (void)memcpy(power, params.MontgomeryMultR1, NumberLengthBytes);
  for (int index = exp.nbrLimbs - 1; index >= 0; index--) {
    int groupExp = exp.limbs[index].x;
    for (unsigned int mask = HALF_INT_RANGE_U; mask > 0U; mask >>= 1) {
      ModMult(params,
              power, power,
              modulus_length, modulus,
              power);
      if (((unsigned int)groupExp & mask) != 0U) {
        ModMultInt(power, base, power, modulus, modulus_length);
      }
    }
  }
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
  int QQ;
  int T1;
  int T3;
  int V1 = 1;
  int V3 = NbrMod;
  int U1 = 0;
  int U3 = currentPrime;
  while (V3 != 0) {
    if (U3 < (V3 + V3)) {
      // QQ = 1
      T1 = U1 - V1;
      T3 = U3 - V3;
    } else {
      QQ = U3 / V3;
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
static bool ModInvBigNbr(const MontgomeryParams &params,
                         int modulus_length, const limb* modulus,
                         limb* num, limb* inv) {
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
  int lowU;
  int lowV;
  unsigned int borrow;
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

  (num + modulus_length)->x = 0;
  // PERF can just be length size, probably?
  limb U[MAX_LEN];
  limb V[MAX_LEN];
  (void)memcpy(U, modulus, size);
  (void)memcpy(V, num, size);
  // Maximum value of R and S can be up to 2*M, so one more limb is needed.
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

  lowU = U[0].x;
  lowV = V[0].x;

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
        limb Ubak[MAX_LEN];
        limb Vbak[MAX_LEN];
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
  ModMult(params,
          R, params.MontgomeryMultR2,
          modulus_length, modulus,
          R);
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
    ModMult(params,
            R, S,
            modulus_length, modulus,
            inv);
  } else {
    unsigned int shLeft;
    shLeft = (unsigned int)bitCount % (unsigned int)BITS_PER_GROUP;
    S[bitCount / BITS_PER_GROUP].x = UintToInt(1U << shLeft);
    ModMult(params,
            R, S,
            modulus_length, modulus,
            inv);
    ModMult(params,
            inv, params.MontgomeryMultR2,
            modulus_length, modulus,
            inv);
  }

  return true;  // Inverse computed.
}

// PERF: This function is slow and used in inner loops.
// Compute modular division for odd moduli.
// params and modulus should match.
BigInt BigIntModularDivision(const MontgomeryParams &params,
                             BigInt Num, BigInt Den,
                             const BigInt &Mod) {

  const bool verbose = false && (Num > Mod || Den > Mod);

  if (verbose)
  printf("With (params), %s / %s mod %s",
         Num.ToString().c_str(),
         Den.ToString().c_str(),
         Mod.ToString().c_str());

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

  // PERF can convert to limbs directly
  BigInteger tmpNum, tmpDen;
  BigIntToBigInteger(Num, &tmpNum);
  BigIntToBigInteger(Den, &tmpDen);

  limb tmp3[MAX_LEN];
  CompressLimbsBigInteger(params.modulus_length, tmp3, &tmpDen);
  // tmp3 <- Den in Montgomery notation
  // tmpDen.limbs <- 1 / Den in Montg notation.
  ModMult(params,
          tmp3, params.MontgomeryMultR2,
          params.modulus_length, params.modulus.data(),
          tmp3);
  (void)ModInvBigNbr(params,
                     params.modulus_length,
                     params.modulus.data(), tmp3, tmpDen.limbs);
  limb tmp4[MAX_LEN];
  CompressLimbsBigInteger(params.modulus_length, tmp4, &tmpNum);
  // tmp3 <- Num / Den in standard notation.
  ModMult(params,
          tmpDen.limbs, tmp4,
          params.modulus_length, params.modulus.data(),
          tmp3);

  // Get Num/Den
  // PERF can just convert to BigInt
  BigInteger z;
  UncompressLimbsBigInteger(params.modulus_length, tmp3, &z);

  BigInt ret = BigIntegerToBigInt(&z);

  if (verbose)
    printf("  = %s\n",
           ret.ToString().c_str());

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
static void ChineseRemainderTheorem(const MontgomeryParams &params,
                                    BigInteger *oddValue,
                                    int modulus_length, const limb *modulus,
                                    limb *resultModOdd,
                                    limb *resultModPower2,
                                    int shRight, BigInteger* result) {
  if (shRight == 0) {
    UncompressLimbsBigInteger(oddValue->nbrLimbs, resultModOdd, result);
    return;
  }

  if (modulus_length > oddValue->nbrLimbs) {
    int lenBytes = (modulus_length - oddValue->nbrLimbs) * (int)sizeof(limb);
    (void)memset(&oddValue->limbs[oddValue->nbrLimbs], 0, lenBytes);
  }

  limb tmp3[MAX_LEN];
  SubtractBigNbr(resultModPower2, resultModOdd, tmp3, modulus_length);
  limb tmp4[MAX_LEN];
  ComputeInversePower2(oddValue->limbs, tmp4, modulus_length);
  limb tmp5[MAX_LEN];
  ModMult(params,
          tmp4, tmp3,
          modulus_length, modulus,
          tmp5);
  (tmp5 + (shRight / BITS_PER_GROUP))->x &=
    (1 << (shRight % BITS_PER_GROUP)) - 1;

  if (modulus_length < oddValue->nbrLimbs) {
    int lenBytes = (oddValue->nbrLimbs - modulus_length) * (int)sizeof(limb);
    (void)memset(&tmp5[modulus_length], 0, lenBytes);
  }
  UncompressLimbsBigInteger(modulus_length, tmp5, result);
  (void)BigIntMultiply(result, oddValue, result);
  // NumberLength = oddValue->nbrLimbs;

  BigInteger tmp;
  UncompressLimbsBigInteger(modulus_length, resultModOdd, &tmp);
  BigIntAdd(result, &tmp, result);
}

// Compute modular division. ModInvBigNbr does not support even moduli,
// so the division is done separately by calculating the division modulo
// n/2^k (n odd) and 2^k and then merge the results using Chinese Remainder
// Theorem.
static void BigIntegerGeneralModularDivision(
    const BigInt &Num, const BigInt &Den,
    const BigInt &Mod, BigInteger* quotient) {

  int shRight = BigInt::BitwiseCtz(Mod);
  BigInt OddMod = Mod >> shRight;
  // BigInteger oddValue;
  // CopyBigInt(&oddValue, mod);
  // DivideBigNbrByMaxPowerOf2(&shRight, oddValue.limbs, &oddValue.nbrLimbs);

  // Reduce Num modulo oddValue.
  BigInt TmpNum = Num % OddMod;
  if (TmpNum < 0) TmpNum += OddMod;
  // BigInteger tmpNum;
  // (void)BigIntRemainder(Num, &oddValue, &tmpNum);
  // if (tmpNum.sign == SIGN_NEGATIVE) {
  // BigIntAdd(&tmpNum, &oddValue, &tmpNum);
  // }

  // Reduce Den modulo oddValue.
  BigInt TmpDen = Den % OddMod;
  if (TmpDen < 0) TmpDen += OddMod;
  // BigInteger tmpDen;
  // (void)BigIntRemainder(Den, &oddValue, &tmpDen);
  // if (tmpDen.sign == SIGN_NEGATIVE) {
  // BigIntAdd(&tmpDen, &oddValue, &tmpDen);
  // }

  // XXX convert directly to fixed limbs
  BigInteger oddValue;
  BigIntToBigInteger(OddMod, &oddValue);

  BigInteger tmpNum, tmpDen;
  BigIntToBigInteger(TmpNum, &tmpNum);
  BigIntToBigInteger(TmpDen, &tmpDen);

  int modulus_length = oddValue.nbrLimbs;
  int NumberLengthBytes = modulus_length * (int)sizeof(limb);
  limb modulus[MAX_LEN];
  (void)memcpy(modulus, oddValue.limbs, NumberLengthBytes);
  modulus[modulus_length].x = 0;
  const std::unique_ptr<MontgomeryParams> params =
    GetMontgomeryParams(modulus_length, modulus);
  limb tmp3[MAX_LEN];
  CompressLimbsBigInteger(modulus_length, tmp3, &tmpDen);
  // tmp3 <- Den in Montgomery notation
  ModMult(*params,
          tmp3, params->MontgomeryMultR2,
          modulus_length, modulus,
          tmp3);
  // tmp3 <- 1 / Den in Montg notation.
  (void)ModInvBigNbr(*params, modulus_length, modulus, tmp3, tmp3);
  limb tmp4[MAX_LEN];
  CompressLimbsBigInteger(modulus_length, tmp4, &tmpNum);
  // resultModOdd <- Num / Den in standard notation.
  // PERF: can be smaller than MAX_LEN.
  limb resultModOdd[MAX_LEN];
  ModMult(*params,
          tmp3, tmp4,
          modulus_length, modulus,
          resultModOdd);

  // Compute inverse mod power of 2.
  if (shRight == 0) {
    // Modulus is odd. Quotient already computed.
    const int number_length = oddValue.nbrLimbs;
    UncompressLimbsBigInteger(number_length, resultModOdd, quotient);
    return;
  }

  BigInteger den;
  BigIntToBigInteger(Den, &den);

  modulus_length = (shRight + BITS_PER_GROUP_MINUS_1) / BITS_PER_GROUP;
  CompressLimbsBigInteger(modulus_length, tmp3, &den);
  ComputeInversePower2(tmp3, tmp4, modulus_length);

  // Port note: This used to set powerOf2Exponent = shRight and then
  // clear to zero at the end, but those are dead now that it's part
  // of MontgomeryParams.

  BigInteger num;
  BigIntToBigInteger(Num, &num);

  // resultModPower2 <- Num / Dev modulus 2^k.
  // PERF: here too
  limb resultModPower2[MAX_LEN];
  ModMult(*params,
          num.limbs, tmp4,
          modulus_length, modulus,
          resultModPower2);
  ChineseRemainderTheorem(*params,
                          &oddValue,
                          modulus_length, modulus,
                          resultModOdd, resultModPower2,
                          shRight, quotient);
}


BigInt GeneralModularDivision(const BigInt &num, const BigInt &den,
                              const BigInt &modulus) {
  BigInteger result;
  BigIntegerGeneralModularDivision(num, den, modulus, &result);
  CHECK(result.nbrLimbs > 0);
  return BigIntegerToBigInt(&result);
}

// Find the inverse of value mod 2^(number_length*BITS_PER_GROUP)
void ComputeInversePower2(const limb *value, limb *result, int number_length) {
  limb tmp[MAX_LEN];
  unsigned int Cy;
  int N = value->x;            // 2 least significant bits of inverse correct.
  int x = N;
  x = x * (2 - (N * x));       // 4 least significant bits of inverse correct.
  x = x * (2 - (N * x));       // 8 least significant bits of inverse correct.
  x = x * (2 - (N * x));       // 16 least significant bits of inverse correct.
  x = x * (2 - (N * x));       // 32 least significant bits of inverse correct.
  result->x = UintToInt((unsigned int)x & MAX_VALUE_LIMB);

  for (int currLen = 2; currLen < number_length; currLen <<= 1) {
    MultiplyLimbs(value, result, tmp, currLen);    // tmp <- N * x
    Cy = 2U - (unsigned int)tmp[0].x;
    tmp[0].x = UintToInt(Cy & MAX_VALUE_LIMB);

    // tmp <- 2 - N * x
    for (int j = 1; j < currLen; j++) {
      Cy = (unsigned int)(-tmp[j].x) - (Cy >> BITS_PER_GROUP);
      tmp[j].x = UintToInt(Cy & MAX_VALUE_LIMB);
    }
    // tmp <- x * (2 - N * x)
    MultiplyLimbs(result, tmp, result, currLen);
  }

  // Perform last approximation to inverse.
  MultiplyLimbs(value, result, tmp, number_length);    // tmp <- N * x
  Cy = 2U - (unsigned int)tmp[0].x;
  tmp[0].x = UintToInt(Cy & MAX_VALUE_LIMB);
  // tmp <- 2 - N * x
  for (int j = 1; j < number_length; j++) {
    Cy = (unsigned int)(-tmp[j].x) - (Cy >> BITS_PER_GROUP);
    tmp[j].x = UintToInt(Cy & MAX_VALUE_LIMB);
  }
  // tmp <- x * (2 - N * x)
  MultiplyLimbs(result, tmp, result, number_length);
}

std::unique_ptr<MontgomeryParams> GetMontgomeryParamsPowerOf2(
    int powerOf2,
    int *modulus_length) {
  std::unique_ptr<MontgomeryParams> params =
    std::make_unique<MontgomeryParams>();

  *modulus_length =
    (powerOf2 + BITS_PER_GROUP - 1) / BITS_PER_GROUP;
  int NumberLengthBytes = *modulus_length * (int)sizeof(limb);

  // Would be better to just generate this directly into
  // params->modulus...
  BigInteger pow2;
  BigIntPowerOf2(&pow2, powerOf2);

  params->modulus_length = *modulus_length;
  params->modulus.resize(*modulus_length + 1);
  CHECK(pow2.nbrLimbs == *modulus_length);
  memcpy(params->modulus.data(), pow2.limbs,
         *modulus_length * sizeof (limb));
  params->modulus[*modulus_length].x = 0;

  params->powerOf2Exponent = powerOf2;
  (void)memset(params->MontgomeryMultR1, 0, NumberLengthBytes);
  (void)memset(params->MontgomeryMultR2, 0, NumberLengthBytes);
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

// Let R be a power of 2 of at least len limbs.
// Compute R1 = MontgomeryR1 and N = MontgomeryN using the formulas:
// R1 = R mod M
// N = M^(-1) mod R
// This routine is only valid for odd or power of 2 moduli.

std::unique_ptr<MontgomeryParams>
GetMontgomeryParams(int modulus_length, const limb *modulus) {
  std::unique_ptr<MontgomeryParams> params =
    std::make_unique<MontgomeryParams>();
  int j;
  CHECK(modulus[modulus_length].x == 0);
  params->powerOf2Exponent = 0;    // Indicate not power of 2 in advance.
  params->NumberLengthR1 = 1;

  params->modulus_length = modulus_length;
  params->modulus.resize(modulus_length + 1);
  memcpy(params->modulus.data(), modulus,
         (modulus_length + 1) * sizeof (limb));

  if (modulus_length == 1 && (modulus[0].x & 1) != 0) {
    params->MontgomeryMultR1[0].x = 1;
    params->MontgomeryMultR2[0].x = 1;
    return params;
  }

  // Check whether modulus is a power of 2.
  for (j = 0; j < modulus_length - 1; j++) {
    if (modulus[j].x != 0) {
      break;
    }
  }

  if (j == modulus_length - 1) {
    // modulus is a power of 2.
    int value = modulus[modulus_length - 1].x;
    for (j = 0; j < BITS_PER_GROUP; j++) {
      if (value == 1) {
        const int NumberLengthBytes = modulus_length * (int)sizeof(limb);
        params->powerOf2Exponent = ((modulus_length - 1) * BITS_PER_GROUP) + j;
        (void)memset(params->MontgomeryMultR1, 0, NumberLengthBytes);
        (void)memset(params->MontgomeryMultR2, 0, NumberLengthBytes);
        params->MontgomeryMultR1[0].x = 1;
        params->MontgomeryMultR2[0].x = 1;
        return params;
      }
      value >>= 1;
    }
  }

  // Compute MontgomeryMultN as 1/modulus (mod 2^k) using Newton method,
  // which doubles the precision for each iteration.
  // In the formula above: k = BITS_PER_GROUP * modulus_length.
  ComputeInversePower2(modulus, params->MontgomeryMultN, modulus_length);
  params->MontgomeryMultN[modulus_length].x = 0;
  // Compute MontgomeryMultR1 as 1 in Montgomery notation,
  // this is 2^(modulus_length*BITS_PER_GROUP) % modulus.
  j = modulus_length;
  params->MontgomeryMultR1[j].x = 1;
  do {
    j--;
    params->MontgomeryMultR1[j].x = 0;
  } while (j > 0);
  AdjustModN(params->MontgomeryMultR1, modulus, modulus_length);
  params->MontgomeryMultR1[modulus_length].x = 0;
  int NumberLengthBytes = (modulus_length + 1) * (int)sizeof(limb);
  (void)memcpy(params->MontgomeryMultR2, params->MontgomeryMultR1,
               NumberLengthBytes);
  for (params->NumberLengthR1 = modulus_length;
       params->NumberLengthR1 > 0;
       params->NumberLengthR1--) {
    if (params->MontgomeryMultR1[params->NumberLengthR1 - 1].x != 0) {
      break;
    }
  }

  // Compute MontgomeryMultR2 as 2^(2*modulus_length*BITS_PER_GROUP) % modulus.
  for (int i = modulus_length; i > 0; i--) {
    const int NumberLengthBytes = modulus_length * (int)sizeof(limb);
    (void)memmove(&params->MontgomeryMultR2[1],
                  &params->MontgomeryMultR2[0],
                  NumberLengthBytes);
    params->MontgomeryMultR2[0].x = 0;
    AdjustModN(params->MontgomeryMultR2, modulus, modulus_length);
  }

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

#if 0
static void endBigModmult(const limb *prodNotAdjusted, int number_length,
                          const limb *modulus_array, limb *product) {
  unsigned int cy = 0;
  // Compute hi(T) - hi(mN)
  // Where hi(number) is the high half of number.
  int index = number_length;
  for (int count = 0; count < number_length; count++) {
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
    for (int count = 0; count < number_length; count++) {
      cy = (unsigned int)(product + count)->x +
        (unsigned int)modulus_array[count].x +
        (cy >> BITS_PER_GROUP);
      (product + count)->x = UintToInt(cy & MAX_VALUE_LIMB);
    }
  }
}



  if (number_length <= 1) {
    BigInt f1 = LimbsToBigInt(factor1, number_length);
    BigInt f2 = LimbsToBigInt(factor2, number_length);

    BigInt modulus = LimbsToBigInt(modulus_array, number_length);
    // Hmm, no BigInt modular multiplication :/
    BigInt r = BigInt::Mod(BigInt::Times(f1, f2), modulus);
    BigIntToFixedLimbs(r, number_length, product);

  } else {
    // Note that we have no coverage for number_length > 3!
    // if (number_length > 3)
    //   fprintf(stderr, "montgomery coverage %d\n", number_length);

    // In Montgomery form.
    // All MultiplyLimbs does is multiply using BigInt, so we can
    // probably just do the first several steps without converting.
    limb aux[number_length * 2], aux2[number_length * 2];
    // limb aux3[number_length * 2];

    // Couple problems here:
    //   Sometimes 'product' is one of the factors. In that case
    //   we get the wrong result when we use it as an intermediate,
    //   right? I guess the two factors are dead after that first
    //   call?
    //   Need to be careful about the size of the buffer, too.
    limb *dest = product;
    // memset(dest, 0, number_length * sizeof (limb));

    // Port note: This used to use product as a temporary, but
    // product may only have space for number_length limbs.
    // Compute T
    MultiplyLimbs(factor1, factor2, dest, number_length);
    // Compute m
    MultiplyLimbs(dest, params.MontgomeryMultN, aux, number_length);
    // Compute mN
    MultiplyLimbs(aux, modulus_array, aux2, number_length);
    // This is likely the last step of REDC, the conditional
    // subtraction.
    endBigModmult(aux2, number_length, modulus_array, product);
  }
}
#endif


static void endBigModmult(const limb *prodNotAdjusted, limb *product,
                          int modulus_length, const limb *modulus_array) {
  unsigned int cy = 0;
  // Compute hi(T) - hi(mN)
  // Where hi(number) is the high half of number.
  int index = modulus_length;
  int count;
  for (count = 0; count < modulus_length; count++) {
    cy = (unsigned int)(product + index)->x - (unsigned int)(prodNotAdjusted+index)->x -
      (cy >> BITS_PER_GROUP);
    (product + count)->x = UintToInt(cy & MAX_VALUE_LIMB);
    index++;
  }
  // Check whether this number is less than zero.
  if ((int)cy < 0) {
    // The number is less than zero. Add TestNbr.
    cy = 0;
    for (count = 0; count < modulus_length; count++) {
      cy = (unsigned int)(product + count)->x + (unsigned int)modulus_array[count].x +
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
void ModMultInternal(const MontgomeryParams &params,
             const limb* factor1, const limb* factor2,
             int modulus_length, const limb *modulus_array,
             limb* product, const char *caller, int line) {

  // These args are not necessary any more; they are part of the
  // Montgomery params now.
  if (SELF_CHECK) {
    CHECK(modulus_length == params.modulus_length);
    CHECK(0 == memcmp(params.modulus.data(),
                      modulus_array,
                      (params.modulus_length + 1) * sizeof (limb)));
  }

  const BigInt f1 = LimbsToBigInt(factor1, modulus_length);
  const BigInt f2 = LimbsToBigInt(factor2, modulus_length);
  const BigInt modulus = LimbsToBigInt(modulus_array, modulus_length);

  printf("[%d] %s * %s mod %s = ",
         modulus_length,
         f1.ToString().c_str(), f2.ToString().c_str(),
         modulus.ToString().c_str());

  if (params.powerOf2Exponent != 0) {
    BigInteger tmpFact1, tmpFact2;
    // TestNbr is a power of 2.
    UncompressLimbsBigInteger(modulus_length, factor1, &tmpFact1);
    UncompressLimbsBigInteger(modulus_length, factor2, &tmpFact2);
    (void)BigIntMultiply(&tmpFact1, &tmpFact2, &tmpFact1);
    CompressLimbsBigInteger(modulus_length, product, &tmpFact1);
    (product + (params.powerOf2Exponent / BITS_PER_GROUP))->x &=
      (1 << (params.powerOf2Exponent % BITS_PER_GROUP)) - 1;

    const BigInt ret = LimbsToBigInt(product, modulus_length);

    printf("(A) %s (%s:%d)\n",
           ret.ToString().c_str(),
           caller, line);

    return;
  }

  if (modulus_length <= 1) {
    BigInt f1 = LimbsToBigInt(factor1, modulus_length);
    BigInt f2 = LimbsToBigInt(factor2, modulus_length);

    BigInt modulus = LimbsToBigInt(modulus_array, modulus_length);
    // Hmm, no BigInt modular multiplication :/
    BigInt r = BigInt::Mod(BigInt::Times(f1, f2), modulus);
    BigIntToFixedLimbs(r, modulus_length, product);

    const BigInt ret = LimbsToBigInt(product, modulus_length);

    printf("(B) %s (%s:%d)\n",
           ret.ToString().c_str(),
           caller, line);

    return;
  }
  // if (true || modulus_length > MONTGOMERY_MULT_THRESHOLD)
  {
    limb aux[MAX_LEN], aux2[MAX_LEN];
    // Compute T
    MultiplyLimbs(factor1, factor2, product, modulus_length);
    // Compute m
    MultiplyLimbs(product, params.MontgomeryMultN, aux, modulus_length);
    // Compute mN
    MultiplyLimbs(aux, modulus_array, aux2, modulus_length);
    endBigModmult(aux2, product, modulus_length, modulus_array);

    const BigInt ret = LimbsToBigInt(product, modulus_length);

    printf("(C) %s (%s:%d)\n",
           ret.ToString().c_str(),
           caller, line);

    return;
  }
}

/*
void MultiplyLimbs(const limb* factor1, const limb* factor2, limb* result,
                   int len);
*/
