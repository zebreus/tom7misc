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

#include <string.h>
#include <stdint.h>
#include <math.h>
#include <cassert>

#include "bignbr.h"
#include "factor.h"
#include "commonstruc.h"
#include "globals.h"
#include "siqs.h"
#include "modmult.h"
#include "ecm.h"

// The first entry is a header. The ptrFactor appears to be bogus.
// For the header, the multiplicity is the number of distinct factors
// after that.
struct sFactorsInternal {
  // This is an "int array" representation: The length followed by
  // the limbs.
  int *ptrFactor;
  int multiplicity;
  int upperBound;
  int type;
};

union uCommon common;
static int DegreeAurif;
static int NextEC;
static BigInteger power;
static BigInteger prime;
static bool foundByLehman;
static int EC;
static BigInteger Temp2;
static BigInteger Temp3;
static BigInteger Temp4;
BigInteger tofactor;

static long long Gamma[386];
static long long Delta[386];
static long long AurifQ[386];

static void insertBigFactor(struct sFactorsInternal *pstFactors, const BigInteger *divisor,
  int type);

static void factorExt(const BigInteger* toFactor, const int* number,
  int* factors, struct sFactorsInternal* pstFactors);

static int Cos(int N) {
  int NMod8 = N % 8;
  if (NMod8 == 0)
  {
    return 1;
  }
  if (NMod8 == 4)
  {
    return -1;
  }
  return 0;
}

static int intTotient(int argument)
{
  int trialDivisor;
  int argumentDivisor = argument;
  int totient = argumentDivisor;
  if ((argumentDivisor % 2) == 0)
  {
    totient /= 2;
    do
    {
      argumentDivisor /= 2;
    } while ((argumentDivisor % 2) == 0);
  }
  if ((argumentDivisor % 3) == 0)
  {
    totient = totient * 2 / 3;
    do
    {
      argumentDivisor /= 3;
    } while ((argumentDivisor % 3) == 0);
  }
  trialDivisor = 5;
  while ((trialDivisor * trialDivisor) <= argumentDivisor)
  {
    if (((trialDivisor % 3) != 0) && ((argumentDivisor % trialDivisor) == 0))
    {
      totient = totient * (trialDivisor - 1) / trialDivisor;
      do
      {
        argumentDivisor /= trialDivisor;
      } while ((argumentDivisor % trialDivisor) == 0);
    }
    trialDivisor += 2;
  }
  if (argumentDivisor > 1)
  {
    totient = totient * (argumentDivisor - 1) / argumentDivisor;
  }
  return totient;
}

int Moebius(int argument)
{
  int moebius;
  int argumentDivisor;
  int trialDivisor;

  moebius = 1;
  argumentDivisor = argument;
  if ((argumentDivisor % 2) == 0)
  {
    moebius = -moebius;
    argumentDivisor /= 2;
    if ((argumentDivisor % 2) == 0)
    {
      return 0;
    }
  }
  if ((argumentDivisor % 3) == 0)
  {
    moebius = -moebius;
    argumentDivisor /= 3;
    if ((argumentDivisor % 3) == 0)
    {
      return 0;
    }
  }
  trialDivisor = 5;
  while ((trialDivisor * trialDivisor) <= argumentDivisor)
  {
    if ((trialDivisor % 3) != 0)
    {
      while ((argumentDivisor % trialDivisor) == 0)
      {
        moebius = -moebius;
        argumentDivisor /= trialDivisor;
        if ((argumentDivisor % trialDivisor) == 0)
        {
          return 0;
        }
      }
    }
    trialDivisor += 2;
  }
  if (argumentDivisor > 1)
  {
    moebius = -moebius;
  }
  return moebius;
}

void GetAurifeuilleFactor(struct sFactorsInternal *pstFactors, int L, const BigInteger *BigBase)
{
  BigInteger x;
  BigInteger Csal;
  BigInteger Dsal;
  BigInteger Nbr1;
  int k;

  (void)BigIntPowerIntExp(BigBase, L, &x);   // x <- BigBase^L.
  intToBigInteger(&Csal, 1);
  intToBigInteger(&Dsal, 1);
  for (k = 1; k < DegreeAurif; k++)
  {
    longToBigInteger(&Nbr1, Gamma[k]);
    (void)BigIntMultiply(&Csal, &x, &Csal);
    BigIntAdd(&Csal, &Nbr1, &Csal);      // Csal <- Csal * x + Gamma[k]
    longToBigInteger(&Nbr1, Delta[k]);
    (void)BigIntMultiply(&Dsal, &x, &Dsal);
    BigIntAdd(&Dsal, &Nbr1, &Dsal);      // Dsal <- Dsal * x + Gamma[k]
  }
  longToBigInteger(&Nbr1, Gamma[k]);
  (void)BigIntMultiply(&Csal, &x, &Csal);
  BigIntAdd(&Csal, &Nbr1, &Csal);        // Csal <- Csal * x + Gamma[k]
  (void)BigIntPowerIntExp(BigBase, (L + 1) / 2, &Nbr1);   // Nbr1 <- Dsal * base^((L+1)/2)
  (void)BigIntMultiply(&Dsal, &Nbr1, &Nbr1);
  BigIntAdd(&Csal, &Nbr1, &Dsal);
  insertBigFactor(pstFactors, &Dsal, TYP_AURIF);
  BigIntSubt(&Csal, &Nbr1, &Dsal);
  insertBigFactor(pstFactors, &Dsal, TYP_AURIF);
}

// Get Aurifeuille factors.
void InsertAurifFactors(struct sFactorsInternal *pstFactors, const BigInteger *BigBase,
  int exponent, int increment)
{
  int Incre = increment;
  int Expon = exponent;
  int Base = BigBase->limbs[0].x;
  if ((BigBase->nbrLimbs != 1) || (Base >= 386))
  {
    return;    // Base is very big, so go out.
  }
  if (((Expon % 2) == 0) && (Incre == -1))
  {
    do
    {
      Expon /= 2;
    } while ((Expon % 2) == 0);
    Incre = (Base % 4) - 2;
  }
  if (((Expon % Base) == 0)
    && (((Expon / Base) % 2) != 0)
    && ((((Base % 4) != 1) && (Incre == 1)) || (((Base % 4) == 1) && (Incre == -1))))
  {
    int N1;
    int q1;
    int L;
    int k;
    int N = Base;
    if ((N % 4) == 1)
    {
      N1 = N;
    }
    else
    {
      N1 = 2 * N;
    }
    DegreeAurif = intTotient(N1) / 2;
    for (k = 1; k <= DegreeAurif; k += 2)
    {
      AurifQ[k] = JacobiSymbol(N, k);
    }
    for (k = 2; k <= DegreeAurif; k += 2)
    {
      int t1 = k; // Calculate t2 = gcd(k, N1)
      int t2 = N1;
      while (t1 != 0)
      {
        int t3 = t2 % t1;
        t2 = t1;
        t1 = t3;
      }
      t1 = Moebius(N1 / t2) * intTotient(t2) * Cos((N - 1) * k);
      AurifQ[k] = t1;
    }
    Gamma[0] = 1;
    Delta[0] = 1;
    for (k = 1; k <= (DegreeAurif / 2); k++)
    {
      Gamma[k] = 0;
      Delta[k] = 0;
      for (int j = 0; j < k; j++)
      {
        int m = 2 * (k - j);
        Gamma[k] = Gamma[k] + (N * AurifQ[m - 1] * Delta[j]) - (AurifQ[m] * Gamma[j]);
        Delta[k] = Delta[k] + (AurifQ[m + 1] * Gamma[j]) - (AurifQ[m] * Delta[j]);
      }
      Gamma[k] /= 2 * k;
      Delta[k] = (Delta[k] + Gamma[k]) / ((2 * k) + 1);
    }
    for (k = (DegreeAurif / 2) + 1; k <= DegreeAurif; k++)
    {
      Gamma[k] = Gamma[DegreeAurif - k];
    }
    for (k = (DegreeAurif + 1) / 2; k < DegreeAurif; k++)
    {
      Delta[k] = Delta[DegreeAurif - k - 1];
    }
    q1 = Expon / Base;
    L = 1;
    while ((L * L) <= q1)
    {
      if ((q1 % L) == 0)
      {
        GetAurifeuilleFactor(pstFactors,L, BigBase);
        if (q1 != (L * L))
        {
          GetAurifeuilleFactor(pstFactors, q1 / L, BigBase);
        }
      }
      L += 2;
    }
  }
}

