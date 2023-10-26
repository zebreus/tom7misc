// This file is part of Alpertron Calculators.
//
// Copyright 2017-2021 Dario Alejandro Alpern
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

#include "quadmodll.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <cassert>
#include <memory>
#include <cstdio>

#include "bignbr.h"
#include "factor.h"
#include "modmult.h"
#include "bigconv.h"
#include "bignum/big.h"
#include "bignum/big-overloads.h"

#include "base/logging.h"

static constexpr bool VERBOSE = false;

static void setNbrLimbs(BigInteger* pBigNbr, int numlen) {
  pBigNbr->nbrLimbs = numlen;
  pBigNbr->sign = SIGN_POSITIVE;
  while (pBigNbr->nbrLimbs > 1) {
    if (pBigNbr->limbs[pBigNbr->nbrLimbs - 1].x != 0) {
      break;
    }
    pBigNbr->nbrLimbs--;
  }
}

// This used to be exposed to the caller for teach mode, but is
// now fully internal.
namespace {

struct QuadModLL {
  // Parallel arrays (same size as number of factors)
  std::vector<BigInt> Solution1;
  std::vector<BigInt> Solution2;
  std::vector<BigInt> Increment;
  std::vector<int> Exponents;
  BigInteger prime;

  QuadModLL() {
    // debugging if uninitialized
    intToBigInteger(&prime, 121212);
  }

  BigInt Discriminant;
  // Only used in CRT; could easily pass
  BigInt NN;

  // BigInteger Quadr;
  // BigInteger Linear;
  // BigInteger Const;
  BigInteger sqrRoot;
  BigInteger ValAOdd;
  BigInteger ValBOdd;
  BigInteger ValCOdd;
  BigInteger SqrtDisc;
  // BigInteger K1;
  // BigInteger L;
  BigInteger Q;
  BigInteger V;
  bool sol1Invalid = false;
  bool sol2Invalid = false;
  bool interesting_coverage = false;

  // These are probably all temporaries that could be locals.
  BigInteger Aux0;
  BigInteger Aux1;
  BigInteger Aux2;
  BigInteger Aux4, Aux5, Aux6;
  BigInteger Aux7, Aux8, Aux9, Aux10, Aux11;

  SolutionFn Solution;

  // Were globals NumberLength and TestNbr
  // int modulus_length = 0;
  limb TheModulus[MAX_LEN] = {};

  // Use Chinese remainder theorem to obtain the solutions.
  void PerformChineseRemainderTheorem(
      const BigInt &GcdAll,
      const std::vector<std::pair<BigInt, int>> &factors) {
    int T1;

    const int nbrFactors = factors.size();
    CHECK((int)Solution1.size() == nbrFactors);
    CHECK((int)Solution2.size() == nbrFactors);
    CHECK((int)Increment.size() == nbrFactors);
    CHECK((int)Exponents.size() == nbrFactors);
    CHECK(nbrFactors > 0);
    // Dynamically allocate temporary space. We need one per factor.
    std::vector<BigInt> Tmp(nbrFactors);

    do {
      // XXX Why can't this just be iteration 0 of the loop below?
      CHECK(!Solution1.empty());
      CHECK(!Solution2.empty());
      CHECK(!Increment.empty());
      CHECK(!Exponents.empty());

      Tmp[0] = Increment[0] * (Exponents[0] >> 1);
      // MultInt(&Tmp[0], &Increment[0], Exponents[0] / 2);
      if ((Exponents[0] & 1) != 0) {
        Tmp[0] += Solution2[0];
        // BigIntAdd(&Tmp[0], &Solution2[0], &Tmp[0]);
      } else {
        Tmp[0] += Solution1[0];
        // BigIntAdd(&Tmp[0], &Solution1[0], &Tmp[0]);
      }

      // PERF: Directly
      BigInt CurrentSolution = Tmp[0];
      // BigInteger currentSolution;
      // BigIntToBigInteger(Tmp[0], &currentSolution);

      // const sFactorz *pstFactor = &factors.product[0];
      const BigInt &Prime = factors[0].first;
      // IntArray2BigInteger(modulus_length, pstFactor->array, &prime);
      BigIntToBigInteger(Prime, &prime);

      BigInt Mult =
        BigInt::Pow(BigIntegerToBigInt(&prime), factors[0].second);

      for (T1 = 1; T1 < nbrFactors; T1++) {

        if (factors[T1].second == 0) {
          Tmp[T1] = BigInt(0);
          continue;
        }

        CHECK(T1 < (int)Solution1.size());
        CHECK(T1 < (int)Solution2.size());
        CHECK(T1 < (int)Increment.size());
        CHECK(T1 < (int)Exponents.size());

        int expon = Exponents[T1];
        Tmp[T1] = Increment[T1] * (expon >> 1);

        if ((expon & 1) != 0) {
          Tmp[T1] += Solution2[T1];
        } else {
          Tmp[T1] += Solution1[T1];
        }

        const BigInt Prime = BigInt::Pow(factors[T1].first, factors[T1].second);

        // Computing montgomery form needs BigInteger. But do it outside
        // the inner loop at least.
        BigIntToBigInteger(Prime, &prime);
        const int modulus_length = prime.nbrLimbs;
        const int NumberLengthBytes = modulus_length * (int)sizeof(limb);
        (void)memcpy(TheModulus, prime.limbs, NumberLengthBytes);
        TheModulus[modulus_length].x = 0;
        std::unique_ptr<MontgomeryParams> params =
          GetMontgomeryParams(modulus_length, TheModulus);

        if (VERBOSE) {
          printf("T1 %d [exp %d]. Prime: %s\n", T1,
                 factors[T1].second,
                 Prime.ToString().c_str());
        }

        for (int E = 0; E < T1; E++) {
          const BigInt Q1 = Tmp[T1] - Tmp[E];

          // L is overwritten before use below.
          const BigInt L1 = BigInt::Pow(factors[E].first, factors[E].second);

          Tmp[T1] = BigIntModularDivision(*params, Q1, L1, Prime);

          if (VERBOSE) {
            printf("T1 %d E %d. %s %s %s -> %s\n", T1, E,
                   Q1.ToString().c_str(), L1.ToString().c_str(),
                   Prime.ToString().c_str(), Tmp[T1].ToString().c_str());
          }
        }

        Tmp[T1] = BigInt::CMod(Tmp[T1], Prime);

        // Compute currentSolution as Tmp[T1] * Mult + currentSolution
        BigInt L2 = Tmp[T1] * Mult;
        CurrentSolution += L2;
        Mult *= Prime;
      }

      BigInt VV(0);
      BigInt KK1 = 0 - GcdAll;

      // BigIntSubt(&V, pGcdAll, &K1);

      // Perform loop while V < GcdAll.
      while (KK1 < 0) {
        // The solution is V*ValNn + currentSolution
        Solution(VV * NN + CurrentSolution);

        VV += 1;
        KK1 = VV - GcdAll;
      }

      // Maybe dead?
      // BigInteger K1;
      // BigIntToBigInteger(KK1, &K1);
      BigIntToBigInteger(VV, &V);

      for (T1 = nbrFactors - 1; T1 >= 0; T1--) {
        // XXX directly prime = pow(base, exp)
        const BigInt Prime = BigInt::Pow(factors[T1].first,
                                         factors[T1].second);
        // BigInteger bigBase;
        // BigIntToBigInteger(factors[T1].first, &bigBase);
        // (void)BigIntPowerIntExp(&bigBase, factors[T1].second, &prime);

        CHECK(T1 < (int)Solution1.size());
        CHECK(T1 < (int)Solution2.size());
        CHECK(T1 < (int)Increment.size());
        CHECK(T1 < (int)Exponents.size());

        // BigIntSubt(&Solution1[T1], &Solution2[T1], &K1);
        // if ((K1.nbrLimbs == 1) && (K1.limbs[0].x == 0)) {
        if (Solution1[T1] == Solution2[T1]) {
          // quad_info.Solution1[T1] == quad_info.Solution2[T1]
          Exponents[T1] += 2;
        } else {
          // quad_info.Solution1[T1] != quad_info.Solution2[T1]
          Exponents[T1]++;
        }

        // L <- Exponents[T1] * quad_info.Increment[T1]
        BigInt L1 = Increment[T1] * Exponents[T1];

        // PERF probably unnecessary? not used in this function
        // BigIntToBigInteger(L1, &L);
        // BigInteger inc;
        // BigIntToBigInteger(Increment[T1], &inc);
        // multadd(&L, Exponents[T1], &inc, 0);
        // multadd(&L, Exponents[T1], &Increment[T1], 0);

        // K1 <- 2 * prime
        BigInt K1 = Prime << 1;
        // BigInteger K1;
        // multadd(&K1, 2, &prime, 0);
        // BigIntSubt(&L1, &K1, &L1);
        if (L1 < K1) {
          break;
        }

        Exponents[T1] = 0;
      }   /* end for */
    } while (T1 >= 0);


  }

