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
#include "bignbr.h"
#include "multiply.h"

#include "base/logging.h"
#include "bigconv.h"

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

enum eNbrCached MontgomeryMultNCached;
enum eNbrCached TestNbrCached;

// These are globals that are regularly modified in secret in other
// code as well. I think they usually (always?) describe the modulus.
// XXX Pass them as parameters!
int NumberLength;
limb TestNbr[MAX_LEN];
// indicates that TestNbr is a power of 2
int powerOf2Exponent;


limb MontgomeryMultN[MAX_LEN];
limb MontgomeryMultR1[MAX_LEN];
limb MontgomeryMultR2[MAX_LEN];
static limb aux[MAX_LEN];
static limb aux3[MAX_LEN];
static limb aux4[MAX_LEN];
static limb aux5[MAX_LEN];
static limb aux6[MAX_LEN];
static limb resultModOdd[MAX_LEN];
static limb resultModPower2[MAX_LEN];
static int NumberLength2;
static int NumberLengthR1;
static limb U[MAX_LEN];
static limb V[MAX_LEN];
static limb R[MAX_LEN];
static limb S[MAX_LEN];
static limb Ubak[MAX_LEN];
static limb Vbak[MAX_LEN];
static BigInteger tmpDen;
static BigInteger tmpNum;
static BigInteger oddValue;

static void smallmodmult(int factor1, int factor2, limb *product, int mod) {
  if (mod < SMALL_NUMBER_BOUND) {
    product->x = factor1 * factor2 % mod;
  } else {
    // TestNbr has one limb but it is not small.
    product->x = (int64_t)factor1 * factor2 % mod;
  }
}