static void Cunningham(struct sFactorsInternal *pstFactors, const BigInteger *BigBase, int Expon,
                       int increment, const BigInteger *BigOriginal)
{
  int Expon2;
  int k;
  BigInteger Nbr1;
  BigInteger Nbr2;

  Expon2 = Expon;

  // There used to be some code here that would consult a server
  // for factors, but it was never enabled. Might be more dead stuff
  // as a result? -tom7

  while (((Expon2 % 2) == 0) && (increment == -1)) {
    Expon2 /= 2;
    (void)BigIntPowerIntExp(BigBase, Expon2, &Nbr1);
    addbigint(&Nbr1, increment);
    insertBigFactor(pstFactors, &Nbr1, TYP_TABLE);
    InsertAurifFactors(pstFactors, BigBase, Expon2, 1);
  }
  k = 1;
  while ((k * k) <= Expon)
  {
    if ((Expon % k) == 0)
    {
      if ((k % 2) != 0)
      { /* Only for odd exponent */
        (void)BigIntPowerIntExp(BigBase, Expon / k, &Nbr1);
        addbigint(&Nbr1, increment);
        BigIntGcd(&Nbr1, BigOriginal, &Nbr2);   // Nbr2 <- gcd(Base^(Expon/k)+incre, original)
        insertBigFactor(pstFactors, &Nbr2, TYP_TABLE);
        // PERF why does this copy to tmp?
        BigInteger tmp;
        CopyBigInt(&tmp, BigOriginal);
        (void)BigIntDivide(&tmp, &Nbr2, &Nbr1);
        insertBigFactor(pstFactors, &Nbr1, TYP_TABLE);
        InsertAurifFactors(pstFactors, BigBase, Expon / k, increment);
      }
      if (((Expon / k) % 2) != 0)
      { /* Only for odd exponent */
        (void)BigIntPowerIntExp(BigBase, k, &Nbr1);
        addbigint(&Nbr1, increment);
        BigIntGcd(&Nbr1, BigOriginal, &Nbr2);   // Nbr2 <- gcd(Base^k+incre, original)
        insertBigFactor(pstFactors, &Nbr2, TYP_TABLE);
        // PERF why does this copy to tmp?
        BigInteger tmp;
        CopyBigInt(&tmp, BigOriginal);
        (void)BigIntDivide(&tmp, &Nbr2, &Nbr1);
        insertBigFactor(pstFactors, &Nbr1, TYP_TABLE);
        InsertAurifFactors(pstFactors, BigBase, k, increment);
      }
    }
    k++;
  }
}

static bool ProcessExponent(struct sFactorsInternal *pstFactors, const BigInteger *numToFactor,
  int Exponent)
{
  static BigInteger NFp1;
  static BigInteger nthRoot;
  static BigInteger rootN1;
  static BigInteger rootN;
  static BigInteger nextroot;
  static BigInteger dif;
  int base;
  int pwr;
  computeRoot(numToFactor, &nthRoot, Exponent);
  // Test whether (nthroot ^ Exponent = NFp1) (mod 2^BITS_PER_GROUP)
  base = nthRoot.limbs[0].x;
  pwr = 1;
  for (int mask = 0x100000; mask > 0; mask /= 2)
  {
    pwr *= pwr;
    if ((Exponent & mask) != 0)
    {
      pwr *= base;
    }
  }
  for (int step = 0; step < 2; step++)
  {
    int delta = ((step == 1) ? 1 : -1);
    if (((pwr - numToFactor->limbs[0].x + delta) & MAX_INT_NBR) == 0)
    {
      CopyBigInt(&NFp1, numToFactor);
      addbigint(&NFp1, -delta);   // NFp1 <- NumberToFactor +/- 1
      for (;;)
      {
        (void)BigIntPowerIntExp(&nthRoot, Exponent - 1, &rootN1); // rootN1 <- nthRoot ^ (Exponent-1)
        (void)BigIntMultiply(&nthRoot, &rootN1, &rootN);  // rootN <- nthRoot ^ Exponent
        BigIntSubt(&NFp1, &rootN, &dif);            // dif <- NFp1 - rootN
        if (BigIntIsZero(&dif))
        { // Perfect power
          Cunningham(pstFactors, &nthRoot, Exponent, delta, numToFactor);
          return true;
        }
        addbigint(&dif, 1);                         // dif <- dif + 1
        BigInteger tmp;
        (void)BigIntDivide(&dif, &rootN1, &tmp);    // Temp1 <- dif / rootN1
        subtractdivide(&tmp, 0, Exponent);        // Temp1 <- Temp1 / Exponent
        BigIntAdd(&tmp, &nthRoot, &nextroot);     // nextroot <- Temp1 + nthRoot
        addbigint(&nextroot, -1);                   // nextroot <- nextroot - 1
        BigIntSubt(&nextroot, &nthRoot, &nthRoot);  // nthRoot <- nextroot - nthRoot
        if (nthRoot.sign == SIGN_POSITIVE)
        {
          break; // Not a perfect power
        }
        CopyBigInt(&nthRoot, &nextroot);
      }
    }
  }
  return false;
}

// Test whether rem2 is a perfect currentPrime(th) power mod currentPrime^2
static bool isPerfectPower(int rem, int currentPrime, const BigInteger *pRem2)
{
  unsigned int expon = (unsigned int)currentPrime;
  unsigned int mask = 0x100000U;
  bool powerStarted = false;
  if (currentPrime < 65536)
  {   // Use unsigned integers for all values because it is a lot faster.
    uint64_t rem2 = (uint64_t)pRem2->limbs[0].x;
    uint64_t base = (uint64_t)rem;
    uint64_t iSq = (uint64_t)currentPrime * (uint64_t)currentPrime;
    uint64_t currentPower = 1U;
    while (mask > 0U)
    {
      if (powerStarted)
      {
        currentPower = (currentPower * currentPower) % iSq;
      }
      if ((mask & expon) != 0U)
      {
        currentPower = (currentPower * base) % iSq;
        powerStarted = true;
      }
      mask >>= 1;
    }
    return currentPower == rem2;
  }
  uint64_t iSquared = (uint64_t)currentPrime * (uint64_t)currentPrime;
  longToBigInteger(&Temp4, iSquared);
  // Initialize current power.
  intToBigInteger(&Temp3, 1);
  while (mask > 0U)
  {
    if (powerStarted)
    {
      (void)BigIntMultiply(&Temp3, &Temp3, &Temp3);
      (void)BigIntRemainder(&Temp3, &Temp4, &Temp3);
    }
    if ((mask & expon) != 0U)
    {
      multint(&Temp3, &Temp3, rem);
      (void)BigIntRemainder(&Temp3, &Temp4, &Temp3);
      powerStarted = true;
    }
    mask >>= 1;
  }
  return BigIntEqual(&Temp3, pRem2);
}