  // Solve Bx + C = 0 (mod N).
  // Port note: This writes 0, 1, or 2 solutions at the beginning of the
  // Solution array. Both Solution1 and Solution2 are the same. This path
  // is disjoint from the general quadratic solver.
  void SolveModularLinearEquation(const BigInt &GcdAll,
                                  BigInteger *pValA, const BigInteger *pValB,
                                  const BigInteger *pValC, BigInteger *pValN) {
    // No coverage for this entire function!
    printf("smodlineq coverage\n");
    interesting_coverage = true;

    // This thing generates its own factors for the Chinese Remainder
    // Theorem call.
    // auto factors = std::make_unique<Factors>();
    // factors->storage.resize(20000);

    std::vector<std::pair<BigInt, int>> factors;

    // int* ptrFactorsMod = factors->storage.data();
    // struct sFactors* pstFactor = &astFactorsMod[1];


    int solutionNbr = 0;
    BigInteger Tmp;
    BigIntGcd(pValB, pValN, &Tmp);
    if ((Tmp.nbrLimbs != 1) || (Tmp.limbs[0].x != 1)) {
      // ValB and ValN are not coprime. Go out.
      return;
    }
    // Calculate z <- -ValC / ValB (mod ValN)
    // Modular division routines used work for power of 2 or odd numbers.
    // This requires to compute the quotient in two steps.
    // N = r*2^k (r = odd)
    int powerOf2;
    DivideBigNbrByMaxPowerOf2(&powerOf2, pValN->limbs, &pValN->nbrLimbs);
    int modulus_length = pValN->nbrLimbs;
    int NumberLengthBytes = modulus_length * (int)sizeof(limb);
    if ((pValN->nbrLimbs != 1) || (pValN->limbs[0].x != 1)) {

      // ValN is not 1.
      Increment.push_back(BigIntegerToBigInt(pValN));
      // CopyBigInt(&Increment[solutionNbr], pValN);
      Exponents.push_back(1);

      (void)memcpy(TheModulus, pValN->limbs, NumberLengthBytes);
      TheModulus[modulus_length].x = 0;
      // Perform division using odd modulus r.
      const std::unique_ptr<MontgomeryParams> params =
        GetMontgomeryParams(modulus_length, TheModulus);

      const BigInt N = BigIntegerToBigInt(pValN);

      // Compute ptrSolution1 as ValC / |ValB|
      BigInt sol1 =
        BigIntModularDivision(*params,
                              BigIntegerToBigInt(pValC),
                              BigIntegerToBigInt(pValB),
                              N);

      // Compute ptrSolution1 as -ValC / ValB
      if (sol1 != 0) {
        sol1 = N - sol1;
        // BigIntSubt(pValN, ptrSolution1, ptrSolution1);
      }

      Solution1.push_back(sol1);
      Solution2.push_back(sol1);

      // CopyBigInt(ptrSolution2, ptrSolution1);

      factors.push_back(std::make_pair(N, 1));

      // BigInteger2IntArray(modulus_length, ptrFactorsMod, pValN);
      // factors->product.push_back({.array = ptrFactorsMod,
      // .multiplicity = 1});
      // in storage, skip the number of limbs we used
      // ptrFactorsMod += *ptrFactorsMod;
      // .. and length
      // ptrFactorsMod++;

      solutionNbr++;
    }

    CHECK((int)factors.size() == solutionNbr);
    CHECK((int)Solution1.size() == solutionNbr);
    CHECK((int)Solution2.size() == solutionNbr);
    CHECK((int)Increment.size() == solutionNbr);

    // Perform division using power of 2.
    if (powerOf2 > 0) {

      // Port note: This used to set ptrSolution1 to the power of 2,
      // I think just as a temporary?
      BigInteger pow2;
      BigIntPowerOf2(&pow2, powerOf2);
      // BigIntPowerOf2(ptrSolution1, powerOf2);
      Increment.push_back(BigIntegerToBigInt(&pow2));
      // CopyBigInt(&Increment[solutionNbr], ptrSolution1);
      Exponents.push_back(1);
      // Exponents[solutionNbr] = 1;

      factors.push_back(std::make_pair(BigIntegerToBigInt(&pow2), 1));
      // BigInteger2IntArray(modulus_length, ptrFactorsMod, &pow2);
      // BigInteger2IntArray(modulus_length, ptrFactorsMod, ptrSolution1);
      // CHECK(ptrSolution1->nbrLimbs > 0);

      // Port note: Original code didn't advance the factor pointer nor
      // storage pointer (probably because this is not in a loop) but
      // that just seems wrong.
      // factors->product.push_back({.array = ptrFactorsMod,
      // .multiplicity = 1});
      // in storage, skip the number of limbs we used
      // ptrFactorsMod += *ptrFactorsMod;
      // .. and length
      // ptrFactorsMod++;

      int modulus_length = 0;
      const std::unique_ptr<MontgomeryParams> params =
        GetMontgomeryParamsPowerOf2(powerOf2, &modulus_length);

      // Port note: This used to do this on ptrSolution1 in place.
      // ptrSolution1 <- 1 / |ValB|
      BigInteger inv;
      ComputeInversePower2(pValB->limbs, inv.limbs, modulus_length);
      // ComputeInversePower2(pValB->limbs, ptrSolution1->limbs,
      //  modulus_length);
      // Compute ptrSolution1 as |ValC| / |ValB|
      ModMult(*params,
              inv.limbs, pValC->limbs,
              modulus_length, TheModulus,
              inv.limbs);

      NumberLengthBytes = modulus_length * (int)sizeof(int);
      // Compute ptrSolution1 as -ValC / ValB
      if (pValB->sign == pValC->sign) {
        (void)memset(pValA->limbs, 0, NumberLengthBytes);
        SubtractBigNbr(pValA->limbs, inv.limbs,
                       inv.limbs, modulus_length);
        // SubtractBigNbr(pValA->limbs, ptrSolution1->limbs,
        // ptrSolution1->limbs, modulus_length);
      }

      // Discard bits outside number in most significant limb.
      inv.limbs[modulus_length - 1].x &=
        (1 << (powerOf2 % BITS_PER_GROUP)) - 1;
      inv.nbrLimbs = modulus_length;
      inv.sign = SIGN_POSITIVE;

      BigInt sol1 = BigIntegerToBigInt(&inv);
      Solution1.push_back(sol1);
      Solution2.push_back(sol1);
      // CopyBigInt(ptrSolution2, ptrSolution1);

      solutionNbr++;
    }

    CHECK((int)factors.size() == solutionNbr);
    CHECK((int)Solution1.size() == solutionNbr);
    CHECK((int)Solution2.size() == solutionNbr);
    CHECK((int)Increment.size() == solutionNbr);
    CHECK((int)Exponents.size() == solutionNbr);

    // astFactorsMod[0].multiplicity = solutionNbr;
    // assert((int)factors->product.size() == solutionNbr);

    PerformChineseRemainderTheorem(GcdAll, factors);
  }