// Multiply big number in Montgomery notation by integer.
static void modmultInt(limb* factorBig, int factorInt, limb* result,
                       const limb* pTestNbr, int nbrLen) {
  int64_t carry;
  int i;
  int TrialQuotient;
  limb* ptrFactorBig;
  const limb* ptrTestNbr;
  double dTestNbr;
  double dFactorBig;
  if (nbrLen == 1)
  {
    smallmodmult(factorBig->x, factorInt, result, pTestNbr->x);
    return;
  }
  (factorBig + nbrLen)->x = 0;
  dTestNbr = getMantissa(pTestNbr + nbrLen, nbrLen);
  dFactorBig = getMantissa(factorBig + nbrLen, nbrLen);
  TrialQuotient = (int)(unsigned int)floor((dFactorBig * (double)factorInt / dTestNbr) + 0.5);
  if ((unsigned int)TrialQuotient >= LIMB_RANGE)
  {   // Maximum value for limb.
    TrialQuotient = MAX_VALUE_LIMB;
  }
  // Compute result as factorBig * factorInt - TrialQuotient * TestNbr
  ptrFactorBig = factorBig;
  ptrTestNbr = pTestNbr;

  carry = 0;
  for (i = 0; i <= nbrLen; i++) {
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
    for (i = 0; i <= nbrLen; i++)
    {
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
void BigIntModularPower(const BigInteger* base, const BigInteger* exponent,
                        BigInteger* power) {
  int lenBytes;
  CompressLimbsBigInteger(NumberLength, aux5, base);
  modmult(aux5, MontgomeryMultR2,
          NumberLength, TestNbr,
          aux6);   // Convert base to Montgomery notation.
  modPow(aux6, exponent->limbs, exponent->nbrLimbs, aux5);
  lenBytes = NumberLength * (int)sizeof(limb);
  (void)memset(aux4, 0, lenBytes); // Convert power to standard notation.
  aux4[0].x = 1;
  modmult(aux4, aux5,
          NumberLength, TestNbr,
          aux6);
  UncompressLimbsBigInteger(NumberLength, aux6, power);
}

// Input: base = base in Montgomery notation.
//        exp  = exponent.
//        nbrGroupsExp = number of limbs of exponent.
// Output: power = power in Montgomery notation.
void modPow(const limb* base, const limb* exp, int nbrGroupsExp, limb* power) {
  int lenBytes = (NumberLength + 1) * (int)sizeof(*power);
  (void)memcpy(power, MontgomeryMultR1, lenBytes);  // power <- 1
  for (int index = nbrGroupsExp - 1; index >= 0; index--) {
    int groupExp = (exp + index)->x;
    for (unsigned int mask = HALF_INT_RANGE_U; mask > 0U; mask >>= 1) {
      modmult(power, power,
              NumberLength, TestNbr,
              power);
      if (((unsigned int)groupExp & mask) != 0U) {
        modmult(power, base,
                NumberLength, TestNbr,
                power);
      }
    }
  }
}

void modPowBaseInt(int base, const limb* exp, int nbrGroupsExp, limb* power) {
  int NumberLengthBytes = (NumberLength + 1) * (int)sizeof(limb);
  (void)memcpy(power, MontgomeryMultR1, NumberLengthBytes);  // power <- 1
  for (int index = nbrGroupsExp - 1; index >= 0; index--) {
    int groupExp = (exp + index)->x;
    for (unsigned int mask = HALF_INT_RANGE_U; mask > 0U; mask >>= 1) {
      modmult(power, power,
              NumberLength, TestNbr,
              power);
      if (((unsigned int)groupExp & mask) != 0U) {
        modmultInt(power, base, power, TestNbr, NumberLength);
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
  int borrow;
  int prevLimb;
  // Perform first <- (first - second)/2.
  borrow = first->x - second->x;
  prevLimb = UintToInt((unsigned int)borrow & MAX_VALUE_LIMB);
  borrow >>= BITS_PER_GROUP;
  for (i = 1; i < len; i++)
  {
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
  for (; len > 0; len--)
  {
    if ((first + len)->x != 0)
    {
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
  while (V3 != 0)
  {
    if (U3 < (V3 + V3))
    {               // QQ = 1
      T1 = U1 - V1;
      T3 = U3 - V3;
    }
    else
    {
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

static void InitHighUandV(int lenU, int lenV, double* pHighU, double* pHighV)
{
  double highU;
  double highV;
  double dLimbRange = (double)LIMB_RANGE;
  if (lenV >= lenU)
  {
    highV = ((double)V[lenV - 1].x * dLimbRange) + (double)V[lenV - 2].x;
    if (lenV >= 3)
    {
      highV += (double)V[lenV - 3].x / dLimbRange;
    }
    if (lenV == lenU)
    {
      highU = ((double)U[lenV - 1].x * dLimbRange) + (double)U[lenV - 2].x;
    }
    else if (lenV == (lenU + 1))
    {
      highU = (double)U[lenV - 2].x;
    }
    else
    {
      highU = 0;
    }
    if ((lenV <= (lenU + 2)) && (lenV >= 3))
    {
      highU += (double)U[lenV - 3].x / dLimbRange;
    }
  }
  else
  {
    highU = ((double)U[lenU - 1].x * (double)LIMB_RANGE) + (double)U[lenU - 2].x;
    if (lenU >= 3)
    {
      highU += (double)U[lenU - 3].x / dLimbRange;
    }
    if (lenU == (lenV + 1))
    {
      highV = (double)V[lenU - 2].x;
    }
    else
    {
      highV = 0;
    }
    if ((lenU <= (lenV + 2)) && (lenU >= 3))
    {
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
static bool ModInvBigNbr(limb* num, limb* inv, limb* mod, int nbrLen) {
  int k;
  int steps;
  int a;
  int b;
  int c;
  int d;  // Coefficients used to update variables R, S, U, V.
  int size;
  int i;
  int bitCount;
  int lenRS;
  int lenU;
  int lenV;
  int lowU;
  int lowV;
  unsigned int borrow;
  assert(nbrLen >= 1);
  if (nbrLen == 1)
  {
    inv->x = modInv(num->x, mod->x);
    return true;
  }
  if (powerOf2Exponent != 0)
  {    // TestNbr is a power of 2.
    unsigned int powerExp = (unsigned int)powerOf2Exponent % (unsigned int)BITS_PER_GROUP;
    ComputeInversePower2(num, inv, aux, NumberLength);
    (inv + (powerOf2Exponent / BITS_PER_GROUP))->x &= UintToInt((1U << powerExp) - 1U);
    return true;
  }
  //  1. U <- M, V <- X, R <- 0, S <- 1, k <- 0
  size = (nbrLen + 1) * (int)sizeof(limb);
  (mod + nbrLen)->x = 0;
  (num + nbrLen)->x = 0;
  (void)memcpy(U, mod, size);
  (void)memcpy(V, num, size);
  // Maximum value of R and S can be up to 2*M, so one more limb is needed.
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
  for (lenU = nbrLen - 1; lenU > 0; lenU--)
  {
    if (U[lenU].x != 0)
    {
      break;
    }
  }
  lenU++;
  // Find length of V.
  for (lenV = nbrLen - 1; lenV > 0; lenV--)
  {
    if (V[lenV].x != 0)
    {
      break;
    }
  }
  lenV++;
  lowU = U[0].x;
  lowV = V[0].x;
  // Initialize highU and highV.
  if ((lenU > 1) || (lenV > 1))
  {
    double highU;
    double highV;
    InitHighUandV(lenU, lenV, &highU, &highV);
    //  2. while V > 0 do
    for (;;)
    {
      //  3.   if U even then U <- U / 2, S <- 2S
      if ((lowU & 1) == 0)
      {     // U is even.
        lowU >>= 1;
        highV += highV;
        // R' <- aR + bS, S' <- cR + dS
        c *= 2;
        d *= 2;  // Multiply S by 2.
      }
      //  4.   elsif V even then V <- V / 2, R <- 2R
      else if ((lowV & 1) == 0)
      {    // V is even.
        lowV >>= 1;
        highU += highU;
        // R' <- aR + bS, S' <- cR + dS
        a *= 2;
        b *= 2;  // Multiply R by 2.
      }
      else
      {
        //  5.   elsif U >= V  then U <- (U - V) / 2, R <- R + S, S <- 2S
        if (highU > highV)
        {     // U > V. Perform U <- (U - V) / 2
          lowU = (lowU - lowV) / 2;
          highU -= highV;
          highV += highV;
          // R' <- aR + bS, S' <- cR + dS
          a += c;
          b += d;  // R <- R + S
          c *= 2;
          d *= 2;  // S <- 2S
        }
        //  6.   elsif V >= U then V <- (V - U) / 2, S <- S + R, R <- 2R
        else
        {    // V >= U. Perform V <- (V - U) / 2
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
      if (steps == BITS_PER_GROUP_MINUS_1)
      {  // compute now U and V and reset e, f, g and h.
         // U' <- eU + fV, V' <- gU + hV
        int lenBytes;
        int len = ((lenU > lenV)? lenU : lenV);
        lenBytes = (len - lenU + 1) * (int)sizeof(limb);
        (void)memset(&U[lenU].x, 0, lenBytes);
        lenBytes = (len - lenV + 1) * (int)sizeof(limb);
        (void)memset(&V[lenV].x, 0, lenBytes);
        lenBytes = (len + 1) * (int)sizeof(limb);
        (void)memcpy(Ubak, U, lenBytes);
        (void)memcpy(Vbak, V, lenBytes);
        AddMult(U, a, -b, V, -c, d, len);
        if ((((unsigned int)U[lenU].x | (unsigned int)V[lenV].x) & FOURTH_INT_RANGE_U) != 0U)
        {    // Complete expansion of U and V required for all steps.
            //  2. while V > 0 do
          (void)memcpy(U, Ubak, lenBytes);
          (void)memcpy(V, Vbak, lenBytes);
          b = 0;
          c = 0;  // U' = U, V' = V.
          a = 1;
          d = 1;
          while ((lenV > 1) || (V[0].x > 0))
          {
            //  3.   if U even then U <- U / 2, S <- 2S
            if ((U[0].x & 1) == 0)
            {     // U is even.
              for (i = 0; i < lenU; i++)
              {  // Loop that divides U by 2.
                U[i].x = UintToInt((((unsigned int)U[i].x >> 1) |
                  ((unsigned int)U[i + 1].x << BITS_PER_GROUP_MINUS_1)) &
                  MAX_VALUE_LIMB);
              }
              if (U[lenU - 1].x == 0)
              {
                lenU--;
              }
              // R' <- aR + bS, S' <- cR + dS
              c *= 2;
              d *= 2;  // Multiply S by 2.
            }
            //  4.   elsif V even then V <- V / 2, R <- 2R
            else if ((V[0].x & 1) == 0)
            {    // V is even.
              for (i = 0; i < lenV; i++)
              {  // Loop that divides V by 2.
                V[i].x = UintToInt((((unsigned int)V[i].x >> 1) |
                  ((unsigned int)V[i + 1].x << BITS_PER_GROUP_MINUS_1)) &
                  MAX_VALUE_LIMB);
              }
              if (V[lenV - 1].x == 0)
              {
                lenV--;
              }
              // R' <- aR + bS, S' <- cR + dS
              a *= 2;
              b *= 2;  // Multiply R by 2.
            }
            //  5.   elsif U >= V  then U <- (U - V) / 2, R <- R + S, S <- 2S
            else
            {
              len = ((lenU > lenV)? lenU : lenV);
              for (i = len - 1; i > 0; i--)
              {
                if (U[i].x != V[i].x)
                {
                  break;
                }
              }
              if (U[i].x > V[i].x)
              {     // U > V
                lenU = HalveDifference(U, V, len); // U <- (U - V) / 2
                                                   // R' <- aR + bS, S' <- cR + dS
                a += c;
                b += d;  // R <- R + S
                c *= 2;
                d *= 2;  // S <- 2S
              }
              //  6.   elsif V >= U then V <- (V - U) / 2, S <- S + R, R <- 2R
              else
              {    // V >= U
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
            if ((k % BITS_PER_GROUP_MINUS_1) == 0)
            {
              break;
            }
          }
          if ((lenV == 1) && (V[0].x == 0))
          {
            break;
          }
        }
        else
        {
          k += steps;
          for (i = 0; i < lenU; i++)
          {  // Loop that divides U by 2^BITS_PER_GROUP_MINUS_1.
            U[i].x = UintToInt((((unsigned int)U[i].x >> BITS_PER_GROUP_MINUS_1) |
              ((unsigned int)U[i + 1].x << 1)) &
              MAX_VALUE_LIMB);
          }
          U[lenU].x = 0;
          while ((lenU > 0) && (U[lenU - 1].x == 0))
          {
            lenU--;
          }
          for (i = 0; i < lenV; i++)
          {  // Loop that divides V by 2^BITS_PER_GROUP_MINUS_1.
            V[i].x = UintToInt((((unsigned int)V[i].x >> BITS_PER_GROUP_MINUS_1) |
              ((unsigned int)V[i + 1].x << 1)) &
              MAX_VALUE_LIMB);
          }
          V[lenV].x = 0;
          while ((lenV > 0) && (V[lenV - 1].x == 0))
          {
            lenV--;
          }
        }
        steps = 0;
        AddMult(R, a, b, S, c, d, lenRS);
        if ((R[lenRS].x != 0) || (S[lenRS].x != 0))
        {
          lenRS++;
        }
        lowU = U[0].x;
        lowV = V[0].x;
        b = 0;
        c = 0;  // U' = U, V' = V.
        a = 1;
        d = 1;
        if ((lenU == 0) || (lenV == 0) || ((lenV == 1) && (lenU == 1)))
        {
          break;
        }
        InitHighUandV(lenU, lenV, &highU, &highV);
      }
    }
  }
  if (lenU > 0)
  {
    //  2. while V > 0 do
    while (lowV > 0)
    {
      //  3.   if U even then U <- U / 2, S <- 2S
      if ((lowU & 1) == 0)
      {     // U is even.
        lowU >>= 1;
        // R' <- aR + bS, S' <- cR + dS
        c *= 2;
        d *= 2;  // Multiply S by 2.
      }
      //  4.   elsif V even then V <- V / 2, R <- 2R
      else if ((lowV & 1) == 0)
      {    // V is even.
        lowV >>= 1;
        // R' <- aR + bS, S' <- cR + dS
        a *= 2;
        b *= 2;  // Multiply R by 2.
      }
      //  5.   elsif U >= V  then U <- (U - V) / 2, R <- R + S, S <- 2S
      else if (lowU > lowV)
      {     // U > V. Perform U <- (U - V) / 2
        lowU = (lowU - lowV) >> 1;
        // R' <- aR + bS, S' <- cR + dS
        a += c;
        b += d;  // R <- R + S
        c *= 2;
        d *= 2;  // S <- 2S
      }
      //  6.   elsif V >= U then V <- (V - U) / 2, S <- S + R, R <- 2R
      else
      {    // V >= U. Perform V <- (V - U) / 2
        lowV = (lowV - lowU) >> 1;
        // R' <- aR + bS, S' <- cR + dS
        c += a;
        d += b;  // S <- S + R
        a *= 2;
        b *= 2;  // R <- 2R
      }
      //  7.   k <- k + 1
      steps++;
      if (steps >= BITS_PER_GROUP_MINUS_1)
      {  // compute now R and S and reset a, b, c and d.
         // R' <- aR + bS, S' <- cR + dS
        AddMult(R, a, b, S, c, d, nbrLen + 1);
        b = 0;     // R' = R, S' = S.
        c = 0;
        a = 1;
        d = 1;
        k += steps;
        if (k > (nbrLen * 64))
        {
          return false;  // Could not compute inverse.
        }
        steps = 0;
      }
    }
  }
  AddMult(R, a, b, S, c, d, nbrLen + 1);
  k += steps;
  //  8. if R >= M then R <- R - M
  for (i = nbrLen; i > 0; i--)
  {
    if (R[i].x != (mod + i)->x)
    {
      break;
    }
  }
  if ((unsigned int)R[i].x >= (unsigned int)(mod + i)->x)
  {      // R >= M.
    borrow = 0U;
    for (i = 0; i <= nbrLen; i++)
    {
      borrow = (unsigned int)R[i].x - (unsigned int)(mod + i)->x - borrow;
      R[i].x = UintToInt(borrow & MAX_VALUE_LIMB);
      borrow >>= BITS_PER_GROUP;
    }
  }
  //  9. R <- M - R
  borrow = 0U;
  for (i = 0; i <= nbrLen; i++)
  {
    borrow = (unsigned int)(mod + i)->x - (unsigned int)R[i].x - borrow;
    R[i].x = UintToInt(borrow & MAX_VALUE_LIMB);
    borrow >>= BITS_PER_GROUP;
  }
  R[nbrLen].x = 0;
  // At this moment R = x^(-1)*2^k
  // 10. R <- MonPro(R, R2)
  modmult(R, MontgomeryMultR2,
          NumberLength, TestNbr,
          R);
  R[nbrLen].x = 0;
  // At this moment R = x^(-1)*2^(k+m)
  // 11. return MonPro(R, 2^(m-k))
  (void)memset(S, 0, size);
  bitCount = (nbrLen * BITS_PER_GROUP) - k;
  if (bitCount < 0)
  {
    unsigned int shLeft;
    bitCount += nbrLen * BITS_PER_GROUP;
    shLeft = (unsigned int)bitCount % (unsigned int)BITS_PER_GROUP;
    S[bitCount / BITS_PER_GROUP].x = UintToInt(1U << shLeft);
    modmult(R, S,
            NumberLength, TestNbr,
            inv);
  }
  else
  {
    unsigned int shLeft;
    shLeft = (unsigned int)bitCount % (unsigned int)BITS_PER_GROUP;
    S[bitCount / BITS_PER_GROUP].x = UintToInt(1U << shLeft);
    modmult(R, S,
            NumberLength, TestNbr,
            inv);
    modmult(inv, MontgomeryMultR2,
            NumberLength, TestNbr,
            inv);
  }
  return true;  // Inverse computed.
}

// Compute modular division for odd moduli.
void BigIntModularDivision(const BigInteger* Num, const BigInteger* Den,
                           const BigInteger* mod, BigInteger* quotient) {
  NumberLength = mod->nbrLimbs;
  // Reduce Num modulo mod.
  (void)BigIntRemainder(Num, mod, &tmpNum);
  if (tmpNum.sign == SIGN_NEGATIVE)
  {
    BigIntAdd(&tmpNum, mod, &tmpNum);
  }
  // Reduce Den modulo mod.
  (void)BigIntRemainder(Den, mod, &tmpDen);
  if (tmpDen.sign == SIGN_NEGATIVE)
  {
    BigIntAdd(&tmpDen, mod, &tmpDen);
  }
  CompressLimbsBigInteger(NumberLength, aux3, &tmpDen);
  // aux3 <- Den in Montgomery notation
  // tmpDen.limbs <- 1 / Den in Montg notation.
  modmult(aux3, MontgomeryMultR2,
          NumberLength, TestNbr,
          aux3);
  (void)ModInvBigNbr(aux3, tmpDen.limbs, TestNbr, NumberLength);
  CompressLimbsBigInteger(NumberLength, aux4, &tmpNum);
          // aux3 <- Num / Den in standard notation.
  modmult(tmpDen.limbs, aux4,
          NumberLength, TestNbr,
          aux3);
  UncompressLimbsBigInteger(NumberLength, aux3, quotient);  // Get Num/Den
}

// On input:
// oddValue = odd modulus.
// resultModOdd = result mod odd value
// resultModPower2 = result mod 2^shRight
// result = pointer to result.
// From Knuth's TAOCP Vol 2, section 4.3.2:
// If c = result mod odd, d = result mod 2^k:
// compute result = c + (((d-c)*modinv(odd,2^k))%2^k)*odd
static void ChineseRemainderTheorem(int shRight, BigInteger* result) {
  if (shRight == 0) {
    const int number_length = oddValue.nbrLimbs;
    NumberLength = number_length;
    UncompressLimbsBigInteger(NumberLength, resultModOdd, result);
    return;
  }

  if (NumberLength > oddValue.nbrLimbs) {
    int lenBytes = (NumberLength - oddValue.nbrLimbs) * (int)sizeof(limb);
    (void)memset(&oddValue.limbs[oddValue.nbrLimbs], 0, lenBytes);
  }
  SubtractBigNbr(resultModPower2, resultModOdd, aux3, NumberLength);
  ComputeInversePower2(oddValue.limbs, aux4, aux, NumberLength);
  modmult(aux4, aux3,
          NumberLength, TestNbr,
          aux5);
  (aux5 + (shRight / BITS_PER_GROUP))->x &= (1 << (shRight % BITS_PER_GROUP)) - 1;

  if (NumberLength < oddValue.nbrLimbs) {
    int lenBytes = (oddValue.nbrLimbs - NumberLength) * (int)sizeof(limb);
    (void)memset(&aux5[NumberLength], 0, lenBytes);
  }
  UncompressLimbsBigInteger(NumberLength, aux5, result);
  (void)BigIntMultiply(result, &oddValue, result);
  NumberLength = oddValue.nbrLimbs;
  UncompressLimbsBigInteger(NumberLength, resultModOdd, &tmpDen);
  BigIntAdd(result, &tmpDen, result);
}

// Compute modular division. ModInvBigNbr does not support even moduli,
// so the division is done separately by calculating the division modulo
// n/2^k (n odd) and 2^k and then merge the results using Chinese Remainder
// Theorem.
void BigIntGeneralModularDivision(const BigInteger* Num, const BigInteger* Den,
  const BigInteger* mod, BigInteger* quotient)
{
  int shRight;
  int NumberLengthBytes;
  CopyBigInt(&oddValue, mod);
  DivideBigNbrByMaxPowerOf2(&shRight, oddValue.limbs, &oddValue.nbrLimbs);
  // Reduce Num modulo oddValue.
  (void)BigIntRemainder(Num, &oddValue, &tmpNum);
  if (tmpNum.sign == SIGN_NEGATIVE)
  {
    BigIntAdd(&tmpNum, &oddValue, &tmpNum);
  }
  // Reduce Den modulo oddValue.
  (void)BigIntRemainder(Den, &oddValue, &tmpDen);
  if (tmpDen.sign == SIGN_NEGATIVE)
  {
    BigIntAdd(&tmpDen, &oddValue, &tmpDen);
  }
  NumberLength = oddValue.nbrLimbs;
  NumberLengthBytes = NumberLength * (int)sizeof(limb);
  (void)memcpy(TestNbr, oddValue.limbs, NumberLengthBytes);
  TestNbr[NumberLength].x = 0;
  GetMontgomeryParams(NumberLength);
  CompressLimbsBigInteger(NumberLength, aux3, &tmpDen);
  // aux3 <- Den in Montgomery notation
  modmult(aux3, MontgomeryMultR2,
          NumberLength, TestNbr,
          aux3);
  (void)ModInvBigNbr(aux3, aux3, TestNbr, NumberLength); // aux3 <- 1 / Den in Montg notation.
  CompressLimbsBigInteger(NumberLength, aux4, &tmpNum);
  // resultModOdd <- Num / Dev in standard notation.
  modmult(aux3, aux4,
          NumberLength, TestNbr,
          resultModOdd);

  // Compute inverse mod power of 2.
  if (shRight == 0)
  {    // Modulus is odd. Quotient already computed.
    const int number_length = oddValue.nbrLimbs;
    NumberLength = number_length;
    UncompressLimbsBigInteger(number_length, resultModOdd, quotient);
    return;
  }
  NumberLength = (shRight + BITS_PER_GROUP_MINUS_1) / BITS_PER_GROUP;
  CompressLimbsBigInteger(NumberLength, aux3, Den);
  ComputeInversePower2(aux3, aux4, aux, NumberLength);
  powerOf2Exponent = shRight;
  // resultModPower2 <- Num / Dev modulus 2^k.
  modmult(Num->limbs, aux4,
          NumberLength, TestNbr,
          resultModPower2);
  ChineseRemainderTheorem(shRight, quotient);
  powerOf2Exponent = 0;
}

// Find the inverse of value mod 2^(NumberLength*BITS_PER_GROUP)
void ComputeInversePower2(const limb *value, limb *result, limb *tmp, int number_length) {
  unsigned int Cy;
  int N = value->x;            // 2 least significant bits of inverse correct.
  int x = N;
  x = x * (2 - (N * x));       // 4 least significant bits of inverse correct.
  x = x * (2 - (N * x));       // 8 least significant bits of inverse correct.
  x = x * (2 - (N * x));       // 16 least significant bits of inverse correct.
  x = x * (2 - (N * x));       // 32 least significant bits of inverse correct.
  result->x = UintToInt((unsigned int)x & MAX_VALUE_LIMB);
  for (int currLen = 2; currLen < number_length; currLen <<= 1)
  {
    multiply(value, result, tmp, currLen, NULL);    // tmp <- N * x
    Cy = 2U - (unsigned int)tmp[0].x;
    tmp[0].x = UintToInt(Cy & MAX_VALUE_LIMB);
    for (int j = 1; j < currLen; j++)
    {
      Cy = (unsigned int)(-tmp[j].x) - (Cy >> BITS_PER_GROUP);
      tmp[j].x = UintToInt(Cy & MAX_VALUE_LIMB);
    }                                                  // tmp <- 2 - N * x
    multiply(result, tmp, result, currLen, NULL);      // tmp <- x * (2 - N * x)
  }

  // Perform last approximation to inverse.
  multiply(value, result, tmp, number_length, NULL);    // tmp <- N * x
  Cy = 2U - (unsigned int)tmp[0].x;
  tmp[0].x = UintToInt(Cy & MAX_VALUE_LIMB);
  for (int j = 1; j < number_length; j++) {
    Cy = (unsigned int)(-tmp[j].x) - (Cy >> BITS_PER_GROUP);
    tmp[j].x = UintToInt(Cy & MAX_VALUE_LIMB);
  }                                                    // tmp <- 2 - N * x
  multiply(result, tmp, result, number_length, NULL);   // tmp <- x * (2 - N * x)
}

void GetMontgomeryParamsPowerOf2(int powerOf2)
{
  NumberLength = (powerOf2 + BITS_PER_GROUP - 1) / BITS_PER_GROUP;
  int NumberLengthBytes = NumberLength * (int)sizeof(limb);
  powerOf2Exponent = powerOf2;
  (void)memset(MontgomeryMultR1, 0, NumberLengthBytes);
  (void)memset(MontgomeryMultR2, 0, NumberLengthBytes);
  MontgomeryMultR1[0].x = 1;
  MontgomeryMultR2[0].x = 1;
}

// Compute Nbr <- Nbr mod Modulus.
// Modulus has NumberLength limbs.
static void AdjustModN(limb *Nbr, const limb *Modulus, int nbrLen) {
  double dInvModulus = 1/getMantissa(Modulus+nbrLen, nbrLen);
  double dNbr = getMantissa(Nbr + nbrLen + 1, nbrLen + 1) * LIMB_RANGE;
  int TrialQuotient = (int)(unsigned int)floor((dNbr * dInvModulus) + 0.5);
  if ((unsigned int)TrialQuotient >= LIMB_RANGE)
  {   // Maximum value for limb.
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

void GetMontgomeryParams(int len) {
  int j;
  int NumberLengthBytes;
  MontgomeryMultNCached = NBR_NOT_CACHED;
  TestNbrCached = NBR_NOT_CACHED;
  TestNbr[len].x = 0;
  NumberLength = len;
  NumberLength2 = len + len;
  powerOf2Exponent = 0;    // Indicate not power of 2 in advance.
  NumberLengthR1 = 1;

  if ((NumberLength == 1) && ((TestNbr[0].x & 1) != 0))
  {
    MontgomeryMultR1[0].x = 1;
    MontgomeryMultR2[0].x = 1;
    return;
  }

  // Check whether TestNbr is a power of 2.
  for (j = 0; j < (NumberLength-1); j++)
  {
    if (TestNbr[j].x != 0)
    {
      break;
    }
  }

  if (j == (NumberLength - 1))
  {      // TestNbr is a power of 2.
    int value = TestNbr[NumberLength - 1].x;
    for (j = 0; j < BITS_PER_GROUP; j++)
    {
      if (value == 1)
      {
        NumberLengthBytes = NumberLength * (int)sizeof(limb);
        powerOf2Exponent = ((NumberLength - 1)*BITS_PER_GROUP) + j;
        (void)memset(MontgomeryMultR1, 0, NumberLengthBytes);
        (void)memset(MontgomeryMultR2, 0, NumberLengthBytes);
        MontgomeryMultR1[0].x = 1;
        MontgomeryMultR2[0].x = 1;
        return;
      }
      value >>= 1;
    }
  }

  // Compute MontgomeryMultN as 1/TestNbr (mod 2^k) using Newton method,
  // which doubles the precision for each iteration.
  // In the formula above: k = BITS_PER_GROUP * NumberLength.
  ComputeInversePower2(TestNbr, MontgomeryMultN, aux, NumberLength);
  MontgomeryMultN[NumberLength].x = 0;
  // Compute MontgomeryMultR1 as 1 in Montgomery notation,
  // this is 2^(NumberLength*BITS_PER_GROUP) % TestNbr.
  j = NumberLength;
  MontgomeryMultR1[j].x = 1;
  do
  {
    j--;
    MontgomeryMultR1[j].x = 0;
  } while (j > 0);
  AdjustModN(MontgomeryMultR1, TestNbr, len);
  MontgomeryMultR1[NumberLength].x = 0;
  NumberLengthBytes = (NumberLength + 1) * (int)sizeof(limb);
  (void)memcpy(MontgomeryMultR2, MontgomeryMultR1, NumberLengthBytes);
  for (NumberLengthR1 = NumberLength; NumberLengthR1 > 0; NumberLengthR1--)
  {
    if (MontgomeryMultR1[NumberLengthR1 - 1].x != 0)
    {
      break;
    }
  }
  // Compute MontgomeryMultR2 as 2^(2*NumberLength*BITS_PER_GROUP) % TestNbr.
  for (j = NumberLength; j > 0; j--)
  {
    NumberLengthBytes = NumberLength * (int)sizeof(limb);
    (void)memmove(&MontgomeryMultR2[1], &MontgomeryMultR2[0], NumberLengthBytes);
    MontgomeryMultR2[0].x = 0;
    AdjustModN(MontgomeryMultR2, TestNbr, len);
  }
  MontgomeryMultNCached = NBR_READY_TO_BE_CACHED;
  TestNbrCached = NBR_READY_TO_BE_CACHED;
}

void AddBigNbrModN(const limb *num1, const limb *num2, limb *sum,
                   const limb *modulus_array, int number_length) {
  BigInt f1 = LimbsToBigInt(num1, number_length);
  BigInt f2 = LimbsToBigInt(num2, number_length);
  BigInt modulus = LimbsToBigInt(modulus_array, number_length);
  BigInt r = BigInt::Mod(BigInt::Plus(f1, f2), modulus);
  /*
  fprintf(stderr, "%s + %s mod %s = %s\n",
          f1.ToString().c_str(), f2.ToString().c_str(),
          modulus.ToString().c_str(),
          r.ToString().c_str());
  */
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



// PERF: Can use montgomery multiplication here; probably should add this
// to BigInt or bigint utils.
//   https://en.wikipedia.org/wiki/Montgomery_modular_multiplication
void modmult(const limb* factor1, const limb* factor2,
             int number_length, const limb *modulus_array,
             limb* product) {
  BigInt f1 = LimbsToBigInt(factor1, number_length);
  BigInt f2 = LimbsToBigInt(factor2, number_length);

  BigInt modulus = LimbsToBigInt(modulus_array, number_length);
  // Hmm, no BigInt modular multiplication :/
  BigInt r = BigInt::Mod(BigInt::Times(f1, f2), modulus);

  // modmultInternal(factor1, factor2, product);
  // BigInt rr = LimbsToBigInt(product, number_length);
  // CHECK(BigInt::Eq(r, rr));

  BigIntToFixedLimbs(r, number_length, product);
}