int kk;
static void initProcessExponVector(const BigInteger* numToFactor, int numPrimes,
  int maxExpon)
{
  unsigned int currentPrime;
  unsigned int j;
  int modulus;
  // If n!=1 or n!=0 or n!=7 (mod 8), then b cannot be even.
  modulus = numToFactor->limbs[0].x & 7;
  if ((modulus == 0) || (modulus == 1) || (modulus == 7))
  {       // b can be even
    (void)memset(common.ecm.ProcessExpon, 0xFF, sizeof(common.ecm.ProcessExpon));
  }
  else
  {       // b cannot be even
    (void)memset(common.ecm.ProcessExpon, 0xAA, sizeof(common.ecm.ProcessExpon));
  }
  (void)memset(common.ecm.primes, 0xFF, sizeof(common.ecm.primes));
  for (currentPrime = 2; (currentPrime * currentPrime) < (unsigned int)numPrimes; currentPrime++)
  {       // Generation of primes using sieve of Eratosthenes.
    if ((common.ecm.primes[currentPrime >> 3] & (1U << (currentPrime & 7U))) != 0U)
    {     // Number i is prime.
      for (j = currentPrime * currentPrime; j < (unsigned int)numPrimes; j += currentPrime)
      {   // Mark multiple of i as composite.
        common.ecm.primes[j >> 3] &= ~(1U << (j & 7U));
      }
    }
  }
  // Let n = a^b +/- 1 (n = number to factor).
  // If -1<=n<=2 (mod p) does not hold, b cannot be multiple of p-1.
  // If -2<=n<=2 (mod p) does not hold, b cannot be multiple of (p-1)/2.
  for (currentPrime = 2U; currentPrime < (unsigned int)numPrimes; currentPrime++)
  {
    if ((common.ecm.primes[currentPrime >> 3] & (1U << (currentPrime & 7U))) == 0U)
    {    // currentPrime is not prime according to sieve.
      continue;
    }
         // If n+/-1 is multiple of p, then it must be multiple
         // of p^2, otherwise it cannot be a perfect power.
    bool isPower;
    int rem = getRemainder(numToFactor, currentPrime);
    uint64_t iSquared = (uint64_t)currentPrime * (uint64_t)currentPrime;
    BigInteger tmp;
    longToBigInteger(&tmp, iSquared);
    // Compute Temp2 as the remainder of nbrToFactor divided by (i*i).
    (void)BigIntRemainder(numToFactor, &tmp, &Temp2);
    isPower = isPerfectPower(rem, currentPrime, &Temp2);
    if (!isPower)
    {
      rem++;
      if (rem == (int)currentPrime)
      {
        rem = 0;
      }
      addbigint(&Temp2, 1);
      if (BigIntEqual(&tmp, &Temp2))
      {
        intToBigInteger(&Temp2, 0);
      }
      isPower = isPerfectPower(rem, currentPrime, &Temp2);
    }
    if (!isPower)
    {
      rem -= 2;
      if (rem < 0)
      {
        rem += (int)currentPrime;
      }
      addbigint(&Temp2, -2);
      if (Temp2.sign == SIGN_NEGATIVE)
      {
        BigIntAdd(&Temp2, &tmp, &Temp2);
      }
      isPower = isPerfectPower(rem, currentPrime, &Temp2);
    }
    if (!isPower)
    {  // If it is not a p-th power, the number is not a k*p-th power.
      for (j = currentPrime; j <= (unsigned int)maxExpon; j += currentPrime)
      {
        common.ecm.ProcessExpon[j >> 3] &= ~(1U << (j & 7U));
      }
    }
  }
}

static void PowerPM1Check(struct sFactorsInternal *pstFactors, const BigInteger *numToFactor)
{
  unsigned int Exponent = 0U;
  int mod9 = getRemainder(numToFactor, 9);
  int maxExpon = numToFactor->nbrLimbs * BITS_PER_GROUP;
  int numPrimes = (2 * maxExpon) + 3;
  double logar = logBigNbr(numToFactor);
  // 332199 = logarithm base 2 of max number supported = 10^100000.
  // Let n = a^b +/- 1 (n = number to factor).
  initProcessExponVector(numToFactor, numPrimes, maxExpon);
  for (unsigned int j = 2U; j < 100U; j++)
  {
    double u = logar / log(j) + .000005;
    Exponent = (unsigned int)floor(u);
    if ((u - (double)Exponent) > .00001)
    {
      continue;
    }
    if (((Exponent % 3U) == 0U) && (mod9 > 2) && (mod9 < 7))
    {
      continue;
    }
    if ((common.ecm.ProcessExpon[Exponent >> 3] & (1U << (Exponent & 7U))) == 0U)
    {
      continue;
    }
    if (ProcessExponent(pstFactors, numToFactor, (int)Exponent))
    {
      return;
    }
  }
  for (; Exponent >= 2U; Exponent--)
  {
    if (((Exponent % 3U) == 0U) && (mod9 > 2) && (mod9 < 7))
    {
      continue;
    }
    if (!((unsigned int)common.ecm.ProcessExpon[Exponent >> 3] & (1U << (Exponent & 7U))))
    {
      continue;
    }
    if (ProcessExponent(pstFactors, numToFactor, (int)Exponent))
    {
      return;
    }
  }
}

