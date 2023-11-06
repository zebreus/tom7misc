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
  // BigInteger sqrRoot;
  // BigInteger ValBOdd;
  // BigInteger ValCOdd;
  BigInteger SqrtDisc;
  // BigInteger K1;
  // BigInteger L;
  BigInteger Q;
  // BigInteger V;
  bool sol1Invalid = false;
  bool sol2Invalid = false;
  bool interesting_coverage = false;

  // These are probably all temporaries that could be locals.
  // BigInteger Aux0;
  // BigInteger Aux1;
  // BigInteger Aux2;

  SolutionFn Solution;

  // Were globals NumberLength and TestNbr
  // int modulus_length = 0;
  // limb TheModulus[MAX_LEN] = {};

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
        // XXX not necessary now
        BigIntToBigInteger(Prime, &prime);
        // const int modulus_length = prime.nbrLimbs;
        // const int NumberLengthBytes = modulus_length * (int)sizeof(limb);
        // (void)memcpy(TheModulus, prime.limbs, NumberLengthBytes);
        // TheModulus[modulus_length].x = 0;
        std::unique_ptr<MontgomeryParams> params =
          GetMontgomeryParams(Prime);

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
      // BigIntToBigInteger(VV, &V);

      for (T1 = nbrFactors - 1; T1 >= 0; T1--) {
        // "prime" = pow(base, exp)
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
                                  const BigInt &A,
                                  const BigInt &B,
                                  const BigInt &C,
                                  BigInt N) {
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
    BigInt Tmp = BigInt::GCD(B, N);
    if (Tmp != 1) {
      // ValB and ValN are not coprime. Go out.
      return;
    }

    // Calculate z <- -ValC / ValB (mod ValN)
    // Modular division routines used work for power of 2 or odd numbers.
    // This requires to compute the quotient in two steps.
    // N = r*2^k (r = odd)

    CHECK(N != 0);
    int powerOf2 = BigInt::BitwiseCtz(N);
    N >>= powerOf2;
    // DivideBigNbrByMaxPowerOf2(&powerOf2, pValN->limbs, &pValN->nbrLimbs);

    if (BigInt::Abs(N) != 1) {

      // ValN is not 1.
      Increment.push_back(N);
      // CopyBigInt(&Increment[solutionNbr], pValN);
      Exponents.push_back(1);

      // BigInteger ValN;
      // XXX unnecessary?
      // BigIntToBigInteger(N, &ValN);
      // int modulus_length = ValN.nbrLimbs;
      // int NumberLengthBytes = modulus_length * (int)sizeof(limb);
      // (void)memcpy(TheModulus, ValN.limbs, NumberLengthBytes);
      // TheModulus[modulus_length].x = 0;
      // Perform division using odd modulus r.
      const std::unique_ptr<MontgomeryParams> params =
        GetMontgomeryParams(N);

      // Compute ptrSolution1 as ValC / |ValB|
      BigInt sol1 = BigIntModularDivision(*params, C, B, N);

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


    BigInteger ValA, ValB, ValC, ValN;
    BigIntToBigInteger(A, &ValA);
    BigIntToBigInteger(B, &ValB);
    BigIntToBigInteger(C, &ValC);
    BigIntToBigInteger(N, &ValN);

    // Perform division using power of 2.
    if (powerOf2 > 0) {

      // Port note: This used to set ptrSolution1 to the power of 2,
      // I think just as a temporary?
      BigInt Pow2 = BigInt(1) << powerOf2;
      // BigInteger pow2;
      // BigIntPowerOf2(&pow2, powerOf2);
      // BigIntPowerOf2(ptrSolution1, powerOf2);
      Increment.push_back(Pow2);
      // CopyBigInt(&Increment[solutionNbr], ptrSolution1);
      Exponents.push_back(1);
      // Exponents[solutionNbr] = 1;

      factors.push_back(std::make_pair(Pow2, 1));
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

      const std::unique_ptr<MontgomeryParams> params =
        GetMontgomeryParamsPowerOf2(powerOf2);
      const int modulus_length = params->modulus_length;

      // Port note: This used to do this on ptrSolution1 in place.
      // ptrSolution1 <- 1 / |ValB|
      BigInteger inv;
      ComputeInversePower2(ValB.limbs, inv.limbs, modulus_length);
      // ComputeInversePower2(pValB->limbs, ptrSolution1->limbs,
      //  modulus_length);
      // Compute ptrSolution1 as |ValC| / |ValB|
      ModMult(*params,
              inv.limbs, ValC.limbs,
              inv.limbs);

      int NumberLengthBytes = modulus_length * (int)sizeof(int);
      // Compute ptrSolution1 as -ValC / ValB
      if (ValB.sign == ValC.sign) {
        (void)memset(ValA.limbs, 0, NumberLengthBytes);
        // Beware: SubtractBigNbr is mod modulus_length words; it
        // drops the carry past that.
        SubtractBigNbr(ValA.limbs, inv.limbs,
                       inv.limbs, modulus_length);
      }

      // Discard bits outside number in most significant limb.
      inv.limbs[modulus_length - 1].x &=
        (1 << (powerOf2 % BITS_PER_GROUP)) - 1;
      inv.nbrLimbs = modulus_length;
      inv.sign = SIGN_POSITIVE;

      BigInt sol1 = BigIntegerToBigInt(&inv);
      Solution1.push_back(sol1);
      Solution2.push_back(sol1);

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

  // How does this differ from MultiplyLimbs?
  // I think it may be implicitly mod a power of 2, as it
  // only fills in limbs up to nbrLen+1, and is only
  // used in ComputeSquareRootModPowerOf2. AddBigNbr and SubtractBigNbr
  // had similar behavior. It may also be approximate (double math)!
  static void MultBigNbrInternal(
      const limb *pFactor1, const limb *pFactor2,
      limb *pProd, int nbrLen) {
    limb* ptrProd = pProd;
    static constexpr double dRangeLimb = (double)(1U << BITS_PER_GROUP);
    static constexpr double dInvRangeLimb = 1.0 / dRangeLimb;
    int low = 0;
    double dAccumulator = 0.0;
    for (int i = 0; i < nbrLen; i++) {
      for (int j = 0; j <= i; j++) {
        int factor1 = (pFactor1 + j)->x;
        int factor2 = (pFactor2 + i - j)->x;
        low += factor1*factor2;
        dAccumulator += (double)factor1 * (double)factor2;
      }
      low &= MAX_INT_NBR;    // Trim extra bits.
      ptrProd->x = low;
      ptrProd++;

      // Subtract or add 0x20000000 so the multiplication by dVal is not
      // nearly an integer. In that case, there would be an error of +/- 1.
      if (low < HALF_INT_RANGE) {
        dAccumulator =
          floor((dAccumulator + (double)FOURTH_INT_RANGE)*dInvRangeLimb);
      } else {
        dAccumulator =
          floor((dAccumulator - (double)FOURTH_INT_RANGE)*dInvRangeLimb);
      }
      low = (int)(dAccumulator - floor(dAccumulator * dInvRangeLimb) *
                  dRangeLimb);
    }
    ptrProd->x = low;
    (ptrProd+1)->x = (int)floor(dAccumulator/dRangeLimb);
  }

  // Also appears to be addition modulo nbrLen words, since it drops
  // the carry when it gets to the end.
  static void AddBigNbr(const limb *pNbr1, const limb *pNbr2,
                        limb *pSum, int nbrLen) {
    unsigned int carry = 0U;
    const limb *ptrNbr1 = pNbr1;
    const limb *ptrNbr2 = pNbr2;
    const limb *ptrEndSum = pSum + nbrLen;
    for (limb *ptrSum = pSum; ptrSum < ptrEndSum; ptrSum++) {
      carry = (carry >> BITS_PER_GROUP) +
        (unsigned int)ptrNbr1->x +
        (unsigned int)ptrNbr2->x;
      const unsigned int tmp = carry & MAX_INT_NBR_U;
      ptrSum->x = (int)tmp;
      ptrNbr1++;
      ptrNbr2++;
    }
  }

  // Compute sqrRoot <- sqrt(ValCOdd) mod 2^expon.
  // To compute the square root, compute the inverse of sqrt,
  // so only multiplications are used.
  // f(x) = invsqrt(x), f_{n+1}(x) = f_n * (3 - x*f_n^2)/2
  static BigInt ComputeSquareRootModPowerOf2(const BigInt &COdd,
                                             int expon, int bitsCZero,
                                             int modulus_length) {
    BigInteger ValCOdd;
    BigIntToBigInteger(COdd, &ValCOdd);

    BigInteger sqrRoot;
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
      MultBigNbrInternal(sqrRoot.limbs, sqrRoot.limbs, tmp2.limbs, nbrLimbs);
      MultBigNbrInternal(tmp2.limbs, ValCOdd.limbs, tmp1.limbs, nbrLimbs);
      ChSignLimbs(tmp1.limbs, nbrLimbs);
      int lenBytes = nbrLimbs * (int)sizeof(limb);
      (void)memset(tmp2.limbs, 0, lenBytes);
      tmp2.limbs[0].x = 3;
      AddBigNbr(tmp2.limbs, tmp1.limbs, tmp2.limbs, nbrLimbs);
      MultBigNbrInternal(tmp2.limbs, sqrRoot.limbs, tmp1.limbs, nbrLimbs);
      (void)memcpy(sqrRoot.limbs, tmp1.limbs, lenBytes);

      // XXX Too much work to divide by 2!
      // Also, the only use of this function.
      DivBigNbrByInt(tmp1.limbs, 2, sqrRoot.limbs, nbrLimbs);
      correctBits--;
    }

    // Get square root of ValCOdd from its inverse by multiplying by ValCOdd.
    MultBigNbrInternal(ValCOdd.limbs, sqrRoot.limbs, tmp1.limbs, nbrLimbs);
    int lenBytes = nbrLimbs * (int)sizeof(limb);

    // PERF avoid copy here
    (void)memcpy(sqrRoot.limbs, tmp1.limbs, lenBytes);
    setNbrLimbs(&sqrRoot, modulus_length);

    BigInt SqrRoot = BigIntegerToBigInt(&sqrRoot) << (bitsCZero / 2);

    // for (int ctr = 0; ctr < (bitsCZero / 2); ctr++) {
    // BigIntMultiplyBy2(&sqrRoot);
    // }

    return SqrRoot;
  }

  // PERF? rewrite FQS to work directly on BigInt
  void FindQuadraticSolution(BigInt* pSolution,
                             const BigInt &Quadr,
                             const BigInt &Linear,
                             const BigInt &Const,
                             int exponent) {
    BigInteger sol;
    FindQuadraticSolutionInternal(&sol, Quadr, Linear, Const, exponent);
    *pSolution = BigIntegerToBigInt(&sol);
  }

  // Quadr, Linear, Const are arguments.
  // Find quadratic solution of
  //   Quadr*x^2 + Linear*x + Const = 0 (mod 2^expon)
  // when Quadr is even and Linear is odd. In this case there is a
  // unique solution.
  void FindQuadraticSolutionInternal(BigInteger* pSolution,
                                     BigInt Quadr,
                                     BigInt Linear,
                                     BigInt Const,
                                     int exponent) {
    // PERF we could just allocate the necessary size here, then
    // convert to BigInt and return
    BigInteger Aux0;
    int expon = exponent;
    int bitMask = 1;
    limb* ptrSolution = pSolution->limbs;
    BigIntPowerOf2(&Aux0, expon);
    int bytesLen = Aux0.nbrLimbs * (int)sizeof(limb);
    (void)memset(ptrSolution, 0, bytesLen);
    while (expon > 0) {
      expon--;

      // Bitmask: Mask <- 2^expon -1
      BigInt Mask = (BigInt(1) << expon) - 1;
      // BigIntPowerOf2(&Mask, expon);
      // addbigint(&Mask, -1);

      if (Const.IsOdd()) {
        // Const is odd.
        ptrSolution->x |= bitMask;
        // Compute Const as Quadr/2 + floor(Linear/2) + floor(Const/2) + 1
        if (Const < 0) {
          Const -= 1;
          // addbigint(Const_, -1);
        }
        // floor(Const/2) + 1
        Const = (Const >> 1) + 1;
        // BigIntDivideBy2(Const_);
        // addbigint(Const_, 1);

        BigInt Aux1 = Linear;
        if (Aux1 < 0) {
          Aux1 -= 1;
        }
        // floor(Linear/2)
        Aux1 >>= 1;
        // BigIntDivideBy2(&Aux1);

        Const += Aux1;

        // BigIntAdd(Const_, &Aux1, Const_);

        Const += (Quadr >> 1);
        // CopyBigInt(&Aux1, Quadr_);
        // BigIntDivideBy2(&Aux1);            // Quadr/2
        // BigIntAdd(Const_, &Aux1, Const_);

        // Linear <- 2*Quadr + Linear and Quadr <- 2*Quadr.
        Quadr <<= 1;
        // BigIntMultiplyBy2(Quadr_);         // Quadr*2

        Linear += Quadr;
        // Reduce mod 2^expon
        Linear &= Mask;
        // BigIntAdd(Linear_, Quadr_, Linear_);
        // BigIntAnd(Linear_, &Mask, Linear_);
      } else {
        // Const is even.
        // Const/2
        Const >>= 1;
        // BigIntDivideBy2(Const_);
        // Quadr*2
        Quadr <<= 1;
      }

      // Reduce mod 2^expon
      Const &= Mask;
      // BigIntAnd(Const_, &Mask, Const_);
      Quadr &= Mask;
      // BigIntAnd(Quadr_, &Mask, Quadr_);

      // XXX this seems to assume two things that aren't guaranteed:
      //   int is 32 bits
      //   signed multiplication is twos complement
      static_assert(sizeof (int) == 4);
      bitMask *= 2;
      if (bitMask < 0) {
        printf("bitmaskoverflow coverage\n");
        interesting_coverage = true;
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
      const BigInt &A, const BigInt &B, const BigInt &C) {

    BigInteger ValA, ValB, ValC;
    BigIntToBigInteger(A, &ValA);
    BigIntToBigInteger(B, &ValB);
    BigIntToBigInteger(C, &ValC);

    int expon = exponent;

    // ax^2 + bx + c = 0 (mod 2^expon)
    // This follows the paper Complete solving the quadratic equation mod 2^n
    // of Dehnavi, Shamsabad and Rishakani.
    // Get odd part of A, B and C and number of bits to zero.
    //
    // Port note: The original code's DivideBigNbrByMaxPowerOf2 gives
    // the "power" as 31 (the number of bits in a limb) when the input
    // is zero. This matches e.g. std::countr_zero but doesn't really
    // make mathematical sense. The code below does apparently rely
    // on the result being nonzero, though.
    BigInt AOdd = A;
    int bitsAZero = BigInt::BitwiseCtz(AOdd);
    AOdd >>= bitsAZero;
    if (A == 0) bitsAZero = BITS_PER_GROUP;

    BigInt BOdd = B;
    int bitsBZero = BigInt::BitwiseCtz(BOdd);
    BOdd >>= bitsBZero;
    if (B == 0) bitsBZero = BITS_PER_GROUP;

    BigInt COdd = C;
    int bitsCZero = BigInt::BitwiseCtz(COdd);
    COdd >>= bitsCZero;
    if (C == 0) bitsCZero = BITS_PER_GROUP;

    BigInteger ValAOdd, ValBOdd;
    BigIntToBigInteger(AOdd, &ValAOdd);
    BigIntToBigInteger(BOdd, &ValBOdd);

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

    if ((bitsAZero == 0 && bitsBZero == 0 && bitsCZero == 0) ||
        (bitsAZero > 0 && bitsBZero > 0 && bitsCZero == 0)) {
      return false;   // No solutions, so go out.
    }

    BigInteger tmp1, tmp2;
    if (bitsAZero == 0 && bitsBZero > 0) {
      // The solution in this case requires square root.
      // compute s = ((b/2)^2 - a*c)/a^2, q = odd part of s,
      // r = maximum exponent of power of 2 that divides s.
      CopyBigInt(&tmp1, &ValB);
      BigIntDivideBy2(&tmp1);
      (void)BigIntMultiply(&tmp1, &tmp1, &tmp1);  // (b/2)^2
      (void)BigIntMultiply(&ValA, &ValC, &tmp2);  // a*c
      BigIntSubt(&tmp1, &tmp2, &tmp1);      // (b/2)^2 - a*c

      BigInteger mask;
      BigIntPowerOf2(&mask, expon);
      addbigint(&mask, -1);

      // Port note: Original code overwrote valcodd.
      // (b/2) - a*c mod 2^n
      BigInt C2 = BigIntegerToBigInt(&tmp1) & BigIntegerToBigInt(&mask);

      int modulus_length = mask.nbrLimbs;

      if (modulus_length > ValAOdd.nbrLimbs) {
        int lenBytes = (modulus_length - ValAOdd.nbrLimbs) * (int)sizeof(int);
        (void)memset(&ValAOdd.limbs[ValAOdd.nbrLimbs], 0, lenBytes);
      }

      if (modulus_length > tmp2.nbrLimbs) {
        int lenBytes = (modulus_length - tmp2.nbrLimbs) * (int)sizeof(int);
        (void)memset(&tmp2.limbs[tmp2.nbrLimbs], 0, lenBytes);
      }

      ComputeInversePower2(ValAOdd.limbs, tmp2.limbs, modulus_length);

      // ((b/2) - a*c)/a mod 2^n
      C2 *= BigIntegerToBigInt(&tmp2);
      C2 &= BigIntegerToBigInt(&mask);

      // s = ((b/2) - a*c)/a^2 mod 2^n
      C2 *= BigIntegerToBigInt(&tmp2);
      C2 &= BigIntegerToBigInt(&mask);

      BigInteger sqrRoot;
      if (C2 == 0) {
        // s = 0, so its square root is also zero.
        intToBigInteger(&sqrRoot, 0);
        expon -= expon / 2;
      } else {

        bitsCZero = BigInt::BitwiseCtz(C2);
        C2 >>= bitsCZero;

        // At this moment, bitsCZero = r and ValCOdd = q.
        if ((C2 & 7) != 1 || (bitsCZero & 1)) {
          // q != 1 or p2(r) == 0, so go out.
          return false;
        }

        if (expon < 2) {
          // Modulus is 2.
          intToBigInteger(&sqrRoot, (bitsCZero > 0) ? 0 : 1);
        } else {
          // Compute sqrRoot as the square root of ValCOdd.
          expon -= bitsCZero / 2;
          BigInt SqrRoot =
            ComputeSquareRootModPowerOf2(C2, expon, bitsCZero, modulus_length);
          BigIntToBigInteger(SqrRoot, &sqrRoot);
          expon--;
          if (expon == (bitsCZero / 2)) {
            expon++;
          }
        }
      }

      // x = sqrRoot - b/2a.
      {
        BigInteger Mask;
        BigIntPowerOf2(&Mask, expon);
        addbigint(&Mask, -1);
        modulus_length = Mask.nbrLimbs;
        ComputeInversePower2(ValAOdd.limbs, tmp2.limbs, modulus_length);
        setNbrLimbs(&tmp2, modulus_length);
        CopyBigInt(&tmp1, &ValB);
        BigIntDivideBy2(&tmp1);               // b/2
        (void)BigIntMultiply(&tmp1, &tmp2, &tmp1);  // b/2a
        BigIntChSign(&tmp1);                  // -b/2a
        BigIntAnd(&tmp1, &Mask, &tmp1);         // -b/2a mod 2^expon
        BigIntAdd(&tmp1, &sqrRoot, &tmp2);

        BigInteger sol;
        BigIntAnd(&tmp2, &Mask, &sol);
        Solution1[factorIndex] = BigIntegerToBigInt(&sol);
        BigIntSubt(&tmp1, &sqrRoot, &tmp2);
        BigIntAnd(&tmp2, &Mask, &sol);
        Solution2[factorIndex] = BigIntegerToBigInt(&sol);
      }

    } else if ((bitsAZero == 0) && (bitsBZero == 0)) {
      BigInt A2 = BigIntegerToBigInt(&ValA) << 1;
      BigInt B = BigIntegerToBigInt(&ValB);

      BigInteger Linear, Const;

      // CopyBigInt(&Quadr, &ValA);
      // BigIntMultiplyBy2(&Quadr);         // 2a
      // CopyBigInt(&Linear, &ValB);        // b
      CopyBigInt(&Const, &ValC);
      BigIntDivideBy2(&Const);           // c/2
      FindQuadraticSolution(&Solution1[factorIndex],
                            A2,
                            B,
                            BigIntegerToBigInt(&Const),
                            expon - 1);

      Solution1[factorIndex] <<= 1;
      // BigIntMultiplyBy2(&Solution1[factorIndex]);

      // CopyBigInt(&Quadr, &ValA);
      // BigIntMultiplyBy2(&Quadr);         // 2a
      // BigIntAdd(&Quadr, &ValB, &Linear); // 2a+b
      CopyBigInt(&Const, &ValA);
      BigIntAdd(&Const, &ValB, &Const);
      BigIntAdd(&Const, &ValC, &Const);
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

      // CopyBigInt(&Quadr, &ValA);
      // CopyBigInt(&Linear, &ValB);
      // CopyBigInt(&Const, &ValC);
      FindQuadraticSolution(&Solution1[factorIndex],
                            BigIntegerToBigInt(&ValA),
                            BigIntegerToBigInt(&ValB),
                            BigIntegerToBigInt(&ValC),
                            expon);
      sol2Invalid = true;
    }

    // XXX how is this used? It looks to me like all
    // all other code paths write Q before using it.
    // memory corruption??
    intToBigInteger(&Q, 0x0DDBALL);
    BigIntPowerOf2(&Q, expon);         // Store increment.
    return true;
  }

  static
  BigInt GetSqrtDisc(const MontgomeryParams &params,
                     const BigInt &Base,
                     const BigInt &Prime) {

    const int NumberLengthBytes =
      params.modulus_length * (int)sizeof(limb);

    // XXX native
    BigInteger base;
    BigIntToBigInteger(Base, &base);

    limb aux5[params.modulus_length];
    limb aux6[params.modulus_length];
    limb aux7[params.modulus_length];
    limb aux8[params.modulus_length];
    limb aux9[params.modulus_length];

    if ((Prime & 3) == 3) {
      // prime mod 4 = 3
      // subtractdivide(&Q, -1, 4);   // Q <- (prime+1)/4.

      return BigIntModularPower(params, Base, (Prime + 1) >> 2);

    } else {
      // BigInteger Q;
      // BigIntToBigInteger(Prime, &Q);

      limb* toConvert = nullptr;
      // Convert discriminant to Montgomery notation.
      // XXX can convert fixed limbs from Base
      CompressLimbsBigInteger(params.modulus_length, aux5, &base);
      // u
      ModMult(params, aux5, params.MontgomeryMultR2, aux6);
      if ((Prime & 7) == 5) {
        // prime mod 8 = 5: use Atkin's method for modular square roots.
        // Step 1. v <- (2u)^((p-5)/8) mod p
        // Step 2. i <- (2uv^2) mod p
        // Step 3. square root of u <- uv (i-1)
        // Step 1.
        // Q <- (prime-5)/8.
        const BigInt Q = (Prime - 5) >> 3;
        // BigInteger Q;
        // BigIntToBigInteger((Prime - 5) >> 3, &Q);

        // subtractdivide(&Q, 5, 8);
        // 2u
        AddBigNbrModN(aux6, aux6, aux7,
                      params.modulus.data(), params.modulus_length);
        ModPow(params, aux7, Q, aux8);
        // At this moment aux7 is v in Montgomery notation.

        // Step 2.
        // v^2
        ModMult(params, aux8, aux8, aux9);
        // i
        ModMult(params, aux7, aux9, aux9);

        // Step 3.
        // i-1
        SubtBigNbrModN(aux9, params.MontgomeryMultR1, aux9,
                       params.modulus.data(), params.modulus_length);
        // v*(i-1)
        ModMult(params, aux8, aux9, aux9);
        // u*v*(i-1)
        ModMult(params, aux6, aux9, aux9);
        toConvert = aux9;
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
        BigInt QQ = Prime - 1;
        CHECK(QQ != 0);
        int e = BigInt::BitwiseCtz(QQ);
        QQ >>= e;

        // subtractdivide(&Q, 1, 1);   // Q <- (prime-1).
        // int e;
        // DivideBigNbrByMaxPowerOf2(&e, Q.limbs, &Q.nbrLimbs);

        // BigIntToBigInteger(QQ, &Q);

        // Step 2.
        int x = 1;

        {
          do {
            x++;
          } while (BigInt::Jacobi(BigInt(x), Prime) >= 0);
        }

        // Step 3.
        // Get z <- x^q (mod p) in Montgomery notation.
        // z
        BigInt Z = ModPowBaseInt(params, x, QQ);

        // PERF We don't use aux4 (now Z) again, so this could just
        // be a substitution?
        // Step 4.
        // y
        BigIntToFixedLimbs(Z, params.modulus_length, aux5);
        // (void)memcpy(aux5, aux4, NumberLengthBytes); // y

        int r = e;


        const BigInt KK1 = (QQ - 1) >> 1;
        // BigInteger K1;
        // BigIntToBigInteger(KK1, &K1);
        // CopyBigInt(&K1, &Q);
        // subtractdivide(&K1, 1, 2);

        // x
        ModPow(params, aux6, KK1, aux7);
        // v
        ModMult(params, aux6, aux7, aux8);
        // w
        ModMult(params, aux8, aux7, aux9);

        // Step 5
        while (memcmp(aux9, params.MontgomeryMultR1,
                      NumberLengthBytes) != 0) {

          limb aux10[params.modulus_length];
          // Step 6
          int k = 0;
          (void)memcpy(aux10, aux9, NumberLengthBytes);
          do {
            k++;
            ModMult(params, aux10, aux10, aux10);
          } while (memcmp(aux10, params.MontgomeryMultR1,
                          NumberLengthBytes) != 0);

          // Step 7
          (void)memcpy(aux10, aux5, NumberLengthBytes); // d
          for (int ctr = 0; ctr < (r - k - 1); ctr++) {
            ModMult(params, aux10, aux10, aux10);
          }
          // y
          ModMult(params, aux10, aux10, aux5);
          r = k;
          // v
          ModMult(params, aux8, aux10, aux8);
          // w
          ModMult(params, aux9, aux5, aux9);
        }
        toConvert = aux8;
      }

      CHECK(toConvert == aux8 || toConvert == aux9);

      // Convert from Montgomery to standard notation.
      // Convert power to standard notation.
      (void)memset(aux7, 0, NumberLengthBytes);
      aux7[0].x = 1;
      ModMult(params, aux7, toConvert, toConvert);

      return LimbsToBigInt(toConvert, params.modulus_length);
    }
  }

  BigInt ComputeSquareRootModPowerOfP(const BigInt &Base,
                                      int nbrBitsSquareRoot) {
    // const int modulus_length = prime.nbrLimbs;
    // const int NumberLengthBytes = modulus_length * (int)sizeof(limb);
    // (void)memcpy(TheModulus, prime.limbs, NumberLengthBytes);
    // TheModulus[modulus_length].x = 0;
    const BigInt Prime = BigIntegerToBigInt(&prime);
    const std::unique_ptr<MontgomeryParams> params =
      GetMontgomeryParams(Prime);

    const BigInt SqrtDisc = GetSqrtDisc(*params, Base, Prime);

    // Obtain inverse of square root stored in SqrtDisc (mod prime).
    BigInt SqrRoot =
      BigIntModularDivision(*params,
                            BigInt(1), SqrtDisc,
                            Prime);

    int correctBits = 1;

    intToBigInteger(&Q, 0xCAFE);

    // XXX just use bigint
    BigInt Q = Prime;
    // CopyBigInt(&Q, &prime);

    // Obtain nbrBitsSquareRoot correct digits of inverse square root.
    while (correctBits < nbrBitsSquareRoot) {
      // Compute f(x) = invsqrt(x), f_{n+1}(x) = f_n * (3 - x*f_n^2)/2
      correctBits *= 2;
      Q = Q * Q;
      // (void)BigIntMultiply(&Q, &Q, &Q);           // Square Q.

      BigInt Tmp1 = SqrRoot * SqrRoot;
      // (void)BigIntMultiply(&sqrRoot, &sqrRoot, &tmp1);
      BigInt Tmp2 = Tmp1 % Q;
      // (void)BigIntRemainder(&tmp1, &Q, &tmp2);

      Tmp1 = Discriminant * Tmp2;
      // BigIntToBigInteger(Discriminant * Tmp2, &tmp1);
      // (void)BigIntMultiply(&tmp2, &discriminant, &tmp1);

      Tmp2 = Tmp1 % Q;
      // (void)BigIntRemainder(&tmp1, &Q, &tmp2);
      Tmp2 = 3 - Tmp2;
      // intToBigInteger(&tmp1, 3);
      // BigIntSubt(&tmp1, &tmp2, &tmp2);
      Tmp1 = Tmp2 * SqrRoot;
      // (void)BigIntMultiply(&tmp2, &sqrRoot, &tmp1);
      if (Tmp1.IsOdd()) {
        Tmp1 += Q;
        // BigIntAdd(&tmp1, &Q, &tmp1);
      }

      Tmp1 >>= 1;
      // BigIntDivide2(&tmp1);
      SqrRoot = Tmp1 % Q;
      // (void)BigIntRemainder(&tmp1, &Q, &sqrRoot);
    }

    if (SqrRoot < 0) {
      SqrRoot += Q;
      // BigIntAdd(&sqrRoot, &Q, &sqrRoot);
    }

    // Get square root of discriminant from its inverse by multiplying
    // by discriminant.
    SqrRoot *= Discriminant;
    // BigIntToBigInteger(Discriminant * SqrRoot, &sqrRoot);
    // (void)BigIntMultiply(&sqrRoot, &discriminant, &sqrRoot);
    // (void)BigIntRemainder(&sqrRoot, &Q, &sqrRoot);
    SqrRoot %= Q;
    // BigIntToBigInteger(SqrRoot, &sqrRoot);
    return SqrRoot;
  }

  // Solve Ax^2 + Bx + C = 0 (mod p^expon).
  bool SolveQuadraticEqModPowerOfP(
      int expon, int factorIndex,
      const BigInt &A, const BigInt &B) {

    // Number of bits of square root of discriminant to compute:
    //   expon + bits_a + 1,
    // where bits_a is the number of least significant bits of
    // a set to zero.
    // To compute the square root, compute the inverse of sqrt,
    // so only multiplications are used.
    //   f(x) = invsqrt(x), f_{n+1}(x) = f_n * (3 - x*f_n^2)/2
    // Get maximum power of prime which divides ValA.
    BigInt AOdd = A;
    int bitsAZero = 0;

    const BigInt Prime = BigIntegerToBigInt(&prime);
    const BigInt VV = BigInt::Pow(Prime, expon);

    for (;;) {
      BigInt Tmp1 = AOdd % Prime;
      if (AOdd < 0) {
        AOdd += Prime;
      }
      if (Tmp1 != 0) {
        break;
      }

      // PERF: I think this can be divexact?
      AOdd /= Prime;
      bitsAZero++;
    }
    Discriminant %= VV;

    CHECK(Prime == BigIntegerToBigInt(&prime));

    // Get maximum power of prime which divides discriminant.
    int deltaZeros;
    if (Discriminant == 0) {
      // Discriminant is zero.
      deltaZeros = expon;
    } else {
      // Discriminant is not zero.
      deltaZeros = 0;
      for (;;) {
        if (!BigInt::DivisibleBy(Discriminant, Prime)) {
          break;
        }
        Discriminant = BigInt::DivExact(Discriminant, Prime);
        deltaZeros++;
      }
    }

    if ((deltaZeros & 1) != 0 && deltaZeros < expon) {
      // If delta is of type m*prime^n where m is not multiple of prime
      // and n is odd, there is no solution, so go out.
      return false;
    }

    deltaZeros >>= 1;
    // Compute inverse of -2*A (mod prime^(expon - deltaZeros)).
    AOdd = A << 1;

    BigInt Tmp1 = BigInt::Pow(Prime, expon - deltaZeros);


    AOdd %= Tmp1;

    if (AOdd < 0) {
      // Negate 2*A
      AOdd = -std::move(AOdd);
    } else if (AOdd != 0) {
      // Negate 2*A
      AOdd = Tmp1 - AOdd;
    } else {
      // Nothing to do.
    }

    {
      // BigInteger tmp1;
      // BigIntToBigInteger(Tmp1, &tmp1);
      // int modulus_length = tmp1.nbrLimbs;
      // int NumberLengthBytes = modulus_length * (int)sizeof(limb);
      // (void)memcpy(TheModulus, tmp1.limbs, NumberLengthBytes);
      // TheModulus[modulus_length].x = 0;
      const std::unique_ptr<MontgomeryParams> params =
        GetMontgomeryParams(Tmp1);

      AOdd = BigIntModularDivision(*params, BigInt(1), AOdd, Tmp1);
    }

    BigInt SqrRoot;
    if (Discriminant == 0) {
      // Discriminant is zero.

      // Port note: Original code explicitly zeroed the buffer, but why?
      SqrRoot = BigInt{0};

    } else {
      // Discriminant is not zero.
      // Find number of digits of square root to compute.
      int nbrBitsSquareRoot = expon + bitsAZero - deltaZeros;
      const BigInt Tmp1 = BigInt::Pow(Prime, nbrBitsSquareRoot);

      Discriminant %= Tmp1;
      if (Discriminant < 0) {
        Discriminant += Tmp1;
      }

      // Port note: Not clear why the original code did this?
      // It doesn't seem to use the limbs directly.
      // if (nbrLimbs > discriminant.nbrLimbs) {
      // int lenBytes = (nbrLimbs - discriminant.nbrLimbs) * (int)sizeof(limb);
      // (void)memset(&discriminant.limbs[nbrLimbs], 0, lenBytes);
      // }

      CHECK(Prime == BigIntegerToBigInt(&prime));
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
      SqrRoot = ComputeSquareRootModPowerOfP(Tmp, nbrBitsSquareRoot);

      // Multiply by square root of discriminant by prime^deltaZeros.
      for (int ctr = 0; ctr < deltaZeros; ctr++) {
        SqrRoot *= Prime;
      }
    }

    int correctBits = expon - deltaZeros;

    // Store increment.
    intToBigInteger(&Q, 0xBABE);
    // Q <- prime^correctBits
    (void)BigIntPowerIntExp(&prime, correctBits, &Q);
    // Compute x = (b + sqrt(discriminant)) / (-2a) and
    //   x = (b - sqrt(discriminant)) / (-2a)

    Tmp1 = B + SqrRoot;
    // BigIntAdd(pValB, &sqrRoot, &tmp1);

    for (int ctr = 0; ctr < bitsAZero; ctr++) {
      BigInt Tmp2 = Tmp1 % Prime;
      if (Tmp2 != 0) {
        // Cannot divide by prime, so go out.
        sol1Invalid = true;
        break;
      }
      Tmp1 = BigInt::DivExact(Tmp1, Prime);
    }

    Solution1[factorIndex] = BigInt::CMod(Tmp1 * AOdd,
                                          BigIntegerToBigInt(&Q));

    if (Solution1[factorIndex] < 0) {
      Solution1[factorIndex] += BigIntegerToBigInt(&Q);
    }

    Tmp1 = B - SqrRoot;
    for (int ctr = 0; ctr < bitsAZero; ctr++) {
      BigInt Tmp2 = Tmp1 % Prime;

      if (Tmp2 != 0) {
        // Cannot divide by prime, so go out.
        sol2Invalid = true;
        break;
      }
      Tmp1 = BigInt::DivExact(Tmp1, Prime);
    }

    Solution2[factorIndex] = BigInt::CMod(Tmp1 * AOdd,
                                          BigIntegerToBigInt(&Q));

    if (Solution2[factorIndex] < 0) {
      Solution2[factorIndex] += BigIntegerToBigInt(&Q);
    }

    return true;
  }

  void QuadraticTermMultipleOfP(
      int expon, int factorIndex,
      const BigInt &A,
      const BigInt &B,
      const BigInt &C) {
    // Perform Newton approximation.
    // The next value of x in sequence x_{n+1} is
    //   x_n - (a*x_n^2 + b*x_n + c) / (2*a_x + b).

    // No coverage!
    printf("qtermmultp coverage\n");
    interesting_coverage = true;

    BigInt sol;

    const BigInt Prime = BigIntegerToBigInt(&prime);
    const std::unique_ptr<MontgomeryParams> params =
      GetMontgomeryParams(Prime);

    BigInt TmpSolution =
      -BigIntModularDivision(*params, C, B, Prime);

    if (TmpSolution < 0) {
      TmpSolution += Prime;
    }

    for (int currentExpon = 2; currentExpon < (2 * expon); currentExpon *= 2) {
      BigInt VV = BigInt::Pow(Prime, currentExpon);
      // (void)BigIntPowerIntExp(&prime, currentExpon, &V);
      // Q <- a*x_n + b
      BigInt QQ = A * TmpSolution + B;
        // intToBigInteger(&Q, 0xDADA);
      // (void)BigIntMultiply(pValA, &tmpSolution, &Q);
      BigInt L = BigIntegerToBigInt(&Q);
      // CopyBigInt(&L, &Q);
      // BigIntAdd(&Q, pValB, &Q);
      QQ %= VV;
      // (void)BigIntRemainder(&Q, &V, &Q);
      // a*x_n^2 + b*x_n
      QQ *= TmpSolution;
      // (void)BigIntMultiply(&Q, &tmpSolution, &Q);
      // a*x_n^2 + b*x_n + c
      QQ += C;
      // BigIntAdd(&Q, pValC, &Q);
      // Numerator.
      QQ %= VV;
      // (void)BigIntRemainder(&Q, &V, &Q);
      // 2*a*x_n
      L <<= 1;
      // MultInt(&L, &L, 2);
      // 2*a*x_n + b
      L += B;
      // BigIntAdd(&L, pValB, &L);
      // Denominator
      L %= VV;
      // (void)BigIntRemainder(&L, &V, &L);

      const std::unique_ptr<MontgomeryParams> params =
        GetMontgomeryParams(VV);
      BigInt Aux =
        BigIntModularDivision(*params, QQ, L, VV);
      TmpSolution -= Aux;
      TmpSolution %= VV;
      // BigIntSubt(&tmpSolution, &Aux1, &tmpSolution);
      // (void)BigIntRemainder(&tmpSolution, &V, &tmpSolution);

      if (TmpSolution < 0) {
        TmpSolution += VV;
        // BigIntAdd(&tmpSolution, &V, &tmpSolution);
      }
    }

    // XXX
    intToBigInteger(&Q, 0xDEFACED);

    BigInt TmpSol1 = TmpSolution % BigInt::Pow(Prime, expon);
    // (void)BigIntPowerIntExp(&prime, expon, &Q);
    // (void)BigIntRemainder(&tmpSolution, &Q, &tmpSolution);

    Solution1[factorIndex] = TmpSol1;
    Solution2[factorIndex] = Solution1[factorIndex];
    // CopyBigInt(&Solution2[factorIndex], &Solution1[factorIndex]);
  }

  // If solutions found, writes normalized solutions at factorIndex
  // and returns true.
  bool QuadraticTermNotMultipleOfP(
      const BigInt &Prime,
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
    CHECK(Prime > 0);
    if (Prime == 2) {
      // Prime p is 2
      solutions = SolveQuadraticEqModPowerOf2(expon, factorIndex,
                                              A, B, C);
    } else {
      // Prime is not 2
      solutions = SolveQuadraticEqModPowerOfP(expon, factorIndex,
                                              A, B);
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
      SolveModularLinearEquation(GcdAll, A, B, C, N);
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

    // intToBigInteger(&Q, 0);
    intToBigInteger(&Q, 0xFACADE);
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

      if (Prime != 2 &&
          BigInt::DivisibleBy(A, Prime)) {
        // ValA multiple of prime means a linear equation mod prime.
        // Also prime is not 2.
        if (B == 0 && C != 0) {
          // There are no solutions: ValB=0 and ValC!=0
          return;
        }

        QuadraticTermMultipleOfP(expon, factorIndex,
                                 BigIntegerToBigInt(&valA),
                                 BigIntegerToBigInt(&valB),
                                 BigIntegerToBigInt(&valC));

      } else {
        // If quadratic equation mod p
        if (!QuadraticTermNotMultipleOfP(Prime, GcdAll,
                                         expon, factorIndex,
                                         &valA, &valB, &valC)) {
          return;
        }
      }

      Increment[factorIndex] = BigIntegerToBigInt(&Q);
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
