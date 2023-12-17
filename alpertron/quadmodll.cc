// This file is part of Alpertron Calculators.
//
// Copyright 2017-2021 Dario Alejandro Alpern
//
// Alpertron Calculators is free software: you can redistribute it
// and/or modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation, either version 3 of
// the License, or (at your option) any later version.
//
// Alpertron Calculators is distributed in the hope that it will be
// useful, but WITHOUT ANY WARRANTY; without even the implied warranty
// of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Alpertron Calculators.
// If not, see <http://www.gnu.org/licenses/>.

#include "quadmodll.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <cassert>
#include <memory>
#include <cstdio>

#include "bignbr.h"
#include "modmult.h"
#include "bigconv.h"
#include "bignum/big.h"
#include "bignum/big-overloads.h"

#include "base/logging.h"

static constexpr bool VERBOSE = false;

// How does this differ from MultiplyLimbs?
// I think it may be implicitly mod a power of 2, as it
// only fills in limbs up to nbrLen+2, and is only
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
      // Port note: This used to do signed multiplication, but would
      // overflow.
      uint32_t factor1 = (pFactor1 + j)->x;
      uint32_t factor2 = (pFactor2 + i - j)->x;
      low += factor1 * factor2;
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
  // Note this writes at nbrLen and nbrLen+1.
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

// I think this is a fixed-width operation like the ones above, but
// it seems like overkill especially given that we only divide by
// 2?
static void DivBigNbrBy2(const limb *pDividend, limb *pQuotient, int nbrLen) {
  // Was only called with this value. Could simplify...
  static constexpr int divisor = 2;
  const limb* ptrDividend = pDividend;
  limb* ptrQuotient = pQuotient;
  unsigned int remainder = 0U;
  constexpr double dDivisor = (double)divisor;
  constexpr double dLimb = 2147483648.0;
  int nbrLenMinus1 = nbrLen - 1;
  ptrDividend += nbrLenMinus1;
  ptrQuotient += nbrLenMinus1;
  for (int ctr = nbrLenMinus1; ctr >= 0; ctr--) {
    unsigned int dividend = (remainder << BITS_PER_GROUP) +
      (unsigned int)ptrDividend->x;
    double dDividend = ((double)remainder * dLimb) + (double)ptrDividend->x;
    // quotient has correct value or 1 more.
    unsigned int quotient = (unsigned int)((dDividend / dDivisor) + 0.5);
    remainder = dividend - (quotient * (unsigned int)divisor);
    if (remainder >= (unsigned int)divisor) {
      // remainder not in range 0 <= remainder < divisor. Adjust.
      quotient--;
      remainder += (unsigned int)divisor;
    }
    ptrQuotient->x = (int)quotient;
    ptrQuotient--;
    ptrDividend--;
  }
}

static void ChSignLimbs(limb *nbr, int length) {
  int carry = 0;
  const limb *ptrEndNbr = nbr + length;
  for (limb *ptrNbr = nbr; ptrNbr < ptrEndNbr; ptrNbr++) {
    carry -= ptrNbr->x;
    ptrNbr->x = carry & MAX_INT_NBR;
    carry >>= BITS_PER_GROUP;
  }
}