// Perform Lehman algorithm
#define NBR_PRIMES_QUADR_SIEVE  17
static void Lehman(const BigInteger *nbr, int multiplier, BigInteger *factor)
{
  // In the following arrays, the bit n is set if n is a square mod p.
  // Bits 31-0
  static constexpr unsigned int bitsSqrLow[NBR_PRIMES_QUADR_SIEVE] =
  {
    0x00000003U, // squares mod 3
    0x00000013U, // squares mod 5
    0x00000017U, // squares mod 7
    0x0000023BU, // squares mod 11
    0x0000161BU, // squares mod 13
    0x0001A317U, // squares mod 17
    0x00030AF3U, // squares mod 19
    0x0005335FU, // squares mod 23
    0x13D122F3U, // squares mod 29
    0x121D47B7U, // squares mod 31
    0x5E211E9BU, // squares mod 37
    0x82B50737U, // squares mod 41
    0x83A3EE53U, // squares mod 43
    0x1B2753DFU, // squares mod 47
    0x3303AED3U, // squares mod 53
    0x3E7B92BBU, // squares mod 59
    0x0A59F23BU, // squares mod 61
  };
  // Bits 63-32
  static constexpr unsigned int bitsSqrHigh[NBR_PRIMES_QUADR_SIEVE] =
  {
    0x00000000U, // squares mod 3
    0x00000000U, // squares mod 5
    0x00000000U, // squares mod 7
    0x00000000U, // squares mod 11
    0x00000000U, // squares mod 13
    0x00000000U, // squares mod 17
    0x00000000U, // squares mod 19
    0x00000000U, // squares mod 23
    0x00000000U, // squares mod 29
    0x00000000U, // squares mod 31
    0x00000016U, // squares mod 37
    0x000001B3U, // squares mod 41
    0x00000358U, // squares mod 43
    0x00000435U, // squares mod 47
    0x0012DD70U, // squares mod 53
    0x022B6218U, // squares mod 59
    0x1713E694U, // squares mod 61
  };
  static constexpr int primes[NBR_PRIMES_QUADR_SIEVE] =
      { 3, 5, 7, 11, 13, 17, 19, 23, 29, 31,
        37, 41, 43, 47, 53, 59, 61 };
  int nbrs[NBR_PRIMES_QUADR_SIEVE];
  int diffs[NBR_PRIMES_QUADR_SIEVE];
  int i;
  int m;
  int r;
  int nbrLimbs;
  int nbrIterations;
  BigInteger sqrRoot;
  BigInteger nextroot;
  BigInteger a;
  BigInteger c;
  BigInteger sqr;
  BigInteger val;
  if ((nbr->limbs[0].x & 1) == 0)
  { // nbr is even
    r = 0;
    m = 1;
  }
  else
  {
    if ((multiplier % 2) == 0)
    { // multiplier is even
      r = 1;
      m = 2;
    }
    else
    { // multiplier is odd
      r = (multiplier + nbr->limbs[0].x) & 3;
      m = 4;
    }
  }
  intToBigInteger(&sqr, multiplier * 4);
  (void)BigIntMultiply(&sqr, nbr, &sqr);
  squareRoot(sqr.limbs, sqrRoot.limbs, sqr.nbrLimbs, &sqrRoot.nbrLimbs);
  sqrRoot.sign = SIGN_POSITIVE;
  CopyBigInt(&a, &sqrRoot);
  for (;;)
  {
    if ((a.limbs[0].x & (m-1)) == r)
    {
      (void)BigIntMultiply(&a, &a, &nextroot);
      BigIntSubt(&nextroot, &sqr, &nextroot);
      if (nextroot.sign == SIGN_POSITIVE)
      {
        break;
      }
    }
    addbigint(&a, 1);                         // a <- a + 1
  }
  (void)BigIntMultiply(&a, &a, &nextroot);
  BigIntSubt(&nextroot, &sqr, &c);
  for (i = 0; i < NBR_PRIMES_QUADR_SIEVE; i++)
  {
    int pr = primes[i];
    nbrs[i] = getRemainder(&c, pr);    // nbrs[i] <- c % primes[i]
    diffs[i] = m * ((getRemainder(&a, pr) * 2) + m) % pr;
  }
  nbrLimbs = factor->nbrLimbs;
  if (nbrLimbs > 10)
  {
    nbrIterations = 10000;
  }
  else
  {
    nbrIterations = 1000 * factor->nbrLimbs;
  }
  for (int iterNbr = 0; iterNbr < nbrIterations; iterNbr++)
  {
    for (i = 0; i < NBR_PRIMES_QUADR_SIEVE; i++)
    {
      unsigned int shiftBits = (unsigned int)nbrs[i];
      unsigned int bitsSqr;
      unsigned int bitsToShift;
      if (shiftBits < 32U)
      {
        bitsSqr = bitsSqrLow[i];
        bitsToShift = shiftBits;
      }
      else
      {
        bitsSqr = bitsSqrHigh[i];
        bitsToShift = shiftBits - 32U;
      }
      if (((bitsSqr >> bitsToShift) & 0x01U) == 0U)
      { // Not a perfect square
        break;
      }
    }
    if (i == NBR_PRIMES_QUADR_SIEVE)
    { // Test for perfect square
      intToBigInteger(&c, m * iterNbr);           // c <- m * j
      BigIntAdd(&a, &c, &val);
      (void)BigIntMultiply(&val, &val, &c); // c <- val * val
      BigIntSubt(&c, &sqr, &c);             // c <- val * val - sqr
      squareRoot(c.limbs, sqrRoot.limbs, c.nbrLimbs, &sqrRoot.nbrLimbs);
      sqrRoot.sign = SIGN_POSITIVE;         // sqrRoot <- sqrt(c)
      BigIntAdd(&sqrRoot, &val, &sqrRoot);
      BigIntGcd(&sqrRoot, nbr, &c);         // Get GCD(sqrRoot + val, nbr)
      if (c.nbrLimbs > 1)
      {    // Non-trivial factor has been found.
        CopyBigInt(factor, &c);
        return;
      }
    }
    for (i = 0; i < NBR_PRIMES_QUADR_SIEVE; i++)
    {
      nbrs[i] = (nbrs[i] + diffs[i]) % primes[i];
      diffs[i] = (diffs[i] + (2 * m * m)) % primes[i];
    }
  }
  intToBigInteger(factor, 1);   // Factor not found.
}

static bool isOne(const limb* nbr, int length)
{
  if (nbr->x != 1)
  {
    return false;
  }
  for (int ctr = 1; ctr < length; ctr++)
  {
    if ((nbr + ctr)->x != 0)
    {
      return false;
    }
  }
  return true;
}

static void performFactorization(const BigInteger *numToFactor,
                                 const struct sFactorsInternal *pstFactors)
{
  (void)pstFactors;     // Ignore parameter.
  int NumberLengthBytes;
  static BigInteger potentialFactor;
  common.ecm.fieldTX = common.ecm.TX;
  common.ecm.fieldTZ = common.ecm.TZ;
  common.ecm.fieldUX = common.ecm.UX;
  common.ecm.fieldUZ = common.ecm.UZ;
  //  int Prob
  //  BigInteger NN

  common.ecm.fieldAA = common.ecm.AA;
  NumberLength = numToFactor->nbrLimbs;
  NumberLengthBytes = NumberLength * (int)sizeof(limb);
  (void)memcpy(TestNbr, numToFactor->limbs, NumberLengthBytes);
  GetMontgomeryParms(NumberLength);
  (void)memset(common.ecm.M, 0, NumberLengthBytes);
  (void)memset(common.ecm.DX, 0, NumberLengthBytes);
  (void)memset(common.ecm.DZ, 0, NumberLengthBytes);
  (void)memset(common.ecm.W3, 0, NumberLengthBytes);
  (void)memset(common.ecm.W4, 0, NumberLengthBytes);
  (void)memset(common.ecm.GD, 0, NumberLengthBytes);
  EC--;
  foundByLehman = false;
  do
  {
    enum eEcmResult ecmResp;
    // Try to factor BigInteger N using Lehman algorithm. Result in potentialFactor.
    Lehman(numToFactor, EC % 50000000, &potentialFactor);
    if (potentialFactor.nbrLimbs > 1)
    {                // Factor found.
      int lenBytes;
      (void)memcpy(common.ecm.GD, potentialFactor.limbs, NumberLengthBytes);
      lenBytes = (NumberLength - potentialFactor.nbrLimbs) * (int)sizeof(limb);
      (void)memset(&common.ecm.GD[potentialFactor.nbrLimbs], 0, lenBytes);
      foundByLehman = true;
      break;
    }
    // XXX looks like just some stray code? -tom7
    // tofactor is not initialized.
    // Lehman(&tofactor, EC % 50000000, &potentialFactor);

    BigIntGcd(numToFactor, &potentialFactor, &potentialFactor);
    if ((potentialFactor.nbrLimbs > 1) &&
      !BigIntEqual(&potentialFactor, numToFactor))
    {                // Factor found.
      int lenBytes;
      (void)memcpy(common.ecm.GD, potentialFactor.limbs, NumberLengthBytes);
      lenBytes = (NumberLength - potentialFactor.nbrLimbs) * (int)sizeof(limb);
      (void)memset(&common.ecm.GD[potentialFactor.nbrLimbs], 0, lenBytes);
      foundByLehman = true;
      break;
    }
    ecmResp = ecmCurve(&EC, &NextEC);
    if (ecmResp == CHANGE_TO_SIQS)
    {    // Perform SIQS
      // If number is a perfect power, SIQS does not work.
      // So the next code detects whether the number is a perfect power.
      int expon = PowerCheck(numToFactor, &potentialFactor);
      if (expon > 1)
      {
        int lenBytes;
        (void)memcpy(common.ecm.GD, potentialFactor.limbs, NumberLengthBytes);
        lenBytes = (NumberLength - potentialFactor.nbrLimbs) * (int)sizeof(limb);
        (void)memset(&common.ecm.GD[potentialFactor.nbrLimbs], 0, lenBytes);
        foundByLehman = false;
        break;
      }
      FactoringSIQS(TestNbr, common.ecm.GD);

      break;
    }
    if (ecmResp == FACTOR_FOUND)
    {
      break;
    }
  } while ((memcmp(common.ecm.GD, TestNbr, NumberLengthBytes) == 0) ||
           isOne(common.ecm.GD, NumberLength));
}