  // Compute sqrRoot <- sqrt(ValCOdd) mod 2^expon.
  // To compute the square root, compute the inverse of sqrt,
  // so only multiplications are used.
  // f(x) = invsqrt(x), f_{n+1}(x) = f_n * (3 - x*f_n^2)/2
  void ComputeSquareRootModPowerOf2(int expon, int bitsCZero,
                                    int modulus_length) {
    // First approximation to inverse of square root.
    // If value is ...0001b, the inverse of square root is ...01b.
    // If value is ...1001b, the inverse of square root is ...11b.
    sqrRoot.limbs[0].x = (((ValCOdd.limbs[0].x & 15) == 1) ? 1 : 3);
    int correctBits = 2;
    int nbrLimbs = 1;
    BigInteger tmp1, tmp2;

    while (correctBits < expon) {
      // Compute f(x) = invsqrt(x), f_{n+1}(x) = f_n * (3 - x*f_n^2)/2
      correctBits *= 2;
      nbrLimbs = (correctBits / BITS_PER_GROUP) + 1;
      MultBigNbr(sqrRoot.limbs, sqrRoot.limbs, tmp2.limbs, nbrLimbs);
      MultBigNbr(tmp2.limbs, ValCOdd.limbs, tmp1.limbs, nbrLimbs);
      ChSignBigNbr(tmp1.limbs, nbrLimbs);
      int lenBytes = nbrLimbs * (int)sizeof(limb);
      (void)memset(tmp2.limbs, 0, lenBytes);
      tmp2.limbs[0].x = 3;
      AddBigNbr(tmp2.limbs, tmp1.limbs, tmp2.limbs, nbrLimbs);
      MultBigNbr(tmp2.limbs, sqrRoot.limbs, tmp1.limbs, nbrLimbs);
      (void)memcpy(sqrRoot.limbs, tmp1.limbs, lenBytes);
      DivBigNbrByInt(tmp1.limbs, 2, sqrRoot.limbs, nbrLimbs);
      correctBits--;
    }

    // Get square root of ValCOdd from its inverse by multiplying by ValCOdd.
    MultBigNbr(ValCOdd.limbs, sqrRoot.limbs, tmp1.limbs, nbrLimbs);
    int lenBytes = nbrLimbs * (int)sizeof(limb);

    (void)memcpy(sqrRoot.limbs, tmp1.limbs, lenBytes);
    setNbrLimbs(&sqrRoot, modulus_length);

    for (int ctr = 0; ctr < (bitsCZero / 2); ctr++) {
      BigIntMultiplyBy2(&sqrRoot);
    }
  }

  // PERF? rewrite FQS to work directly on BigInt
  void FindQuadraticSolution(BigInt* pSolution,
                             const BigInt &Quadr,
                             const BigInt &Linear,
                             const BigInt &Const,
                             int exponent) {
    BigInteger sol;
    BigInteger qq, ll, cc;
    BigIntToBigInteger(Quadr, &qq);
    BigIntToBigInteger(Linear, &ll);
    BigIntToBigInteger(Const, &cc);
    FindQuadraticSolutionInternal(&sol, &qq, &ll, &cc, exponent);
    *pSolution = BigIntegerToBigInt(&sol);
  }

  // Quadr, Linear, Const are arguments.
  // Find quadratic solution of
  //   Quadr*x^2 + Linear*x + Const = 0 (mod 2^expon)
  // when Quadr is even and Linear is odd. In this case there is a
  // unique solution.
  void FindQuadraticSolutionInternal(BigInteger* pSolution,
                                     BigInteger* Quadr_,
                                     BigInteger* Linear_,
                                     BigInteger* Const_,
                                     int exponent) {
    int expon = exponent;
    int bitMask = 1;
    limb* ptrSolution = pSolution->limbs;
    BigIntPowerOf2(&Aux0, expon);
    int bytesLen = Aux0.nbrLimbs * (int)sizeof(limb);
    (void)memset(ptrSolution, 0, bytesLen);
    while (expon > 0) {
      expon--;
      BigIntPowerOf2(&Aux2, expon);
      addbigint(&Aux2, -1);              // Aux2 <- 2^expon -1

      if ((Const_->limbs[0].x & 1) != 0) {
        // Const is odd.
        ptrSolution->x |= bitMask;
        // Compute Const as Quadr/2 + floor(Linear/2) + floor(Const/2) + 1
        if (Const_->sign == SIGN_NEGATIVE) {
          addbigint(Const_, -1);
        }
        BigIntDivideBy2(Const_);          // floor(Const/2)
        addbigint(Const_, 1);             // floor(Const/2) + 1
        CopyBigInt(&Aux1, Linear_);
        if (Aux1.sign == SIGN_NEGATIVE) {
          addbigint(&Aux1, -1);
        }
        BigIntDivideBy2(&Aux1);           // floor(Linear/2)
        BigIntAdd(Const_, &Aux1, Const_);
        CopyBigInt(&Aux1, Quadr_);
        BigIntDivideBy2(&Aux1);            // Quadr/2
        BigIntAdd(Const_, &Aux1, Const_);

        // Linear <- 2*Quadr + Linear and Quadr <- 2*Quadr.
        BigIntMultiplyBy2(Quadr_);         // Quadr*2
        BigIntAdd(Linear_, Quadr_, Linear_);
        BigIntAnd(Linear_, &Aux2, Linear_);   // Reduce mod 2^expon
      } else {
        // Const is even.
        BigIntDivideBy2(Const_);           // Const/2
        BigIntMultiplyBy2(Quadr_);         // Quadr*2
      }

      BigIntAnd(Const_, &Aux2, Const_);    // Reduce mod 2^expon
      BigIntAnd(Quadr_, &Aux2, Quadr_);    // Reduce mod 2^expon
      bitMask *= 2;
      if (bitMask < 0) {
        bitMask = 1;
        ptrSolution++;
      }
    }

    int modulus_length = Aux0.nbrLimbs;
    setNbrLimbs(pSolution, modulus_length);
  }