// Compute sqrRoot <- sqrt(ValCOdd) mod 2^expon.
// To compute the square root, compute the inverse of sqrt,
// so only multiplications are used.
// f(x) = invsqrt(x), f_{n+1}(x) = f_n * (3 - x*f_n^2)/2
static BigInt ComputeSquareRootModPowerOf2(const BigInt &COdd,
                                           int expon, int bitsCZero,
                                           int modulus_length) {
  // Number of limbs to represent 2^expon.
  // (Not sure why we need 1+ here, but we would read outside
  // the bounds of this array if not.)
  const int expon_limbs = 1 + ((expon + BITS_PER_GROUP - 1) / BITS_PER_GROUP);
  const int codd_limbs = std::max(expon_limbs, BigIntNumLimbs(COdd));
  limb codd[codd_limbs];
  BigIntToFixedLimbs(COdd, codd_limbs, codd);

  limb sqrRoot[codd_limbs];
  // MultBigNbrInternal writes 2 past nbrLimbs.
  const int tmp_limbs = codd_limbs + 2;
  limb tmp1[tmp_limbs], tmp2[tmp_limbs];
  // Code below appears to read the n+1th limb (or more, as
  // correctBits is doubling) before writing it in each pass, at least
  // for sqrRoot.
  memset(sqrRoot, 0, codd_limbs * sizeof(limb));
  memset(tmp1, 0, tmp_limbs * sizeof(limb));
  memset(tmp2, 0, tmp_limbs * sizeof(limb));

  // First approximation to inverse of square root.
  // If value is ...0001b, the inverse of square root is ...01b.
  // If value is ...1001b, the inverse of square root is ...11b.
  sqrRoot[0].x = (((codd[0].x & 15) == 1) ? 1 : 3);
  int correctBits = 2;
  int nbrLimbs = 1;

  while (correctBits < expon) {
    // Compute f(x) = invsqrt(x), f_{n+1}(x) = f_n * (3 - x*f_n^2)/2
    correctBits *= 2;
    nbrLimbs = (correctBits / BITS_PER_GROUP) + 1;
    CHECK(nbrLimbs <= codd_limbs) << "COdd: " << COdd.ToString() <<
      "\nexpon: " << expon <<
      "\nbitsCZero: " << bitsCZero <<
      "\nmodulus_length: " << modulus_length <<
      "\ncorrectBits: " << correctBits <<
      "\nnbrLimbs: " << nbrLimbs <<
      "\nexpon_limbs: " << expon_limbs <<
      "\ncodd_limbs: " << codd_limbs;
    CHECK(nbrLimbs + 2 <= tmp_limbs) <<
      "nbrLimbs: " << nbrLimbs <<
      "\ntmp_limbs: " << tmp_limbs;
    MultBigNbrInternal(sqrRoot, sqrRoot, tmp2, nbrLimbs);
    MultBigNbrInternal(tmp2, codd, tmp1, nbrLimbs);
    ChSignLimbs(tmp1, nbrLimbs);
    int lenBytes = nbrLimbs * (int)sizeof(limb);
    (void)memset(tmp2, 0, lenBytes);
    tmp2[0].x = 3;
    AddBigNbr(tmp2, tmp1, tmp2, nbrLimbs);
    MultBigNbrInternal(tmp2, sqrRoot, tmp1, nbrLimbs);
    (void)memcpy(sqrRoot, tmp1, lenBytes);

    // PERF Too much work to divide by 2!
    DivBigNbrBy2(tmp1, sqrRoot, nbrLimbs);
    correctBits--;
  }

  // Get square root of ValCOdd from its inverse by multiplying by ValCOdd.
  MultBigNbrInternal(codd, sqrRoot, tmp1, nbrLimbs);
  int lenBytes = nbrLimbs * (int)sizeof(limb);

  // PERF avoid copy here
  (void)memcpy(sqrRoot, tmp1, lenBytes);
  // setNbrLimbs(&sqrRoot, modulus_length);

  BigInt SqrRoot =
    LimbsToBigInt(tmp1, std::min(modulus_length, lenBytes)) <<
    (bitsCZero / 2);

  // for (int ctr = 0; ctr < (bitsCZero / 2); ctr++) {
  // BigIntMultiplyBy2(&sqrRoot);
  // }

  return SqrRoot;
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

  explicit QuadModLL(const SolutionFn &f) : SolutionCallback(f) {}

  BigInt Discriminant;
  // Only used in CRT; could easily pass
  BigInt NN;

  bool sol1Invalid = false;
  bool sol2Invalid = false;
  bool interesting_coverage = false;

  const SolutionFn &SolutionCallback;

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
      CHECK(!Solution1.empty());
      CHECK(!Solution2.empty());
      CHECK(!Increment.empty());
      CHECK(!Exponents.empty());

      Tmp[0] = Increment[0] * (Exponents[0] >> 1);
      if ((Exponents[0] & 1) != 0) {
        Tmp[0] += Solution2[0];
      } else {
        Tmp[0] += Solution1[0];
      }

      // PERF: Directly
      BigInt CurrentSolution = Tmp[0];

      // PERF: We keep computing Prime^Exp. We could just do it once
      // at the beginning of the function.
      const BigInt &Prime = factors[0].first;

      BigInt Mult = BigInt::Pow(Prime, factors[0].second);

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

        const BigInt Term = BigInt::Pow(factors[T1].first, factors[T1].second);

        std::unique_ptr<MontgomeryParams> params =
          GetMontgomeryParams(Term);

        if (VERBOSE) {
          printf("T1 %d [exp %d]. Term: %s\n", T1,
                 factors[T1].second,
                 Term.ToString().c_str());
        }

        for (int E = 0; E < T1; E++) {
          const BigInt Q1 = Tmp[T1] - Tmp[E];

          // L is overwritten before use below.
          const BigInt L1 = BigInt::Pow(factors[E].first, factors[E].second);

          Tmp[T1] = BigIntModularDivision(*params, Q1, L1, Term);

          if (VERBOSE) {
            printf("T1 %d E %d. %s %s %s -> %s\n", T1, E,
                   Q1.ToString().c_str(), L1.ToString().c_str(),
                   Term.ToString().c_str(), Tmp[T1].ToString().c_str());
          }
        }

        Tmp[T1] = BigInt::CMod(Tmp[T1], Term);

        // Compute currentSolution as Tmp[T1] * Mult + currentSolution
        BigInt L2 = Tmp[T1] * Mult;
        CurrentSolution += L2;
        Mult *= Term;
      }

      BigInt VV(0);
      BigInt KK1 = 0 - GcdAll;

      // Perform loop while V < GcdAll.
      while (KK1 < 0) {
        // The solution is V*ValNn + currentSolution
        SolutionCallback(VV * NN + CurrentSolution);

        VV += 1;
        KK1 = VV - GcdAll;
      }

      for (T1 = nbrFactors - 1; T1 >= 0; T1--) {
        // term = base^exp
        const BigInt Term = BigInt::Pow(factors[T1].first,
                                        factors[T1].second);

        CHECK(T1 < (int)Solution1.size());
        CHECK(T1 < (int)Solution2.size());
        CHECK(T1 < (int)Increment.size());
        CHECK(T1 < (int)Exponents.size());

        if (Solution1[T1] == Solution2[T1]) {
          // quad_info.Solution1[T1] == quad_info.Solution2[T1]
          Exponents[T1] += 2;
        } else {
          // quad_info.Solution1[T1] != quad_info.Solution2[T1]
          Exponents[T1]++;
        }

        // L <- Exponents[T1] * quad_info.Increment[T1]
        BigInt L1 = Increment[T1] * Exponents[T1];

        // K1 <- 2 * term
        BigInt K1 = Term << 1;
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

    if (BigInt::Abs(N) != 1) {

      // ValN is not 1.
      Increment.push_back(N);
      Exponents.push_back(1);

      // Perform division using odd modulus r.
      const std::unique_ptr<MontgomeryParams> params =
        GetMontgomeryParams(N);

      // Compute ptrSolution1 as ValC / |ValB|
      BigInt sol1 = BigIntModularDivision(*params, C, B, N);

      // Compute ptrSolution1 as -ValC / ValB
      if (sol1 != 0) {
        sol1 = N - sol1;
      }

      Solution1.push_back(sol1);
      Solution2.push_back(sol1);

      factors.push_back(std::make_pair(N, 1));
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
      BigInt Pow2 = BigInt(1) << powerOf2;
      Increment.push_back(Pow2);
      Exponents.push_back(1);

      factors.push_back(std::make_pair(Pow2, 1));

      // Port note: Original code didn't advance the factor pointer nor
      // storage pointer (probably because this is not in a loop) but
      // that just seems wrong.

      const std::unique_ptr<MontgomeryParams> params =
        GetMontgomeryParamsPowerOf2(powerOf2);
      const int modulus_length = params->modulus_length;

      limb valb[modulus_length], valc[modulus_length];
      limb valn[modulus_length];
      BigIntToFixedLimbs(B, modulus_length, valb);
      BigIntToFixedLimbs(C, modulus_length, valc);
      BigIntToFixedLimbs(N, modulus_length, valn);

      // Port note: This used to do this on ptrSolution1 in place.
      // ptrSolution1 <- 1 / |ValB|
      limb inv[modulus_length];
      ComputeInversePower2(valb, inv, modulus_length);
      // Compute ptrSolution1 as |ValC| / |ValB|
      ModMult(*params, inv, valc, inv);

      // Compute ptrSolution1 as -ValC / ValB
      if (BigInt::Sign(B) == BigInt::Sign(C)) {
        limb vala[modulus_length];
        BigIntToFixedLimbs(A, modulus_length, vala);
        // Beware: SubtractBigNbr is mod modulus_length words; it
        // drops the carry past that.
        SubtractBigNbr(vala, inv, inv, modulus_length);
      }

      // Discard bits outside number in most significant limb.
      inv[modulus_length - 1].x &=
        (1 << (powerOf2 % BITS_PER_GROUP)) - 1;

      BigInt sol1 = LimbsToBigInt(inv, modulus_length);
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

  // Quadr, Linear, Const are arguments.
  // Find quadratic solution of
  //   Quadr*x^2 + Linear*x + Const = 0 (mod 2^expon)
  // when Quadr is even and Linear is odd. In this case there is a
  // unique solution.
  BigInt FindQuadraticSolution(BigInt Quadr,
                               BigInt Linear,
                               BigInt Const,
                               int exponent) {

    // Same calculation from BigIntPowerOf2.
    const int num_limbs = (exponent / BITS_PER_GROUP) + 1;
    int expon = exponent;

    // Enough to store numbers up to 2^expon.
    limb solution[num_limbs];
    memset(solution, 0, num_limbs * sizeof(limb));

    // Together, bitmask and limb_idx basically indicate the bit that
    // we're working on.
    uint32_t bitMask = 1;
    int limb_idx = 0;
    while (expon > 0) {
      expon--;

      // Bitmask: Mask <- 2^expon -1
      BigInt Mask = (BigInt(1) << expon) - 1;

      if (Const.IsOdd()) {
        // Const is odd.
        solution[limb_idx].x |= bitMask;
        // Compute Const as Quadr/2 + floor(Linear/2) + floor(Const/2) + 1
        if (Const < 0) {
          Const -= 1;
        }
        // floor(Const/2) + 1
        Const = (Const >> 1) + 1;

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

        // Linear <- 2*Quadr + Linear and Quadr <- 2*Quadr.
        Quadr <<= 1;

        Linear += Quadr;
        // Reduce mod 2^expon
        Linear &= Mask;
      } else {
        // Const is even.
        // Const/2
        Const >>= 1;
        Quadr <<= 1;
      }

      // Reduce mod 2^expon
      Const &= Mask;
      Quadr &= Mask;

      // Port note: This used to use signed int, but that's
      // undefined behavior.
      static_assert(sizeof (limb) == 4);
      bitMask <<= 1;
      if (bitMask & 0x80000000) {
        // printf("bitmaskoverflow coverage\n");
        bitMask = 1;
        limb_idx++;
      }
    }

    return LimbsToBigInt(solution, num_limbs);
  }

  // Solve Ax^2 + Bx + C = 0 (mod 2^expon).
  // If a solution is found, writes to Solution(1,2)[factorIndex]
  // and returns true.
  bool SolveQuadraticEqModPowerOf2(
      int exponent, int factorIndex,
      const BigInt &A, const BigInt &B, const BigInt &C) {

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
    int bitsAZero = BigInt::BitwiseCtz(A);
    const BigInt AOdd = A >> bitsAZero;
    if (A == 0) bitsAZero = BITS_PER_GROUP;

    int bitsBZero = BigInt::BitwiseCtz(B);
    const BigInt BOdd = B >> bitsBZero;
    if (B == 0) bitsBZero = BITS_PER_GROUP;

    int bitsCZero = BigInt::BitwiseCtz(C);
    // const BigInt COdd = C >> bitsCZero;
    if (C == 0) bitsCZero = BITS_PER_GROUP;

    if (bitsAZero > 0 && bitsBZero > 0 && bitsCZero > 0) {
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

    if (bitsAZero == 0 && bitsBZero > 0) {
      // The solution in this case requires square root.
      // compute s = ((b/2)^2 - a*c)/a^2, q = odd part of s,
      // r = maximum exponent of power of 2 that divides s.

      BigInt Tmp1 = B >> 1;

      // (b/2)^2
      Tmp1 *= Tmp1;

      // (b/2)^2 - a*c
      Tmp1 -= A * C;

      const BigInt Mask = (BigInt(1) << expon) - 1;

      // Port note: Original code overwrote valcodd.
      // (b/2) - a*c mod 2^n
      BigInt C2 = Tmp1 & Mask;

      int modulus_length = BigIntNumLimbs(Mask);

      // Port note: The code used to explicitly pad out AOdd (and the
      // output?) if smaller than modulus_length, but the BigInt version
      // does its own padding internally.
      BigInt Tmp2 = GetInversePower2(AOdd, modulus_length);

      // ((b/2) - a*c)/a mod 2^n
      C2 *= Tmp2;
      C2 &= Mask;

      // s = ((b/2) - a*c)/a^2 mod 2^n
      C2 *= Tmp2;
      C2 &= Mask;

      BigInt SqrRoot;
      if (C2 == 0) {
        // s = 0, so its square root is also zero.
        SqrRoot = BigInt(0);
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
          SqrRoot = BigInt((bitsCZero > 0) ? 0 : 1);
        } else {
          // Compute sqrRoot as the square root of ValCOdd.
          expon -= bitsCZero / 2;
          SqrRoot =
            ComputeSquareRootModPowerOf2(C2, expon, bitsCZero, modulus_length);
          expon--;
          if (expon == (bitsCZero / 2)) {
            expon++;
          }
        }
      }

      // x = sqrRoot - b/2a.
      {
        // New mask for exponent, which was modified above.
        const BigInt Mask = (BigInt(1) << expon) - 1;
        const int modulus_length = BigIntNumLimbs(Mask);

        BigInt Tmp2 = GetInversePower2(
            AOdd,
            modulus_length);

        // b/2
        BigInt Tmp1 = B >> 1;
        // BigIntDivideBy2(&tmp1);

        // b/2a
        Tmp1 *= Tmp2;

        // -b/2a
        Tmp1 = -std::move(Tmp1);

        // -b/2a mod 2^expon
        Tmp1 &= Mask;
        Tmp2 = Tmp1 + SqrRoot;

        Solution1[factorIndex] = Tmp2 & Mask;
        Solution2[factorIndex] = (Tmp1 - SqrRoot) & Mask;
      }

    } else if (bitsAZero == 0 && bitsBZero == 0) {
      BigInt A2 = A << 1;
      Solution1[factorIndex] =
        FindQuadraticSolution(A2,
                              B,
                              C >> 1,
                              expon - 1) << 1;

      Solution2[factorIndex] =
        (FindQuadraticSolution(A2,
                               A2 + B,
                               (A + B + C) >> 1,
                               expon - 1) << 1) + 1;

    } else {

      Solution1[factorIndex] =
        FindQuadraticSolution(A,
                              B,
                              C,
                              expon);
      sol2Invalid = true;
    }

    // Store increment.
    Increment[factorIndex] = BigInt(1) << expon;
    return true;
  }

  static
  BigInt GetSqrtDisc(const MontgomeryParams &params,
                     const BigInt &Base,
                     const BigInt &Prime) {

    const int NumberLengthBytes =
      params.modulus_length * (int)sizeof(limb);

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

      limb* toConvert = nullptr;
      // Convert discriminant to Montgomery notation.
      BigIntToFixedLimbs(Base, params.modulus_length, aux5);
      // u
      ModMult(params, aux5, params.R2.data(), aux6);
      if ((Prime & 7) == 5) {
        // prime mod 8 = 5: use Atkin's method for modular square roots.
        // Step 1. v <- (2u)^((p-5)/8) mod p
        // Step 2. i <- (2uv^2) mod p
        // Step 3. square root of u <- uv (i-1)
        // Step 1.
        // Q <- (prime-5)/8.
        const BigInt Q = (Prime - 5) >> 3;

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
        SubtBigNbrModN(aux9, params.R1.data(), aux9,
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

        int r = e;

        const BigInt KK1 = (QQ - 1) >> 1;

        // x
        ModPow(params, aux6, KK1, aux7);
        // v
        ModMult(params, aux6, aux7, aux8);
        // w
        ModMult(params, aux8, aux7, aux9);

        // Step 5
        while (memcmp(aux9, params.R1.data(),
                      NumberLengthBytes) != 0) {

          limb aux10[params.modulus_length];
          // Step 6
          int k = 0;
          (void)memcpy(aux10, aux9, NumberLengthBytes);
          do {
            k++;
            ModMult(params, aux10, aux10, aux10);
          } while (memcmp(aux10, params.R1.data(),
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
                                      const BigInt &Prime,
                                      int nbrBitsSquareRoot) {
    const std::unique_ptr<MontgomeryParams> params =
      GetMontgomeryParams(Prime);

    const BigInt SqrtDisc = GetSqrtDisc(*params, Base, Prime);

    // Obtain inverse of square root stored in SqrtDisc (mod prime).
    // PERF: Maybe can use BigInt::ModInv here, though not if
    // we need to preserve montgomery form?
    BigInt SqrRoot =
      BigIntModularDivision(*params, BigInt(1), SqrtDisc, Prime);

    int correctBits = 1;

    BigInt Q = Prime;

    // Obtain nbrBitsSquareRoot correct digits of inverse square root.
    while (correctBits < nbrBitsSquareRoot) {
      // Compute f(x) = invsqrt(x), f_{n+1}(x) = f_n * (3 - x*f_n^2)/2
      correctBits *= 2;
      // Square Q.
      Q = Q * Q;

      BigInt Tmp1 = SqrRoot * SqrRoot;
      BigInt Tmp2 = Tmp1 % Q;

      Tmp1 = Discriminant * Tmp2;

      Tmp2 = Tmp1 % Q;
      Tmp2 = 3 - Tmp2;
      Tmp1 = Tmp2 * SqrRoot;

      if (Tmp1.IsOdd()) {
        Tmp1 += Q;
      }

      Tmp1 >>= 1;
      SqrRoot = Tmp1 % Q;
    }

    if (SqrRoot < 0) {
      SqrRoot += Q;
    }

    // Get square root of discriminant from its inverse by multiplying
    // by discriminant.
    SqrRoot *= Discriminant;
    SqrRoot %= Q;

    // printf("SqrRoot: %s\n", SqrRoot.ToString().c_str());
    return SqrRoot;
  }

  // Solve Ax^2 + Bx + C = 0 (mod p^expon).
  bool SolveQuadraticEqModPowerOfP(
      int expon, int factorIndex,
      const BigInt &Prime,
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

    // Get maximum power of prime which divides discriminant.
    int deltaZeros;
    if (Discriminant == 0) {
      // Discriminant is zero.
      deltaZeros = expon;
    } else {
      // Discriminant is not zero.
      deltaZeros = 0;
      while (BigInt::DivisibleBy(Discriminant, Prime)) {
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
      const std::unique_ptr<MontgomeryParams> params =
        GetMontgomeryParams(Tmp1);

      // PERF: is 1/aodd modular inverse? Faster?
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

      BigInt Tmp = Discriminant % Prime;
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
      SqrRoot = ComputeSquareRootModPowerOfP(
          Tmp, Prime, nbrBitsSquareRoot);

      // Multiply square root of discriminant by prime^deltaZeros.
      for (int ctr = 0; ctr < deltaZeros; ctr++) {
        SqrRoot *= Prime;
      }
    }

    int correctBits = expon - deltaZeros;

    // Compute increment.
    // Q <- prime^correctBits
    BigInt Q = BigInt::Pow(Prime, correctBits);

    Tmp1 = B + SqrRoot;

    for (int ctr = 0; ctr < bitsAZero; ctr++) {
      BigInt Tmp2 = Tmp1 % Prime;
      if (Tmp2 != 0) {
        // Cannot divide by prime, so go out.
        sol1Invalid = true;
        break;
      }
      Tmp1 = BigInt::DivExact(Tmp1, Prime);
    }

    Solution1[factorIndex] = BigInt::CMod(Tmp1 * AOdd, Q);

    if (Solution1[factorIndex] < 0) {
      Solution1[factorIndex] += Q;
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

    Solution2[factorIndex] = BigInt::CMod(Tmp1 * AOdd, Q);

    if (Solution2[factorIndex] < 0) {
      Solution2[factorIndex] += Q;
    }

    Increment[factorIndex] = Q;
    return true;
  }

  void QuadraticTermMultipleOfP(
      int expon, int factorIndex,
      const BigInt &Prime,
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

    const std::unique_ptr<MontgomeryParams> params =
      GetMontgomeryParams(Prime);

    BigInt TmpSolution =
      -BigIntModularDivision(*params, C, B, Prime);

    if (TmpSolution < 0) {
      TmpSolution += Prime;
    }

    BigInt Q;
    for (int currentExpon = 2; currentExpon < (2 * expon); currentExpon *= 2) {
      BigInt VV = BigInt::Pow(Prime, currentExpon);
      // Q <- a*x_n + b
      Q = A * TmpSolution + B;

      BigInt L = Q;
      Q %= VV;
      // a*x_n^2 + b*x_n
      Q *= TmpSolution;
      // a*x_n^2 + b*x_n + c
      Q += C;
      // Numerator.
      Q %= VV;
      // 2*a*x_n
      L <<= 1;
      // 2*a*x_n + b
      L += B;
      // Denominator
      L %= VV;

      const std::unique_ptr<MontgomeryParams> params =
        GetMontgomeryParams(VV);
      BigInt Aux =
        BigIntModularDivision(*params, Q, L, VV);
      TmpSolution -= Aux;
      TmpSolution %= VV;

      if (TmpSolution < 0) {
        TmpSolution += VV;
      }
    }

    BigInt TmpSol1 = TmpSolution % BigInt::Pow(Prime, expon);

    Solution1[factorIndex] = TmpSol1;
    Solution2[factorIndex] = Solution1[factorIndex];
    Increment[factorIndex] = Q;
  }

  // If solutions found, writes normalized solutions at factorIndex
  // and returns true.
  bool QuadraticTermNotMultipleOfP(
      const BigInt &Prime,
      const BigInt &GcdAll,
      int expon, int factorIndex,
      const BigInt &A,
      const BigInt &B,
      const BigInt &C) {

    sol1Invalid = false;
    sol2Invalid = false;
    // Compute discriminant = B^2 - 4*A*C.
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
                                              Prime, A, B);
    }

    if (!solutions || (sol1Invalid && sol2Invalid)) {
      // Both solutions are invalid. Go out.
      return false;
    }

    if (sol1Invalid) {
      // Solution1 is invalid. Overwrite it with Solution2.
      Solution1[factorIndex] = Solution2[factorIndex];
    } else if (sol2Invalid) {
      // Solution2 is invalid. Overwrite it with Solution1.
      Solution2[factorIndex] = Solution1[factorIndex];
    } else {
      // Nothing to do.
    }

    if (Solution2[factorIndex] < Solution1[factorIndex]) {
      std::swap(Solution1[factorIndex], Solution2[factorIndex]);
    }

    return true;
  }

  // Solve Ax^2 + Bx + C = 0 (mod N).
  void SolveEquation(
      const BigInt &A, const BigInt &B,
      const BigInt &C, const BigInt &N,
      const BigInt &GcdAll, const BigInt &NN_arg) {

    NN = NN_arg;

    if (BigInt::DivisibleBy(A, N)) {
      // Linear equation.
      SolveModularLinearEquation(GcdAll, A, B, C, N);
      return;
    }

    // PERF: Original code would cache factorization of N. We seem
    // to have the factorization for the first call, so maybe we
    // could pass it from quad.
    //
    // For the x^2 + y^2 = z case, it needs the factors of z. So
    // being able to pass these explicitly would be useful for the
    // sos "ways" usage.
    std::vector<std::pair<BigInt, int>> factors = BigIntFactor(N);

    const int nbrFactors = factors.size();

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


      if (Prime != 2 &&
          BigInt::DivisibleBy(A, Prime)) {
        // ValA multiple of prime means a linear equation mod prime.
        // Also prime is not 2.
        if (B == 0 && C != 0) {
          // There are no solutions: ValB=0 and ValC!=0
          return;
        }

        QuadraticTermMultipleOfP(expon, factorIndex,
                                 Prime, A, B, C);

      } else {
        // If quadratic equation mod p
        if (!QuadraticTermNotMultipleOfP(Prime, GcdAll,
                                         expon, factorIndex,
                                         A, B, C)) {
          return;
        }
      }

      // Port note: This used to set the increment via the value
      // residing in Q, but now we set that in each branch explicitly
      // along with the solution. Probably could be setting Exponents
      // there too?
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
  std::unique_ptr<QuadModLL> qmll =
    std::make_unique<QuadModLL>(solutionCback);

  qmll->SolveEquation(A, B, C, N, GcdAll, Nn);

  if (interesting_coverage != nullptr &&
      qmll->interesting_coverage) {
    *interesting_coverage = true;
  }
}