static void SortFactors(struct sFactorsInternal *pstFactors)
{
  int factorNumber;
  int ctr;
  int nbrFactors = pstFactors->multiplicity;
  struct sFactorsInternal *pstCurFactor = pstFactors + 1;
  struct sFactorsInternal stTempFactor;
  int *ptrNewFactor;
  struct sFactorsInternal *pstNewFactor;
  // I think this starts at 1 because the 0th entry is the
  // residual (maybe composite?) number. Its multiplicity is the
  // count of factors in the array.
  for (factorNumber = 1; factorNumber < nbrFactors; factorNumber++)
  {
    pstNewFactor = pstCurFactor + 1;
    for (int factorNumber2 = factorNumber + 1; factorNumber2 <= nbrFactors; factorNumber2++)
    {
      const int *ptrFactor = pstCurFactor->ptrFactor;
      const int *ptrFactor2 = pstNewFactor->ptrFactor;
      if (*ptrFactor < *ptrFactor2)
      {     // Factors already in correct order.
        pstNewFactor++;
        continue;
      }
      if (*ptrFactor == *ptrFactor2)
      {
        for (ctr = *ptrFactor; ctr > 1; ctr--)
        {
          if (*(ptrFactor + ctr) != *(ptrFactor2 + ctr))
          {
            break;
          }
        }
        if (*(ptrFactor + ctr) < *(ptrFactor2 + ctr))
        {     // Factors already in correct order.
          pstNewFactor++;
          continue;
        }
        if (*(ptrFactor + ctr) == *(ptrFactor2 + ctr))
        {     // Factors are the same.
          pstCurFactor->multiplicity += pstNewFactor->multiplicity;
          ctr = pstFactors->multiplicity - factorNumber2;
          if (ctr > 0)
          {
            int lenBytes = ctr * (int)sizeof(struct sFactorsInternal);
            (void)memmove(pstNewFactor, pstNewFactor + 1, lenBytes);
          }
          pstFactors->multiplicity--;   // Indicate one less known factor.
          nbrFactors--;
          pstNewFactor++;
          continue;
        }
      }
      // Exchange both factors.
      (void)memcpy(&stTempFactor, pstCurFactor, sizeof(struct sFactorsInternal));
      (void)memcpy(pstCurFactor, pstNewFactor, sizeof(struct sFactorsInternal));
      (void)memcpy(pstNewFactor, &stTempFactor, sizeof(struct sFactorsInternal));
      pstNewFactor++;
    }
    pstCurFactor++;
  }
  // Find location for new factors.
  ptrNewFactor = NULL;
  pstCurFactor = pstFactors + 1;
  for (factorNumber = 1; factorNumber <= pstFactors->multiplicity; factorNumber++)
  {
    int *ptrPotentialNewFactor = pstCurFactor->ptrFactor + *(pstCurFactor->ptrFactor) + 1;
    if (ptrPotentialNewFactor > ptrNewFactor)
    {
      ptrNewFactor = ptrPotentialNewFactor;
    }
    pstCurFactor++;
  }
  pstFactors->ptrFactor = ptrNewFactor;
}

// Insert new factor found into factor array. This factor array must be sorted.
static void insertIntFactor(struct sFactorsInternal *pstFactors,
                            struct sFactorsInternal *pstFactorDividend,
                            int divisor, int expon, const BigInteger *cofactor)
{
  struct sFactorsInternal *pstCurFactor;
  int multiplicity;
  int factorNumber;
  limb *ptrFactor = (limb *)pstFactorDividend->ptrFactor;
  int nbrLimbs = ptrFactor->x;
  int *ptrValue;
  pstFactorDividend->upperBound = divisor;
  // Divide number by factor just found.
  if (cofactor == NULL)
  {        // Find cofactor.
    DivBigNbrByInt(ptrFactor + 1, divisor, ptrFactor + 1, nbrLimbs);
    if ((ptrFactor + nbrLimbs)->x == 0)
    {
      ptrFactor->x--;
    }
  }
  else
  {        // Cofactor given as a parameter.
    NumberLength = cofactor->nbrLimbs;
    BigInteger2IntArray((int *)ptrFactor, cofactor);
  }
  // Check whether prime is already in factor list.
  pstCurFactor = pstFactors+1;
  for (factorNumber = 1; factorNumber <= pstFactors->multiplicity; factorNumber++)
  {
    ptrValue = pstCurFactor->ptrFactor;  // Point to factor in factor array.
    if ((*ptrValue == 1) && (*(ptrValue+1) == divisor))
    {  // Prime already found: increment multiplicity and go out.
      pstCurFactor->multiplicity += pstFactorDividend->multiplicity * expon;
      ptrValue = pstFactorDividend->ptrFactor;
      if ((*ptrValue == 1) && (*(ptrValue + 1) == 1))
      {    // Dividend is 1 now, so discard it.
        *pstFactorDividend = *(pstFactors + pstFactors->multiplicity);
        pstFactors->multiplicity--;
      }
      SortFactors(pstFactors);
      return;
    }
    if ((*ptrValue > 1) || (*(ptrValue + 1) > divisor))
    {   // Factor in factor list is greater than factor to insert. Exit loop.
      break;
    }
    pstCurFactor++;
  }
  pstFactors->multiplicity++; // Indicate new known factor.
  multiplicity = pstFactorDividend->multiplicity;
  // Move all elements.
  ptrValue = pstFactorDividend->ptrFactor;
  if (pstFactors->multiplicity > factorNumber)
  {
    int lenBytes = (pstFactors->multiplicity - factorNumber) * (int)sizeof(struct sFactorsInternal);
    (void)memmove(pstCurFactor + 1, pstCurFactor, lenBytes);
  }
  if ((*ptrValue == 1) && (*(ptrValue + 1) == 1))
  {
    pstCurFactor = pstFactorDividend;
  }
  else
  {
    ptrValue = pstFactors->ptrFactor;
    pstCurFactor->ptrFactor = ptrValue;
    pstCurFactor->multiplicity = multiplicity * expon;
    pstCurFactor->type = 0;
    pstFactors->ptrFactor += 2;  // Next free memory.
  }
  pstCurFactor->upperBound = 0;
  *ptrValue = 1;  // Number of limbs.
  *(ptrValue + 1) = divisor;
  SortFactors(pstFactors);
}