  // Solve Ax^2 + Bx + C = 0 (mod 2^expon).
  // If a solution is found, writes to Solution(1,2)[factorIndex]
  // and returns true.
  bool SolveQuadraticEqModPowerOf2(
      int exponent, int factorIndex,
      const BigInteger *pValA,
      const BigInteger *pValB,
      const BigInteger *pValC) {
    int expon = exponent;
    int bitsAZero;
    int bitsBZero;
    int bitsCZero;
    // ax^2 + bx + c = 0 (mod 2^expon)
    // This follows the paper Complete solving the quadratic equation mod 2^n
    // of Dehnavi, Shamsabad and Rishakani.
    // Get odd part of A, B and C and number of bits to zero.
    CopyBigInt(&ValAOdd, pValA);
    DivideBigNbrByMaxPowerOf2(&bitsAZero, ValAOdd.limbs, &ValAOdd.nbrLimbs);
    CopyBigInt(&ValBOdd, pValB);
    DivideBigNbrByMaxPowerOf2(&bitsBZero, ValBOdd.limbs, &ValBOdd.nbrLimbs);
    CopyBigInt(&ValCOdd, pValC);
    DivideBigNbrByMaxPowerOf2(&bitsCZero, ValCOdd.limbs, &ValCOdd.nbrLimbs);

    if ((bitsAZero > 0) && (bitsBZero > 0) && (bitsCZero > 0)) {
      int minExpon = bitsAZero;
      if (minExpon < bitsBZero) {
        minExpon = bitsBZero;
      }
      if (minExpon < bitsCZero) {
        minExpon = bitsCZero;
      }
      bitsAZero -= minExpon;
      bitsBZero -= minExpon;
      bitsCZero -= minExpon;
      expon -= minExpon;
    }

    if (((bitsAZero == 0) && (bitsBZero == 0) && (bitsCZero == 0)) ||
      ((bitsAZero > 0) && (bitsBZero > 0) && (bitsCZero == 0))) {
      return false;   // No solutions, so go out.
    }

    BigInteger tmp1, tmp2;
    if ((bitsAZero == 0) && (bitsBZero > 0)) {
      // The solution in this case requires square root.
      // compute s = ((b/2)^2 - a*c)/a^2, q = odd part of s,
      // r = maximum exponent of power of 2 that divides s.
      CopyBigInt(&tmp1, pValB);
      BigIntDivideBy2(&tmp1);
      (void)BigIntMultiply(&tmp1, &tmp1, &tmp1);  // (b/2)^2
      (void)BigIntMultiply(pValA, pValC, &tmp2);  // a*c
      BigIntSubt(&tmp1, &tmp2, &tmp1);      // (b/2)^2 - a*c
      BigInteger K1;
      BigIntPowerOf2(&K1, expon);
      addbigint(&K1, -1);
      BigIntAnd(&tmp1, &K1, &ValCOdd);      // (b/2) - a*c mod 2^n

      int modulus_length = K1.nbrLimbs;

      if (modulus_length > ValAOdd.nbrLimbs) {
        int lenBytes = (modulus_length - ValAOdd.nbrLimbs) * (int)sizeof(int);
        (void)memset(&ValAOdd.limbs[ValAOdd.nbrLimbs], 0, lenBytes);
      }

      if (modulus_length > tmp2.nbrLimbs) {
        int lenBytes = (modulus_length - tmp2.nbrLimbs) * (int)sizeof(int);
        (void)memset(&tmp2.limbs[tmp2.nbrLimbs], 0, lenBytes);
      }

      ComputeInversePower2(ValAOdd.limbs, tmp2.limbs, modulus_length);
      (void)BigIntMultiply(&ValCOdd, &tmp2, &ValCOdd);
      BigIntAnd(&ValCOdd, &K1, &ValCOdd);      // ((b/2) - a*c)/a mod 2^n
      (void)BigIntMultiply(&ValCOdd, &tmp2, &ValCOdd);
      BigIntAnd(&ValCOdd, &K1, &ValCOdd);      // s = ((b/2) - a*c)/a^2 mod 2^n
      if (BigIntIsZero(&ValCOdd)) {
        // s = 0, so its square root is also zero.
        intToBigInteger(&sqrRoot, 0);
        expon -= expon / 2;
      } else {
        DivideBigNbrByMaxPowerOf2(&bitsCZero, ValCOdd.limbs, &ValCOdd.nbrLimbs);
        // At this moment, bitsCZero = r and ValCOdd = q.
        if (((ValCOdd.limbs[0].x & 7) != 1) || (bitsCZero & 1)) {
          // q != 1 or p2(r) == 0, so go out.
          return false;
        }
        if (expon < 2) {
          // Modulus is 2.
          intToBigInteger(&sqrRoot, (bitsCZero > 0) ? 0 : 1);
        } else {
          // Compute sqrRoot as the square root of ValCOdd.
          expon -= bitsCZero / 2;
          ComputeSquareRootModPowerOf2(expon, bitsCZero, modulus_length);
          expon--;
          if (expon == (bitsCZero / 2)) {
            expon++;
          }
        }
      }

      // x = sqrRoot - b/2a.
      {
        BigInteger K1;
        BigIntPowerOf2(&K1, expon);
        addbigint(&K1, -1);
        modulus_length = K1.nbrLimbs;
        ComputeInversePower2(ValAOdd.limbs, tmp2.limbs, modulus_length);
        setNbrLimbs(&tmp2, modulus_length);
        CopyBigInt(&tmp1, pValB);
        BigIntDivideBy2(&tmp1);               // b/2
        (void)BigIntMultiply(&tmp1, &tmp2, &tmp1);  // b/2a
        BigIntChSign(&tmp1);                  // -b/2a
        BigIntAnd(&tmp1, &K1, &tmp1);         // -b/2a mod 2^expon
        BigIntAdd(&tmp1, &sqrRoot, &tmp2);

        BigInteger sol;
        BigIntAnd(&tmp2, &K1, &sol);
        Solution1[factorIndex] = BigIntegerToBigInt(&sol);
        BigIntSubt(&tmp1, &sqrRoot, &tmp2);
        BigIntAnd(&tmp2, &K1, &sol);
        Solution2[factorIndex] = BigIntegerToBigInt(&sol);
      }

    } else if ((bitsAZero == 0) && (bitsBZero == 0)) {
      BigInt A2 = BigIntegerToBigInt(pValA) << 1;
      BigInt B = BigIntegerToBigInt(pValB);

      BigInteger Linear, Const;

      // CopyBigInt(&Quadr, pValA);
      // BigIntMultiplyBy2(&Quadr);         // 2a
      // CopyBigInt(&Linear, pValB);        // b
      CopyBigInt(&Const, pValC);
      BigIntDivideBy2(&Const);           // c/2
      FindQuadraticSolution(&Solution1[factorIndex],
                            A2,
                            B,
                            BigIntegerToBigInt(&Const),
                            expon - 1);

      Solution1[factorIndex] <<= 1;
      // BigIntMultiplyBy2(&Solution1[factorIndex]);

      // CopyBigInt(&Quadr, pValA);
      // BigIntMultiplyBy2(&Quadr);         // 2a
      // BigIntAdd(&Quadr, pValB, &Linear); // 2a+b
      CopyBigInt(&Const, pValA);
      BigIntAdd(&Const, pValB, &Const);
      BigIntAdd(&Const, pValC, &Const);
      BigIntDivideBy2(&Const);           // (a+b+c)/2
      FindQuadraticSolution(&Solution2[factorIndex],
                            A2,
                            A2 + B,
                            BigIntegerToBigInt(&Const),
                            expon - 1);

      Solution2[factorIndex] <<= 1;
      Solution2[factorIndex] += 1;

      // BigIntMultiplyBy2(&Solution2[factorIndex]);
      // addbigint(&Solution2[factorIndex], 1);

    } else {

      // CopyBigInt(&Quadr, pValA);
      // CopyBigInt(&Linear, pValB);
      // CopyBigInt(&Const, pValC);
      FindQuadraticSolution(&Solution1[factorIndex],
                            BigIntegerToBigInt(pValA),
                            BigIntegerToBigInt(pValB),
                            BigIntegerToBigInt(pValC),
                            expon);
      sol2Invalid = true;
    }

    // XXX how is this used? It looks to me like all
    // all other code paths write Q before using it.
    // BigIntToBigInteger(BigInt(0x0DDBALL), &Q);
    BigIntPowerOf2(&Q, expon);         // Store increment.
    return true;
  }

  void ComputeSquareRootModPowerOfP(const BigInt &Base,
                                    int nbrBitsSquareRoot) {
    // XXX native
    BigInteger base;
    BigIntToBigInteger(Base, &base);

    int correctBits;
    int modulus_length = prime.nbrLimbs;
    int NumberLengthBytes = modulus_length * (int)sizeof(limb);
    (void)memcpy(TheModulus, prime.limbs, NumberLengthBytes);
    TheModulus[modulus_length].x = 0;
    const std::unique_ptr<MontgomeryParams> params =
      GetMontgomeryParams(modulus_length, TheModulus);
    const BigInt Prime = BigIntegerToBigInt(&prime);

    // This could be a function that returns SqrtDiscr ?
    if ((prime.limbs[0].x & 3) == 3) {
      // prime mod 4 = 3
      // subtractdivide(&Q, -1, 4);   // Q <- (prime+1)/4.

      BigInt SD =
        BigIntModularPower(*params, modulus_length, TheModulus,
                           Base,
                           (Prime + 1) >> 2);
      BigIntToBigInteger(SD, &SqrtDisc);

    } else {
      CopyBigInt(&Q, &prime);

      limb* toConvert = nullptr;
      // Convert discriminant to Montgomery notation.
      // XXX can convert fixed limbs from Base
      CompressLimbsBigInteger(modulus_length, Aux5.limbs, &base);
      ModMult(*params,
              Aux5.limbs, params->MontgomeryMultR2,
              modulus_length, TheModulus,
              Aux6.limbs);  // u
      if ((prime.limbs[0].x & 7) == 5) {
        // prime mod 8 = 5: use Atkin's method for modular square roots.
        // Step 1. v <- (2u)^((p-5)/8) mod p
        // Step 2. i <- (2uv^2) mod p
        // Step 3. square root of u <- uv (i-1)
        // Step 1.
        // Q <- (prime-5)/8.
        subtractdivide(&Q, 5, 8);
        // 2u
        AddBigNbrModN(Aux6.limbs, Aux6.limbs, Aux7.limbs,
                      TheModulus, modulus_length);
        ModPow(*params, modulus_length, TheModulus,
               Aux7.limbs, Q.limbs, Q.nbrLimbs, Aux8.limbs);
        // At this moment Aux7.limbs is v in Montgomery notation.

        // Step 2.
        ModMult(*params,
                Aux8.limbs, Aux8.limbs,
                modulus_length, TheModulus,
                Aux9.limbs);  // v^2
        ModMult(*params,
                Aux7.limbs, Aux9.limbs,
                modulus_length, TheModulus,
                Aux9.limbs);  // i

        // Step 3.
        // i-1
        SubtBigNbrModN(Aux9.limbs, params->MontgomeryMultR1, Aux9.limbs,
                       TheModulus, modulus_length);
        // v*(i-1)
        ModMult(*params,
                Aux8.limbs, Aux9.limbs,
                modulus_length, TheModulus,
                Aux9.limbs);
        // u*v*(i-1)
        ModMult(*params,
                Aux6.limbs, Aux9.limbs,
                modulus_length, TheModulus,
                Aux9.limbs);
        toConvert = Aux9.limbs;
      } else {
        // prime = 1 (mod 8). Use Shanks' method for modular square roots.
        // Step 1. Select e >= 3, q odd such that p = 2^e * q + 1.
        // Step 2. Choose x at random in the range 1 < x < p such that
        //           jacobi (x, p) = -1.
        // Step 3. Set z <- x^q (mod p).
        // Step 4. Set y <- z, r <- e, x <- a^((q-1)/2) mod p,
        //           v <- ax mod p, w <- vx mod p.
        // Step 5. If w = 1, the computation ends with +/-v as the square root.
        // Step 6. Find the smallest value of k such that w^(2^k) = 1 (mod p)
        // Step 7. Set d <- y^(2^(r-k-1)) mod p, y <- d^2 mod p,
        //           r <- k, v <- dv mod p, w <- wy mod p.
        // Step 8. Go to step 5.

        // Step 1.
        subtractdivide(&Q, 1, 1);   // Q <- (prime-1).
        int e;
        DivideBigNbrByMaxPowerOf2(&e, Q.limbs, &Q.nbrLimbs);
        // Step 2.
        int x = 1;

        {
          const BigInt Prime = BigIntegerToBigInt(&prime);
          do {
            x++;
          } while (BigInt::Jacobi(BigInt(x), Prime) >= 0);
        }

        // Step 3.
        // Get z <- x^q (mod p) in Montgomery notation.
        ModPowBaseInt(*params, modulus_length, TheModulus,
                      x, Q.limbs, Q.nbrLimbs, Aux4.limbs);  // z
        // Step 4.
        NumberLengthBytes = modulus_length * (int)sizeof(limb);
        (void)memcpy(Aux5.limbs, Aux4.limbs, NumberLengthBytes); // y
        int r = e;
        BigInteger K1;
        CopyBigInt(&K1, &Q);
        subtractdivide(&K1, 1, 2);
        ModPow(*params, modulus_length, TheModulus,
               Aux6.limbs, K1.limbs, K1.nbrLimbs, Aux7.limbs); // x
        ModMult(*params,
                Aux6.limbs, Aux7.limbs,
                modulus_length, TheModulus,
                Aux8.limbs);         // v
        ModMult(*params,
                Aux8.limbs, Aux7.limbs,
                modulus_length, TheModulus,
                Aux9.limbs);         // w

        // Step 5
        while (memcmp(Aux9.limbs, params->MontgomeryMultR1,
                      NumberLengthBytes) != 0) {
          // Step 6
          int k = 0;
          (void)memcpy(Aux10.limbs, Aux9.limbs, NumberLengthBytes);
          do {
            k++;
            ModMult(*params,
                    Aux10.limbs, Aux10.limbs,
                    modulus_length, TheModulus,
                    Aux10.limbs);
          } while (memcmp(Aux10.limbs, params->MontgomeryMultR1,
                          NumberLengthBytes) != 0);
          // Step 7
          (void)memcpy(Aux11.limbs, Aux5.limbs, NumberLengthBytes); // d
          for (int ctr = 0; ctr < (r - k - 1); ctr++) {
            ModMult(*params,
                    Aux11.limbs, Aux11.limbs,
                    modulus_length, TheModulus,
                    Aux11.limbs);
          }
          ModMult(*params,
                  Aux11.limbs, Aux11.limbs,
                  modulus_length, TheModulus,
                  Aux5.limbs);   // y
          r = k;
          ModMult(*params,
                  Aux8.limbs, Aux11.limbs,
                  modulus_length, TheModulus,
                  Aux8.limbs);    // v
          ModMult(*params,
                  Aux9.limbs, Aux5.limbs,
                  modulus_length, TheModulus,
                  Aux9.limbs);     // w
        }
        toConvert = Aux8.limbs;
      }

      // Convert from Montgomery to standard notation.
      NumberLengthBytes = modulus_length * (int)sizeof(limb);
      // Convert power to standard notation.
      (void)memset(Aux4.limbs, 0, NumberLengthBytes);
      Aux4.limbs[0].x = 1;
      ModMult(*params,
              Aux4.limbs, toConvert,
              modulus_length, TheModulus,
              toConvert);
      UncompressLimbsBigInteger(modulus_length, toConvert, &SqrtDisc);
    }

    // Obtain inverse of square root stored in SqrtDisc (mod prime).
    BigInteger tmp2;
    intToBigInteger(&tmp2, 1);
    BigInt SqrRoot =
      BigIntModularDivision(*params,
                            BigInt(1), BigIntegerToBigInt(&SqrtDisc),
                            BigIntegerToBigInt(&prime));

    // PERF good place to do this natively
    BigIntToBigInteger(SqrRoot, &sqrRoot);
    correctBits = 1;
    CopyBigInt(&Q, &prime);

    // Obtain nbrBitsSquareRoot correct digits of inverse square root.
    while (correctBits < nbrBitsSquareRoot) {
      BigInteger tmp1;
      // Compute f(x) = invsqrt(x), f_{n+1}(x) = f_n * (3 - x*f_n^2)/2
      correctBits *= 2;
      (void)BigIntMultiply(&Q, &Q, &Q);           // Square Q.
      (void)BigIntMultiply(&sqrRoot, &sqrRoot, &tmp1);
      (void)BigIntRemainder(&tmp1, &Q, &tmp2);

      BigIntToBigInteger(Discriminant * BigIntegerToBigInt(&tmp2),
                         &tmp1);
      // (void)BigIntMultiply(&tmp2, &discriminant, &tmp1);

      (void)BigIntRemainder(&tmp1, &Q, &tmp2);
      intToBigInteger(&tmp1, 3);
      BigIntSubt(&tmp1, &tmp2, &tmp2);
      (void)BigIntMultiply(&tmp2, &sqrRoot, &tmp1);
      if ((tmp1.limbs[0].x & 1) != 0) {
        BigIntAdd(&tmp1, &Q, &tmp1);
      }
      BigIntDivide2(&tmp1);
      (void)BigIntRemainder(&tmp1, &Q, &sqrRoot);
    }

    // Get square root of discriminant from its inverse by multiplying
    // by discriminant.
    if (sqrRoot.sign == SIGN_NEGATIVE) {
      BigIntAdd(&sqrRoot, &Q, &sqrRoot);
    }


    BigIntToBigInteger(Discriminant * BigIntegerToBigInt(&sqrRoot),
                       &sqrRoot);
    // (void)BigIntMultiply(&sqrRoot, &discriminant, &sqrRoot);
    (void)BigIntRemainder(&sqrRoot, &Q, &sqrRoot);
  }