// Insert new factor found into factor array. This factor array must be sorted.
// The divisor must be also sorted.
static void insertBigFactor(struct sFactorsInternal *pstFactors,
                            const BigInteger *divisor,
                            int type) {
  int typeFactor = type;
  struct sFactorsInternal *pstCurFactor;
  int lastFactorNumber = pstFactors->multiplicity;
  struct sFactorsInternal *pstNewFactor = pstFactors + lastFactorNumber + 1;
  int *ptrNewFactorLimbs = pstFactors->ptrFactor;
  pstCurFactor = pstFactors + 1;
  for (int factorNumber = 1; factorNumber <= lastFactorNumber; factorNumber++)
  {     // For each known factor...
    int *ptrFactor = pstCurFactor->ptrFactor;
    NumberLength = *ptrFactor;
    IntArray2BigInteger(ptrFactor, &Temp2);    // Convert known factor to Big Integer.
    BigIntGcd(divisor, &Temp2, &Temp3);         // Temp3 is the GCD between known factor and divisor.
    if ((Temp3.nbrLimbs == 1) && (Temp3.limbs[0].x < 2))
    {                                           // divisor is not a new factor (GCD = 0 or 1).
      pstCurFactor++;
      continue;
    }
    if (TestBigNbrEqual(&Temp2, &Temp3))
    {                                           // GCD is equal to known factor.
      pstCurFactor++;
      continue;
    }
    // At this moment both GCD and known factor / GCD are new known factors. Replace the known factor by
    // known factor / GCD and generate a new known factor entry.
    NumberLength = Temp3.nbrLimbs;
    BigInteger2IntArray(ptrNewFactorLimbs, &Temp3);      // Append new known factor.
    (void)BigIntDivide(&Temp2, &Temp3, &Temp4);          // Divide by this factor.
    NumberLength = Temp4.nbrLimbs;
    BigInteger2IntArray(ptrFactor, &Temp4);              // Overwrite old known factor.
    pstNewFactor->multiplicity = pstCurFactor->multiplicity;
    pstNewFactor->ptrFactor = ptrNewFactorLimbs;
    pstNewFactor->upperBound = pstCurFactor->upperBound;
    if (typeFactor < 50000000)
    {          // Factor found using ECM.
      pstNewFactor->type = TYP_EC + EC;
      typeFactor = pstCurFactor->type / 50000000 * 50000000;
      if (typeFactor == 0)
      {
        pstCurFactor->type = TYP_DIVISION + EC;
      }
      else
      {
        pstCurFactor->type = typeFactor + EC;
      }
    }
    else
    {          // Found otherwise.
      pstNewFactor->type = typeFactor;
    }
    pstNewFactor++;
    pstFactors->multiplicity++;
    ptrNewFactorLimbs += 1 + Temp3.nbrLimbs;
    pstCurFactor++;
  }
  // Sort factors in ascending order. If two factors are equal, coalesce them.
  // Divide number by factor just found.
  SortFactors(pstFactors);
}

// Return: 0 = No factors found.
//         1 = Factors found.
// Use: Xaux for square root of -1.
//      Zaux for square root of 1.
static int factorCarmichael(BigInteger *pValue, struct sFactorsInternal *pstFactors)
{
  int randomBase = 0;
  bool factorsFound = false;
  int nbrLimbsQ;
  int ctr;
  int nbrLimbs = pValue->nbrLimbs;
  bool sqrtOneFound = false;
  bool sqrtMinusOneFound = false;
  int Aux1Len;
  int lenBytes;
  limb *pValueLimbs = pValue->limbs;
  (pValueLimbs + nbrLimbs)->x = 0;
  lenBytes = (nbrLimbs + 1) * (int)sizeof(limb);

  int valueQ[MAX_LEN];
  (void)memcpy(valueQ, pValueLimbs, lenBytes);
  nbrLimbsQ = nbrLimbs;
  valueQ[0]--;                     // q = p - 1 (p is odd, so there is no carry).
  lenBytes = (nbrLimbsQ + 1) * (int)sizeof(valueQ[0]);
  (void)memcpy(common.ecm.Aux1, valueQ, lenBytes);
  Aux1Len = nbrLimbs;
  DivideBigNbrByMaxPowerOf2(&ctr, common.ecm.Aux1, &Aux1Len);
  lenBytes = nbrLimbs * (int)sizeof(limb);
  (void)memcpy(TestNbr, pValueLimbs, lenBytes);
  TestNbr[nbrLimbs].x = 0;
  GetMontgomeryParms(nbrLimbs);
  for (int countdown = 20; countdown > 0; countdown--)
  {
    uint64_t ui64Random;
    NumberLength = nbrLimbs;
    ui64Random = (((uint64_t)randomBase * 89547121U) + 1762281733U) & MAX_INT_NBR_U;
    randomBase = (int)ui64Random;
    modPowBaseInt(randomBase, common.ecm.Aux1, Aux1Len, common.ecm.Aux2); // Aux2 = base^Aux1.
                                                 // If Mult1 = 1 or Mult1 = TestNbr-1, then try next base.
    if (checkOne(common.ecm.Aux2, nbrLimbs) || checkMinusOne(common.ecm.Aux2, nbrLimbs))
    {
      continue;    // This base cannot find a factor. Try another one.
    }
    for (int i = 0; i < ctr; i++)
    {              // Loop that squares number.
      modmult(common.ecm.Aux2, common.ecm.Aux2, common.ecm.Aux3);
      if (checkOne(common.ecm.Aux3, nbrLimbs) != 0)
      {            // Non-trivial square root of 1 found.
        if (!sqrtOneFound)
        {          // Save it to perform GCD later.
          lenBytes = nbrLimbs * (int)sizeof(limb);
          (void)memcpy(common.ecm.Zaux, common.ecm.Aux2, lenBytes);
          sqrtOneFound = true;
        }
        else
        {          // Try to find non-trivial factor by doing GCD.
          SubtBigNbrMod(common.ecm.Aux2, common.ecm.Zaux, common.ecm.Aux4);
          UncompressLimbsBigInteger(common.ecm.Aux4, &Temp2);
          BigIntGcd(pValue, &Temp2, &Temp4);
          lenBytes = NumberLength * (int)sizeof(limb);
          if (((Temp4.nbrLimbs != 1) || (Temp4.limbs[0].x > 1)) &&
            ((Temp4.nbrLimbs != NumberLength) ||
              memcmp(pValue->limbs, Temp4.limbs, lenBytes)))
          {          // Non-trivial factor found.
            insertBigFactor(pstFactors, &Temp4, TYP_RABIN);
            factorsFound = true;
          }
        }
                   // Try to find non-trivial factor by doing GCD.
        NumberLength = nbrLimbs;
        AddBigNbrMod(common.ecm.Aux2, MontgomeryMultR1, common.ecm.Aux4);
        UncompressLimbsBigInteger(common.ecm.Aux4, &Temp2);
        BigIntGcd(pValue, &Temp2, &Temp4);
        lenBytes = NumberLength * (int)sizeof(limb);
        if (((Temp4.nbrLimbs != 1) || (Temp4.limbs[0].x > 1)) &&
          ((Temp4.nbrLimbs != NumberLength) ||
            memcmp(pValue->limbs, Temp4.limbs, lenBytes)))
        {          // Non-trivial factor found.
          insertBigFactor(pstFactors, &Temp4, TYP_RABIN);
          factorsFound = true;
        }
        break;  // Find more factors.
      }
      if (checkMinusOne(common.ecm.Aux3, nbrLimbs) != 0)
      {            // Square root of 1 found.
        if (!sqrtMinusOneFound)
        {          // Save it to perform GCD later.
          lenBytes = nbrLimbs * (int)sizeof(limb);
          (void)memcpy(common.ecm.Xaux, common.ecm.Aux2, lenBytes);
          sqrtOneFound = true;
        }
        else
        {          // Try to find non-trivial factor by doing GCD.
          SubtBigNbrMod(common.ecm.Aux3, common.ecm.Xaux, common.ecm.Aux4);
          UncompressLimbsBigInteger(common.ecm.Aux4, &Temp2);
          BigIntGcd(pValue, &Temp2, &Temp4);
          lenBytes = NumberLength * (int)sizeof(limb);
          if (((Temp4.nbrLimbs != 1) || (Temp4.limbs[0].x > 1)) &&
            ((Temp4.nbrLimbs != NumberLength) ||
              memcmp(pValue->limbs, Temp4.limbs, lenBytes)))
          {          // Non-trivial factor found.
            insertBigFactor(pstFactors, &Temp4, TYP_RABIN);
            factorsFound = true;
          }
        }
        break;  // Find more factors.
      }
      lenBytes = nbrLimbs * (int)sizeof(limb);
      (void)memcpy(common.ecm.Aux2, common.ecm.Aux3, lenBytes);
    }
  }
  return factorsFound;
}