  // Solve Ax^2 + Bx + C = 0 (mod p^expon).
  bool SolveQuadraticEqModPowerOfP(
      int expon, int factorIndex,
      const BigInteger* pValA, const BigInteger* pValB) {
    // Number of bits of square root of discriminant to compute:
    //   expon + bits_a + 1,
    // where bits_a is the number of least significant bits of
    // a set to zero.
    // To compute the square root, compute the inverse of sqrt,
    // so only multiplications are used.
    //   f(x) = invsqrt(x), f_{n+1}(x) = f_n * (3 - x*f_n^2)/2
    // Get maximum power of prime which divides ValA.
    CopyBigInt(&ValAOdd, pValA);
    int bitsAZero = 0;

    for (;;) {
      BigInteger tmp1;
      (void)BigIntRemainder(&ValAOdd, &prime, &tmp1);
      if (ValAOdd.sign == SIGN_NEGATIVE) {
        BigIntAdd(&ValAOdd, &prime, &ValAOdd);
      }
      if (!BigIntIsZero(&tmp1)) {
        break;
      }
      (void)BigIntDivide(&ValAOdd, &prime, &ValAOdd);
      bitsAZero++;
    }
    Discriminant %= BigIntegerToBigInt(&V);
    // (void)BigIntRemainder(&discriminant, &V, &discriminant);

    // Get maximum power of prime which divides discriminant.
    int deltaZeros;
    if (Discriminant == 0) {
      // Discriminant is zero.
      deltaZeros = expon;
    } else {
      // Discriminant is not zero.
      deltaZeros = 0;
      const BigInt Prime = BigIntegerToBigInt(&prime);
      for (;;) {
        // (void)BigIntRemainder(&discriminant, &prime, &tmp1);
        if (!BigInt::DivisibleBy(Discriminant, Prime)) {
          break;
        }
        Discriminant = BigInt::DivExact(Discriminant, Prime);
        deltaZeros++;
      }
    }

    if (((deltaZeros & 1) != 0) && (deltaZeros < expon)) {
      // If delta is of type m*prime^n where m is not multiple of prime
      // and n is odd, there is no solution, so go out.
      return false;
    }

    deltaZeros >>= 1;
    // Compute inverse of -2*A (mod prime^(expon - deltaZeros)).
    BigIntAdd(pValA, pValA, &ValAOdd);
    BigInteger tmp1;
    (void)BigIntPowerIntExp(&prime, expon - deltaZeros, &tmp1);
    (void)BigIntRemainder(&ValAOdd, &tmp1, &ValAOdd);
    int nbrLimbs = tmp1.nbrLimbs;

    if (ValAOdd.sign == SIGN_NEGATIVE) {
      ValAOdd.sign = SIGN_POSITIVE;           // Negate 2*A
    } else if (!BigIntIsZero(&ValAOdd)) {
      BigIntSubt(&tmp1, &ValAOdd, &ValAOdd);  // Negate 2*A
    } else {
      // Nothing to do.
    }

    {
      int modulus_length = tmp1.nbrLimbs;
      int NumberLengthBytes = modulus_length * (int)sizeof(limb);
      (void)memcpy(TheModulus, tmp1.limbs, NumberLengthBytes);
      TheModulus[modulus_length].x = 0;
      const std::unique_ptr<MontgomeryParams> params =
        GetMontgomeryParams(modulus_length, TheModulus);

      BigInt Tmp =
        BigIntModularDivision(*params, BigInt(1),
                              BigIntegerToBigInt(&ValAOdd),
                              BigIntegerToBigInt(&tmp1));
      BigIntToBigInteger(Tmp, &ValAOdd);
    }

    if (Discriminant == 0) {
      // Discriminant is zero.
      int lenBytes = nbrLimbs * (int)sizeof(limb);
      (void)memset(sqrRoot.limbs, 0, lenBytes);
      sqrRoot.nbrLimbs = 1;
      sqrRoot.sign = SIGN_POSITIVE;
    } else {
      // Discriminant is not zero.
      // Find number of digits of square root to compute.
      int nbrBitsSquareRoot = expon + bitsAZero - deltaZeros;
      (void)BigIntPowerIntExp(&prime, nbrBitsSquareRoot, &tmp1);
      nbrLimbs = tmp1.nbrLimbs;
      const BigInt Tmp1 = BigIntegerToBigInt(&tmp1);
      Discriminant %= Tmp1;
      // (void)BigIntRemainder(&discriminant, &tmp1, &discriminant);

      if (Discriminant < 0) {
        Discriminant += Tmp1;
      }

      // Port note: Not clear why the original code did this?
      // It doesn't seem to use the limbs directly.
      // if (nbrLimbs > discriminant.nbrLimbs) {
      // int lenBytes = (nbrLimbs - discriminant.nbrLimbs) * (int)sizeof(limb);
      // (void)memset(&discriminant.limbs[nbrLimbs], 0, lenBytes);
      // }

      {
        const BigInt Prime = BigIntegerToBigInt(&prime);
        BigInt Tmp = Discriminant % Prime;
        // (void)BigIntRemainder(&discriminant, &prime, &Tmp);
        if (Tmp < 0) {
          Tmp += Prime;
        }

        if (BigInt::Jacobi(Tmp, Prime) != 1) {
          // Not a quadratic residue, so go out.
          return false;
        }

        // Port note: This used to be passed in Aux3. This call might
        // expect more state in Aux, ugh.

        // Compute square root of discriminant.
        ComputeSquareRootModPowerOfP(Tmp, nbrBitsSquareRoot);
      }

      // Multiply by square root of discriminant by prime^deltaZeros.
      for (int ctr = 0; ctr < deltaZeros; ctr++) {
        (void)BigIntMultiply(&sqrRoot, &prime, &sqrRoot);
      }
    }

    int correctBits = expon - deltaZeros;

    // Store increment.
    // Q <- prime^correctBits
    (void)BigIntPowerIntExp(&prime, correctBits, &Q);
    // Compute x = (b + sqrt(discriminant)) / (-2a) and
    //   x = (b - sqrt(discriminant)) / (-2a)
    BigIntAdd(pValB, &sqrRoot, &tmp1);

    for (int ctr = 0; ctr < bitsAZero; ctr++) {
      BigInteger tmp2;
      (void)BigIntRemainder(&tmp1, &prime, &tmp2);
      if (!BigIntIsZero(&tmp2)) {
        // Cannot divide by prime, so go out.
        sol1Invalid = true;
        break;
      }
      (void)BigIntDivide(&tmp1, &prime, &tmp1);
    }

    (void)BigIntMultiply(&tmp1, &ValAOdd, &tmp1);
    Solution1[factorIndex] = BigInt::CMod(BigIntegerToBigInt(&tmp1),
                                          BigIntegerToBigInt(&Q));
    // (void)BigIntRemainder(&tmp1, &Q, &Solution1[factorIndex]);

    if (Solution1[factorIndex] < 0) {
      Solution1[factorIndex] += BigIntegerToBigInt(&Q);
      // BigIntAdd(&Solution1[factorIndex], &Q, &Solution1[factorIndex]);
    }

    BigIntSubt(pValB, &sqrRoot, &tmp1);
    for (int ctr = 0; ctr < bitsAZero; ctr++) {
      BigInteger tmp2;
      (void)BigIntRemainder(&tmp1, &prime, &tmp2);
      if (!BigIntIsZero(&tmp2)) {
        // Cannot divide by prime, so go out.
        sol2Invalid = true;
        break;
      }
      (void)BigIntDivide(&tmp1, &prime, &tmp1);
    }

    (void)BigIntMultiply(&tmp1, &ValAOdd, &tmp1);
    Solution2[factorIndex] = BigInt::CMod(BigIntegerToBigInt(&tmp1),
                                          BigIntegerToBigInt(&Q));
    // (void)BigIntRemainder(&tmp1, &Q, &Solution2[factorIndex]);

    if (Solution2[factorIndex] < 0) {
      Solution2[factorIndex] += BigIntegerToBigInt(&Q);
      // BigIntAdd(&Solution2[factorIndex], &Q, &Solution2[factorIndex]);
    }

    return true;
  }