static void factorSmallInt(int intToFactor, int* factors, struct sFactorsInternal* pstFactors)
{
  int toFactor = intToFactor;
  int factorsFound = 0;
  int primeFactor;
  int multiplicity;
  struct sFactorsInternal* ptrFactor = pstFactors + 1;
  int* ptrFactorLimbs = factors;
  if (toFactor <= 3)
  {     // Only one factor.
    ptrFactor->ptrFactor = ptrFactorLimbs;
    ptrFactor->multiplicity = 1;
    ptrFactor->type = 0;
    ptrFactor->upperBound = 0;
    *ptrFactorLimbs = 1;
    ptrFactorLimbs++;
    *ptrFactorLimbs = toFactor;
    pstFactors->multiplicity = 1;
    return;
  }
  // Divide by 2 and 3.
  for (primeFactor = 2; primeFactor <= 3; primeFactor++)
  {
    multiplicity = 0;
    while ((toFactor % primeFactor) == 0)
    {
      toFactor /= primeFactor;
      multiplicity++;
    }
    if (multiplicity > 0)
    {
      factorsFound++;
      ptrFactor->ptrFactor = ptrFactorLimbs;
      ptrFactor->multiplicity = multiplicity;
      ptrFactor->type = 0;
      ptrFactor->upperBound = 0;
      *ptrFactorLimbs = 1;
      ptrFactorLimbs++;
      *ptrFactorLimbs = primeFactor;
      ptrFactorLimbs++;
      ptrFactor++;
    }
  }
  for (primeFactor = 5;
    (unsigned int)primeFactor * (unsigned int)primeFactor <= (unsigned int)toFactor;
    primeFactor += 2)
  {
    if ((primeFactor % 3) == 0)
    {
      continue;
    }
    multiplicity = 0;
    while ((toFactor % primeFactor) == 0)
    {
      toFactor /= primeFactor;
      multiplicity++;
    }
    if (multiplicity > 0)
    {
      factorsFound++;
      ptrFactor->ptrFactor = ptrFactorLimbs;
      ptrFactor->multiplicity = multiplicity;
      ptrFactor->type = 0;
      ptrFactor->upperBound = 0;
      *ptrFactorLimbs = 1;
      ptrFactorLimbs++;
      *ptrFactorLimbs = primeFactor;
      ptrFactorLimbs++;
      ptrFactor++;
    }
  }
  if (toFactor > 1)
  {
    factorsFound++;
    ptrFactor->ptrFactor = ptrFactorLimbs;
    ptrFactor->multiplicity = 1;
    ptrFactor->type = 0;
    ptrFactor->upperBound = 0;
    *ptrFactorLimbs = 1;
    ptrFactorLimbs++;
    *ptrFactorLimbs = toFactor;
  }
  pstFactors->multiplicity = factorsFound;
}

static void InternalFactor(
    const BigInteger* toFactor, const int* number, int* factors, struct
    sFactorsInternal* pstFactors) {
  factorExt(toFactor, number, factors, pstFactors);
}

std::unique_ptr<Factors> Factor(const BigInteger *toFactor) {
  auto ret = std::make_unique<Factors>();

  ret->storage.resize(20000);

  NumberLength = toFactor->nbrLimbs;
  BigInteger2IntArray(nbrToFactor, toFactor);

  sFactorsInternal astFactorsMod[MAX_FACTORS];
  InternalFactor(toFactor, nbrToFactor, ret->storage.data(), astFactorsMod);

  sFactorsInternal *hdr = &astFactorsMod[0];
  // convert astfactorsmod to factorz
  int num_factors = hdr->multiplicity;
  ret->product.resize(num_factors);

  for (int i = 0; i < num_factors; i++) {
    // header is in slot 0
    int *ptr = astFactorsMod[i + 1].ptrFactor;
    int mult = astFactorsMod[i + 1].multiplicity;
    // expecting these to point into storage
    assert(ptr >= ret->storage.data() &&
           ptr < (ret->storage.data() + ret->storage.size()));
    ret->product[i].array = ptr;
    ret->product[i].multiplicity = mult;
  }

  return ret;
}