  void QuadraticTermMultipleOfP(
      int expon, int factorIndex,
      const BigInteger *pValA,
      const BigInteger *pValB,
      const BigInteger *pValC) {
    // Perform Newton approximation.
    // The next value of x in sequence x_{n+1} is
    //   x_n - (a*x_n^2 + b*x_n + c) / (2*a_x + b).

    // No coverage!
    printf("qtermmultp coverage\n");
    interesting_coverage = true;

    BigInt sol;

    int modulus_length = prime.nbrLimbs;
    int NumberLengthBytes = modulus_length * (int)sizeof(limb);
    (void)memcpy(TheModulus, prime.limbs, NumberLengthBytes);
    TheModulus[modulus_length].x = 0;
    const std::unique_ptr<MontgomeryParams> params =
      GetMontgomeryParams(modulus_length, TheModulus);

    const BigInt Prime = BigIntegerToBigInt(&prime);
    BigInt TmpSolution =
      -BigIntModularDivision(*params,
                             BigIntegerToBigInt(pValC),
                             BigIntegerToBigInt(pValB),
                             Prime);
    // PERF chsign?
    // BigIntNegate(&tmpSolution, &tmpSolution);
    if (TmpSolution < 0) {
      TmpSolution += Prime;
    }

    // PERF: Directly?
    BigInteger tmpSolution;
    BigIntToBigInteger(TmpSolution, &tmpSolution);

    for (int currentExpon = 2; currentExpon < (2 * expon); currentExpon *= 2) {
      (void)BigIntPowerIntExp(&prime, currentExpon, &V);
      // Q <- a*x_n
      (void)BigIntMultiply(pValA, &tmpSolution, &Q);
      BigInt L = BigIntegerToBigInt(&Q);
      // CopyBigInt(&L, &Q);
      BigIntAdd(&Q, pValB, &Q);                    // a*x_n + b
      (void)BigIntRemainder(&Q, &V, &Q);
      (void)BigIntMultiply(&Q, &tmpSolution, &Q);   // a*x_n^2 + b*x_n
      BigIntAdd(&Q, pValC, &Q);                    // a*x_n^2 + b*x_n + c
      (void)BigIntRemainder(&Q, &V, &Q);           // Numerator.
      L <<= 1;
      // MultInt(&L, &L, 2);                          // 2*a*x_n
      L += BigIntegerToBigInt(pValB);
      // BigIntAdd(&L, pValB, &L);                    // 2*a*x_n + b
      L %= BigIntegerToBigInt(&V);
      // (void)BigIntRemainder(&L, &V, &L);           // Denominator
      int modulus_length = V.nbrLimbs;
      int NumberLengthBytes = modulus_length * (int)sizeof(limb);
      (void)memcpy(TheModulus, V.limbs, NumberLengthBytes);
      TheModulus[modulus_length].x = 0;
      const std::unique_ptr<MontgomeryParams> params =
        GetMontgomeryParams(modulus_length, TheModulus);
      BigInt Aux =
        BigIntModularDivision(*params,
                              BigIntegerToBigInt(&Q),
                              L,
                              BigIntegerToBigInt(&V));
      BigIntToBigInteger(Aux, &Aux1);
      BigIntSubt(&tmpSolution, &Aux1, &tmpSolution);
      (void)BigIntRemainder(&tmpSolution, &V, &tmpSolution);

      if (tmpSolution.sign == SIGN_NEGATIVE) {
        BigIntAdd(&tmpSolution, &V, &tmpSolution);
      }
    }
    // Q <- prime^expon
    (void)BigIntPowerIntExp(&prime, expon, &Q);
    (void)BigIntRemainder(&tmpSolution, &Q, &tmpSolution);

    Solution1[factorIndex] = BigIntegerToBigInt(&tmpSolution);
    Solution2[factorIndex] = Solution1[factorIndex];
    // CopyBigInt(&Solution2[factorIndex], &Solution1[factorIndex]);
  }

  // If solutions found, writes normalized solutions at factorIndex
  // and returns true.
  bool QuadraticTermNotMultipleOfP(
      const BigInt &GcdAll,
      int expon, int factorIndex,
      const BigInteger *pValA,
      const BigInteger *pValB,
      const BigInteger *pValC) {

    const BigInt A = BigIntegerToBigInt(pValA);
    const BigInt B = BigIntegerToBigInt(pValB);
    const BigInt C = BigIntegerToBigInt(pValC);

    sol1Invalid = false;
    sol2Invalid = false;
    // Compute discriminant = ValB^2 - 4*ValA*ValC.
    Discriminant = B * B - ((A * C) << 2);

    bool solutions = false;
    if ((prime.nbrLimbs == 1) && (prime.limbs[0].x == 2)) {
      /* Prime p is 2 */
      solutions = SolveQuadraticEqModPowerOf2(expon, factorIndex,
                                              pValA, pValB, pValC);
    } else {
      // Prime is not 2
      solutions = SolveQuadraticEqModPowerOfP(expon, factorIndex,
                                              pValA, pValB);
    }

    if (!solutions || (sol1Invalid && sol2Invalid)) {
      // Both solutions are invalid. Go out.
      return false;
    }

    if (sol1Invalid) {
      // Solution1 is invalid. Overwrite it with Solution2.
      Solution1[factorIndex] = Solution2[factorIndex];
      // CopyBigInt(&Solution1[factorIndex], &Solution2[factorIndex]);
    } else if (sol2Invalid) {
      // Solution2 is invalid. Overwrite it with Solution1.
      Solution2[factorIndex] = Solution1[factorIndex];
      // CopyBigInt(&Solution2[factorIndex], &Solution1[factorIndex]);
    } else {
      // Nothing to do.
    }

    if (Solution2[factorIndex] < Solution1[factorIndex]) {
      std::swap(Solution1[factorIndex], Solution2[factorIndex]);
    }

    // BigInteger Tmp;
    // BigIntSubt(&Solution2[factorIndex], &Solution1[factorIndex], &Tmp);
    // if (Tmp < 0) {
    //   // Solution2 is less than Solution1, so exchange them.
    //   CopyBigInt(&Tmp, &Solution1[factorIndex]);
    //   CopyBigInt(&Solution1[factorIndex], &Solution2[factorIndex]);
    //   CopyBigInt(&Solution2[factorIndex], &Tmp);
    // }

    return true;
  }

  // Solve Ax^2 + Bx + C = 0 (mod N).
  void SolveEquation(
      const SolutionFn &solutionCback,
      const BigInt &A, const BigInt &B,
      const BigInt &C, const BigInt &N,
      const BigInt &GcdAll, const BigInt &NN_arg) {

    BigInteger valA, valB, valC, valN;
    BigIntToBigInteger(A, &valA);
    BigIntToBigInteger(B, &valB);
    BigIntToBigInteger(C, &valC);
    BigIntToBigInteger(N, &valN);

    NN = NN_arg;

    // PERF: no need to copy
    Solution = solutionCback;

    if (BigInt::CMod(A, N) == 0) {
      // Linear equation.
      SolveModularLinearEquation(GcdAll, &valA, &valB, &valC, &valN);
      return;
    }

    // PERF: This code will reuse factors. We might want to do that.
    #if 0
    if ((LastModulus.nbrLimbs == 0) || !BigIntEqual(&LastModulus, pValN))
    {     // Last modulus is different from ValN.
      CopyBigInt(&LastModulus, pValN);
      int modulus_length = pValN->nbrLimbs;
      BigInteger2IntArray(modulus_length, nbrToFactor, pValN);
      factor(pValN, nbrToFactor, factorsMod, astFactorsMod);
    }
    #endif

    // mimicking original code, but now we aren't using the LastModulus
    // at all, I think
    // CopyBigInt(&LastModulus, pValN);
    // std::unique_ptr<Factors> factors = BigFactor(pValN);
    std::vector<std::pair<BigInt, int>> factors = BigIntFactor(N);

    intToBigInteger(&Q, 0);
    // intToBigInteger(&Q, 0xCAFE);
    const int nbrFactors = factors.size();
    // const sFactorz *pstFactor = &factors->product[0];

    Solution1.resize(nbrFactors);
    Solution2.resize(nbrFactors);
    Increment.resize(nbrFactors);
    Exponents.resize(nbrFactors);

    for (int factorIndex = 0; factorIndex < nbrFactors; factorIndex++) {
      // XXX we never return a 0 exponent, right?
      int expon = factors[factorIndex].second;
      if (expon == 0) {
        Solution1[factorIndex] = BigInt(0);
        Solution2[factorIndex] = BigInt(0);
        Increment[factorIndex] = BigInt(1);
        // Port note: Was uninitialized.
        Exponents[factorIndex] = 0;
        continue;
      }

      const BigInt &Prime = factors[factorIndex].first;
      // Used, but not on all paths.
      BigIntToBigInteger(Prime, &prime);

      // Just used in SolveQuadraticEqModPowerOfP.
      (void)BigIntPowerIntExp(&prime, expon, &V);
      // (void)BigIntRemainder(pValA, &prime, &L);

      if (Prime != 2 &&
          BigInt::DivisibleBy(A, Prime)) {
        // ValA multiple of prime means a linear equation mod prime.
        // Also prime is not 2.
        if (B == 0 && C != 0) {
          // There are no solutions: ValB=0 and ValC!=0
          return;
        }

        QuadraticTermMultipleOfP(expon, factorIndex, &valA, &valB, &valC);

      } else {
        // If quadratic equation mod p
        if (!QuadraticTermNotMultipleOfP(GcdAll,
                                         expon, factorIndex,
                                         &valA, &valB, &valC)) {
          return;
        }
      }

      Increment[factorIndex] = BigIntegerToBigInt(&Q);
      // CopyBigInt(&Increment[factorIndex], &Q);
      Exponents[factorIndex] = 0;
    }

    PerformChineseRemainderTheorem(GcdAll, factors);
  }

};
}  // namespace

void SolveEquation(
    const SolutionFn &solutionCback,
    const BigInt &A, const BigInt &B,
    const BigInt &C, const BigInt &N,
    const BigInt &GcdAll, const BigInt &Nn,
    bool *interesting_coverage) {
  std::unique_ptr<QuadModLL> qmll = std::make_unique<QuadModLL>();

  qmll->SolveEquation(solutionCback, A, B, C, N, GcdAll, Nn);
  if (interesting_coverage != nullptr &&
      qmll->interesting_coverage) {
    *interesting_coverage = true;
  }
}