// pstFactors -> ptrFactor points to end of factors.
// pstFactors -> multiplicity indicates the number of different factors.
static void factorExt(const BigInteger *toFactor, const int *number,
                      int *factors, struct sFactorsInternal *pstFactors) {
  struct sFactorsInternal *pstCurFactor;
  int expon;
  int remainder;
  int nbrLimbs;
  int ctr;
  const int *ptrFactor;
  int dividend;
  int result;
  int factorNbr;

  CopyBigInt(&tofactor, toFactor);
  initializeSmallPrimes(smallPrimes);
  if (toFactor->nbrLimbs == 1)
  {
    factorSmallInt(toFactor->limbs[0].x, factors, pstFactors);
    return;
  }
  NextEC = -1;
  EC = 1;
  NumberLength = toFactor->nbrLimbs;
  pstCurFactor = pstFactors + 1;

  {
    // No factors known.
    int lenBytes = (1 + *number) * (int)sizeof(int);
    (void)memcpy(factors, number, lenBytes);
    pstFactors->multiplicity = 1;
    pstFactors->ptrFactor = factors + 1 + *factors;
    pstFactors->upperBound = 0;
    pstCurFactor->multiplicity = 1;
    pstCurFactor->ptrFactor = factors;
    pstCurFactor->upperBound = 2;
  }

  // (used to restore known factors here)

  if (toFactor->nbrLimbs > 1)
  {
    PowerPM1Check(pstFactors, toFactor);
  }
  pstCurFactor = pstFactors;
  factorNbr = 0;
  while (factorNbr < pstFactors->multiplicity)
  {
    factorNbr++;
    int upperBoundIndex;
    int upperBound;
    bool restartFactoring = false;
    pstCurFactor++;
    upperBound = pstCurFactor->upperBound;
    // If number is prime, do not process it.
    if (upperBound == 0)
    {     // Factor is prime.
      continue;
    }
    // Get upperBoundIndex from upperBound.
    upperBoundIndex = 0;
    if (upperBound < 100000)
    {
      for (int delta = 8192; delta > 0; delta >>= 1)
      {
        int tempUpperBoundIndex = upperBoundIndex + delta;
        if (tempUpperBoundIndex >= SMALL_PRIMES_ARRLEN)
        {           // Index too large.
          continue;
        }
        if (upperBound == smallPrimes[tempUpperBoundIndex])
        {
          upperBoundIndex = tempUpperBoundIndex;
          break;
        }
        if (upperBound > smallPrimes[tempUpperBoundIndex])
        {
          upperBoundIndex = tempUpperBoundIndex;
        }
      }
    }
    ptrFactor = pstCurFactor->ptrFactor;
    nbrLimbs = *ptrFactor;
    NumberLength = *pstCurFactor->ptrFactor;
    IntArray2BigInteger(pstCurFactor->ptrFactor, &power);
    NumberLength = power.nbrLimbs;

    expon = PowerCheck(&power, &prime);
    if (expon > 1)
    {
      NumberLength = prime.nbrLimbs;
      BigInteger2IntArray(pstCurFactor->ptrFactor, &prime);
      pstCurFactor->multiplicity *= expon;
      SortFactors(pstFactors);
      factorNbr = 0;             // Factor order has been changed. Restart factorization.
      pstCurFactor = pstFactors;
      continue;
    }
    while ((upperBound < 100000) && (nbrLimbs > 1))
    {        // Number has at least 2 limbs: Trial division by small numbers.
      if (pstCurFactor->upperBound != 0)
      {            // Factor found.
        ptrFactor = pstCurFactor->ptrFactor;
        remainder = RemDivBigNbrByInt((const limb *)(ptrFactor + 1), upperBound, nbrLimbs);
        if (remainder == 0)
        {
          // Small factor found. Find the exponent.
          int deltaIndex = 1;
          int exponent = 1;
          int index = 0;
          CopyBigInt(&common.trialDiv.cofactor, &prime);
          subtractdivide(&common.trialDiv.cofactor, 0, upperBound);
          intToBigInteger(&common.trialDiv.power[0], upperBound);
          for (;;)
          {      // Test whether the cofactor is multiple of power.
            (void)BigIntDivide(&common.trialDiv.cofactor, &common.trialDiv.power[index], &common.trialDiv.quotient);
            (void)BigIntMultiply(&common.trialDiv.quotient, &common.trialDiv.power[index], &common.trialDiv.temp);
            if (!BigIntEqual(&common.trialDiv.temp, &common.trialDiv.cofactor))
            {    // Not a multiple, so exit loop.
              break;
            }
            CopyBigInt(&common.trialDiv.cofactor, &common.trialDiv.quotient);
            (void)BigIntMultiply(&common.trialDiv.power[index], &common.trialDiv.power[index], &common.trialDiv.power[index + 1]);
            exponent += deltaIndex;
            deltaIndex <<= 1;
            index++;
          }
          index--;
          for (; index >= 0; index--)
          {
            deltaIndex >>= 1;
            (void)BigIntDivide(&common.trialDiv.cofactor, &common.trialDiv.power[index], &common.trialDiv.quotient);
            (void)BigIntMultiply(&common.trialDiv.quotient, &common.trialDiv.power[index], &common.trialDiv.temp);
            if (BigIntEqual(&common.trialDiv.temp, &common.trialDiv.cofactor))
            {    // It is a multiple.
              CopyBigInt(&common.trialDiv.cofactor, &common.trialDiv.quotient);
              exponent += deltaIndex;
            }
          }
          insertIntFactor(pstFactors, pstCurFactor, upperBound, exponent, &common.trialDiv.cofactor);
          restartFactoring = true;
        }
      }
      if (restartFactoring)
      {
        break;
      }
      upperBoundIndex++;
      upperBound = smallPrimes[upperBoundIndex];
    }
    if (restartFactoring)
    {
      factorNbr = 0;
      pstCurFactor = pstFactors;
      continue;
    }
    if (nbrLimbs == 1)
    {
      dividend = *(ptrFactor + 1);
      while ((upperBound < 65535) &&
              (((unsigned int)upperBound * (unsigned int)upperBound) <= (unsigned int)dividend))
      {              // Trial division by small numbers.
        if ((dividend % upperBound) == 0)
        {            // Factor found.
          insertIntFactor(pstFactors, pstCurFactor, upperBound, 1, NULL);
          restartFactoring = true;
          break;
        }
        upperBoundIndex++;
        upperBound = smallPrimes[upperBoundIndex];
      }
      if (restartFactoring)
      {
        factorNbr = 0;
        pstCurFactor = pstFactors;
        continue;
      }
      pstCurFactor->upperBound = 0;   // Number is prime.
      continue;
    }
    // No small factor. Check whether the number is prime or prime power.

    result = BpswPrimalityTest(&prime);

    if (result == 0)
    {   // Number is prime power.
      pstCurFactor->upperBound = 0;   // Indicate that number is prime.
      continue;                       // Check next factor.
    }
    if ((result > 1) &&
        // Number is 2-Fermat probable prime. Try to factor it.
       (factorCarmichael(&prime, pstFactors) != 0))
    {                               // Factors found.
      factorNbr--;                  // Test whether factor found is prime.
      pstCurFactor--;
      continue;
    }
    performFactorization(&prime, pstFactors);          // Factor number.
    // Check whether GD is not one. In this case we found a proper factor.
    for (ctr = 1; ctr < NumberLength; ctr++)
    {
      if (common.ecm.GD[ctr].x != 0)
      {
        break;
      }
    }
    if ((ctr != NumberLength) || (common.ecm.GD[0].x != 1))
    {
      int numLimbs;
      int lenBytes;
      BigInteger tmp;
      tmp.sign = SIGN_POSITIVE;
      numLimbs = NumberLength;
      while (numLimbs > 1)
      {
        if (common.ecm.GD[numLimbs-1].x != 0)
        {
          break;
        }
        numLimbs--;
      }
      lenBytes = numLimbs * (int)sizeof(limb);
      (void)memcpy(tmp.limbs, common.ecm.GD, lenBytes);
      tmp.nbrLimbs = numLimbs;
      if (foundByLehman)
      {
        insertBigFactor(pstFactors, &tmp, TYP_LEHMAN + EC);
      }
      else
      {
        insertBigFactor(pstFactors, &tmp, EC);
      }
      factorNbr = 0;
      pstCurFactor = pstFactors;
    }    // End if
  }      // End for
}
