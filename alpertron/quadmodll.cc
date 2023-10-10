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

#include "bignbr.h"
#include "factor.h"
#include "modmult.h"
#include "bigconv.h"

#include "base/logging.h"

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

// PERF So big!
struct QuadModLL {
  BigInteger Solution1[400];
  BigInteger Solution2[400];
  BigInteger Increment[400];
  BigInteger prime;

  int Exponents[400];

  QuadModLL() {
    // PERF: probably unnecessary, but debugging invalid
    // Solution bigint
    for (int i = 0; i < 400; i++) {
      Exponents[i] = 777;
      intToBigInteger(&Solution1[i], 888);
      intToBigInteger(&Solution2[i], 999);
      intToBigInteger(&Increment[i], 101010);
    }
    intToBigInteger(&prime, 121212);
  }

  BigInteger Quadr;
  BigInteger Linear;
  BigInteger Const;
  BigInteger sqrRoot;
  BigInteger ValAOdd;
  BigInteger ValBOdd;
  BigInteger ValCOdd;
  BigInteger discriminant;
  BigInteger SqrtDisc;
  BigInteger K1;
  BigInteger L;
  BigInteger Q;
  BigInteger V;
  BigInteger Aux[12];
  bool sol1Invalid = false;
  bool sol2Invalid = false;

  BigInteger Aux0;
  BigInteger Aux1;
  BigInteger Aux2;
  BigInteger* pGcdAll = nullptr;
  BigInteger* pValNn = nullptr;
  SolutionFn Solution;

  // Were globals NumberLength and TestNbr
  int modulus_length = 0;
  limb TheModulus[MAX_LEN] = {};

  // Use Chinese remainder theorem to obtain the solutions.
  void PerformChineseRemainderTheorem(const Factors &factors) {
    int T1;

    const int nbrFactors = factors.product.size();
    CHECK(nbrFactors > 0);
    // Dynamically allocate temporary space. We need one per factor.
    std::unique_ptr<BigInteger []> Tmp(new BigInteger[nbrFactors]);

    do {
      MultInt(&Tmp[0], &Increment[0], Exponents[0] / 2);
      if ((Exponents[0] & 1) != 0) {
        BigIntAdd(&Tmp[0], &Solution2[0], &Tmp[0]);
      } else {
        BigIntAdd(&Tmp[0], &Solution1[0], &Tmp[0]);
      }

      BigInteger currentSolution;
      CopyBigInt(&currentSolution, &Tmp[0]);
      const sFactorz *pstFactor = &factors.product[0];
      IntArray2BigInteger(modulus_length, pstFactor->array, &prime);
      BigInteger Mult;
      (void)BigIntPowerIntExp(&prime, pstFactor->multiplicity, &Mult);

      for (T1 = 1; T1 < nbrFactors; T1++) {
        pstFactor++;

        if (pstFactor->multiplicity == 0) {
          intToBigInteger(&Tmp[T1], 0);
          continue;
        }

        int expon = Exponents[T1];
        MultInt(&Tmp[T1], &Increment[T1], expon / 2);

        if ((expon & 1) != 0) {
          BigIntAdd(&Tmp[T1], &Solution2[T1], &Tmp[T1]);
        } else {
          BigIntAdd(&Tmp[T1], &Solution1[T1], &Tmp[T1]);
        }

        const int number_length = *pstFactor->array;
        modulus_length = number_length;
        IntArray2BigInteger(number_length, pstFactor->array, &prime);
        (void)BigIntPowerIntExp(&prime, pstFactor->multiplicity, &K1);
        CopyBigInt(&prime, &K1);

        for (int E = 0; E < T1; E++) {
          BigIntSubt(&Tmp[T1], &Tmp[E], &Q);
          BigInteger bigBase;
          IntArray2BigInteger(modulus_length, factors.product[E].array, &bigBase);
          (void)BigIntPowerIntExp(&bigBase, factors.product[E].multiplicity, &L);
          modulus_length = prime.nbrLimbs;
          int NumberLengthBytes = modulus_length * (int)sizeof(limb);
          (void)memcpy(TheModulus, prime.limbs, NumberLengthBytes);
          TheModulus[modulus_length].x = 0;
          MontgomeryParams params = GetMontgomeryParams(modulus_length, TheModulus);
          BigIntegerModularDivision(params, modulus_length, TheModulus,
                                    &Q, &L, &prime, &Tmp[T1]);
        }

        (void)BigIntRemainder(&Tmp[T1], &prime, &L);
        CopyBigInt(&Tmp[T1], &L);
        // Compute currentSolution as Tmp[T1] * Mult + currentSolution
        (void)BigIntMultiply(&Tmp[T1], &Mult, &L);
        BigIntAdd(&currentSolution, &L, &currentSolution);
        (void)BigIntMultiply(&K1, &Mult, &Mult);
      }   /* end for */

      intToBigInteger(&V, 0);
      BigIntSubt(&V, pGcdAll, &K1);

      // Perform loop while V < GcdAll.
      while (K1.sign == SIGN_NEGATIVE) {
        // The solution is V*ValNn + currentSolution
        (void)BigIntMultiply(&V, pValNn, &K1);
        BigIntAdd(&K1, &currentSolution, &K1);
        Solution(BigIntegerToBigInt(&K1));
        addbigint(&V, 1);  // V <- V + 1
        BigIntSubt(&V, pGcdAll, &K1);
      }

      for (T1 = nbrFactors - 1; T1 >= 0; T1--) {
        BigInteger bigBase;
        IntArray2BigInteger(modulus_length, factors.product[T1].array, &bigBase);
        (void)BigIntPowerIntExp(&bigBase, factors.product[T1].multiplicity,
                                &prime);
        BigIntSubt(&Solution1[T1], &Solution2[T1], &K1);
        if ((K1.nbrLimbs == 1) && (K1.limbs[0].x == 0)) {
          // quad_info.Solution1[T1] == quad_info.Solution2[T1]
          Exponents[T1] += 2;
        } else {
          // quad_info.Solution1[T1] != quad_info.Solution2[T1]
          Exponents[T1]++;
        }
        // L <- Exponents[T1] * quad_info.Increment[T1]
        multadd(&L, Exponents[T1], &Increment[T1], 0);
        // K1 <- 2 * prime
        multadd(&K1, 2, &prime, 0);
        BigIntSubt(&L, &K1, &L);
        if (L.sign == SIGN_NEGATIVE) {
          break;
        }
        Exponents[T1] = 0;
      }   /* end for */
    } while (T1 >= 0);
  }

  // Solve Bx + C = 0 (mod N).
  void SolveModularLinearEquation(BigInteger *pValA, const BigInteger *pValB,
                                  const BigInteger *pValC, BigInteger *pValN) {
    int powerOf2;
    int solutionNbr = 0;

    // This thing generates its own factors for the Chinese Remainder
    // Theorem call.
    auto factors = std::make_unique<Factors>();
    factors->storage.resize(20000);

    int* ptrFactorsMod = factors->storage.data();
    // struct sFactors* pstFactor = &astFactorsMod[1];

    BigInteger* ptrSolution1 = Solution1;
    BigInteger* ptrSolution2 = Solution2;
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
    DivideBigNbrByMaxPowerOf2(&powerOf2, pValN->limbs, &pValN->nbrLimbs);
    modulus_length = pValN->nbrLimbs;
    int NumberLengthBytes = modulus_length * (int)sizeof(limb);
    if ((pValN->nbrLimbs != 1) || (pValN->limbs[0].x != 1)) {
      // ValN is not 1.
      CopyBigInt(&Increment[solutionNbr], pValN);
      Exponents[solutionNbr] = 1;
      (void)memcpy(TheModulus, pValN->limbs, NumberLengthBytes);
      TheModulus[modulus_length].x = 0;
      // Perform division using odd modulus r.
      MontgomeryParams params = GetMontgomeryParams(modulus_length, TheModulus);
      // Compute ptrSolution1 as ValC / |ValB|
      BigIntegerModularDivision(params, modulus_length, TheModulus,
                                pValC, pValB, pValN, ptrSolution1);
      CHECK(ptrSolution1->nbrLimbs > 0);
      // Compute ptrSolution1 as -ValC / ValB
      if (!BigIntIsZero(ptrSolution1)) {
        BigIntSubt(pValN, ptrSolution1, ptrSolution1);
      }
      CopyBigInt(ptrSolution2, ptrSolution1);
      BigInteger2IntArray(modulus_length, ptrFactorsMod, pValN);
      factors->product.push_back({.array = ptrFactorsMod,
                                  .multiplicity = 1});
      // in storage, skip the number of limbs we used
      ptrFactorsMod += *ptrFactorsMod;
      // .. and length
      ptrFactorsMod++;

      CHECK(ptrSolution1->nbrLimbs > 0);
      CHECK(ptrSolution2->nbrLimbs > 0);

      ptrSolution1++;
      ptrSolution2++;
      solutionNbr++;
    }

    // Perform division using power of 2.
    if (powerOf2 > 0) {
      BigIntPowerOf2(ptrSolution1, powerOf2);
      CopyBigInt(&Increment[solutionNbr], ptrSolution1);
      CHECK(ptrSolution1->nbrLimbs > 0);
      Exponents[solutionNbr] = 1;
      BigInteger2IntArray(modulus_length, ptrFactorsMod, ptrSolution1);
      CHECK(ptrSolution1->nbrLimbs > 0);

      // Port note: Original code didn't advance the factor pointer nor
      // storage pointer (probably because this is not in a loop) but
      // that just seems wrong.
      factors->product.push_back({.array = ptrFactorsMod,
                                  .multiplicity = 1});
      // in storage, skip the number of limbs we used
      ptrFactorsMod += *ptrFactorsMod;
      // .. and length
      ptrFactorsMod++;

      int modulus_length = 0;
      MontgomeryParams params =
        GetMontgomeryParamsPowerOf2(powerOf2, &modulus_length);
      // ptrSolution1 <- 1 / |ValB|
      ComputeInversePower2(pValB->limbs, ptrSolution1->limbs, modulus_length);
      // Compute ptrSolution1 as |ValC| / |ValB|
      ModMult(params,
              ptrSolution1->limbs, pValC->limbs,
              modulus_length, TheModulus,
              ptrSolution1->limbs);
      NumberLengthBytes = modulus_length * (int)sizeof(int);
      // Compute ptrSolution1 as -ValC / ValB
      if (pValB->sign == pValC->sign) {
        (void)memset(pValA->limbs, 0, NumberLengthBytes);
        SubtractBigNbr(pValA->limbs, ptrSolution1->limbs,
                       ptrSolution1->limbs, modulus_length);
      }

      // Discard bits outside number in most significant limb.
      ptrSolution1->limbs[modulus_length - 1].x &=
        (1 << (powerOf2 % BITS_PER_GROUP)) - 1;
      ptrSolution1->nbrLimbs = modulus_length;
      ptrSolution1->sign = SIGN_POSITIVE;
      CopyBigInt(ptrSolution2, ptrSolution1);
      CHECK(ptrSolution1->nbrLimbs > 0);
      solutionNbr++;
    }
    // astFactorsMod[0].multiplicity = solutionNbr;
    assert((int)factors->product.size() == solutionNbr);

    PerformChineseRemainderTheorem(*factors);
  }

  // Compute sqrRoot <- sqrt(ValCOdd) mod 2^expon.
  // To compute the square root, compute the inverse of sqrt,
  // so only multiplications are used.
  // f(x) = invsqrt(x), f_{n+1}(x) = f_n * (3 - x*f_n^2)/2
  void ComputeSquareRootModPowerOf2(int expon, int bitsCZero) {
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

  // Find quadratic solution of Quadr*x^2 + Linear*x + Const = 0 (mod 2^expon)
  // when Quadr is even and Linear is odd. In this case there is unique solution.
  void findQuadraticSolution(BigInteger* pSolution, int exponent) {
    int bytesLen;
    int expon = exponent;
    int bitMask = 1;
    limb* ptrSolution = pSolution->limbs;
    BigIntPowerOf2(&Aux0, expon);
    bytesLen = Aux0.nbrLimbs * (int)sizeof(limb);
    (void)memset(ptrSolution, 0, bytesLen);
    while (expon > 0) {
      expon--;
      BigIntPowerOf2(&Aux2, expon);
      addbigint(&Aux2, -1);              // Aux2 <- 2^expon -1

      if ((Const.limbs[0].x & 1) != 0) {
        // Const is odd.
        ptrSolution->x |= bitMask;
        // Compute Const as Quadr/2 + floor(Linear/2) + floor(Const/2) + 1
        if (Const.sign == SIGN_NEGATIVE) {
          addbigint(&Const, -1);
        }
        BigIntDivideBy2(&Const);          // floor(Const/2)
        addbigint(&Const, 1);             // floor(Const/2) + 1
        CopyBigInt(&Aux1, &Linear);
        if (Aux1.sign == SIGN_NEGATIVE) {
          addbigint(&Aux1, -1);
        }
        BigIntDivideBy2(&Aux1);           // floor(Linear/2)
        BigIntAdd(&Const, &Aux1, &Const);
        CopyBigInt(&Aux1, &Quadr);
        BigIntDivideBy2(&Aux1);            // Quadr/2
        BigIntAdd(&Const, &Aux1, &Const);

        // Linear <- 2*Quadr + Linear and Quadr <- 2*Quadr.
        BigIntMultiplyBy2(&Quadr);         // Quadr*2
        BigIntAdd(&Linear, &Quadr, &Linear);
        BigIntAnd(&Linear, &Aux2, &Linear);   // Reduce mod 2^expon
      } else {
        // Const is even.
        BigIntDivideBy2(&Const);           // Const/2
        BigIntMultiplyBy2(&Quadr);         // Quadr*2
      }

      BigIntAnd(&Const, &Aux2, &Const);    // Reduce mod 2^expon
      BigIntAnd(&Quadr, &Aux2, &Quadr);    // Reduce mod 2^expon
      bitMask *= 2;
      if (bitMask < 0) {
        bitMask = 1;
        ptrSolution++;
      }
    }
    modulus_length = Aux0.nbrLimbs;
    setNbrLimbs(pSolution, modulus_length);
  }

  // Solve Ax^2 + Bx + C = 0 (mod 2^expon).
  bool SolveQuadraticEqModPowerOf2(
      int exponent, int factorIndex,
      const BigInteger *pValA, const BigInteger* pValB, const BigInteger* pValC) {
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
      BigIntPowerOf2(&K1, expon);
      addbigint(&K1, -1);
      BigIntAnd(&tmp1, &K1, &ValCOdd);      // (b/2) - a*c mod 2^n
      modulus_length = K1.nbrLimbs;

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
          ComputeSquareRootModPowerOf2(expon, bitsCZero);
          expon--;
          if (expon == (bitsCZero / 2)) {
            expon++;
          }
        }
      }

      // x = sqrRoot - b/2a.
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
      BigIntAnd(&tmp2, &K1, &Solution1[factorIndex]);
      BigIntSubt(&tmp1, &sqrRoot, &tmp2);
      BigIntAnd(&tmp2, &K1, &Solution2[factorIndex]);

      CHECK(Solution1[factorIndex].nbrLimbs > 0);

    } else if ((bitsAZero == 0) && (bitsBZero == 0)) {
      CopyBigInt(&Quadr, pValA);
      BigIntMultiplyBy2(&Quadr);         // 2a
      CopyBigInt(&Linear, pValB);        // b
      CopyBigInt(&Const, pValC);
      BigIntDivideBy2(&Const);           // c/2
      findQuadraticSolution(&Solution1[factorIndex], expon - 1);
      BigIntMultiplyBy2(&Solution1[factorIndex]);

      CopyBigInt(&Quadr, pValA);
      BigIntMultiplyBy2(&Quadr);         // 2a
      BigIntAdd(&Quadr, pValB, &Linear); // 2a+b
      CopyBigInt(&Const, pValA);
      BigIntAdd(&Const, pValB, &Const);
      BigIntAdd(&Const, pValC, &Const);
      BigIntDivideBy2(&Const);           // (a+b+c)/2
      findQuadraticSolution(&Solution2[factorIndex], expon - 1);
      BigIntMultiplyBy2(&Solution2[factorIndex]);
      addbigint(&Solution2[factorIndex], 1);

      CHECK(Solution1[factorIndex].nbrLimbs > 0);

    } else {
      CopyBigInt(&Quadr, pValA);
      CopyBigInt(&Linear, pValB);
      CopyBigInt(&Const, pValC);
      findQuadraticSolution(&Solution1[factorIndex], expon);
      sol2Invalid = true;

      CHECK(Solution1[factorIndex].nbrLimbs > 0);
    }

    BigIntPowerOf2(&Q, expon);         // Store increment.
    return true;
  }

  void ComputeSquareRootModPowerOfP(const BigInteger *base, int nbrBitsSquareRoot) {
    int correctBits;
    modulus_length = prime.nbrLimbs;
    int NumberLengthBytes = modulus_length * (int)sizeof(limb);
    (void)memcpy(TheModulus, prime.limbs, NumberLengthBytes);
    TheModulus[modulus_length].x = 0;
    MontgomeryParams params = GetMontgomeryParams(modulus_length, TheModulus);
    CopyBigInt(&Q, &prime);

    if ((prime.limbs[0].x & 3) == 3) {
      // prime mod 4 = 3
      subtractdivide(&Q, -1, 4);   // Q <- (prime+1)/4.
      BigIntegerModularPower(params, modulus_length, TheModulus, base, &Q, &SqrtDisc);
    } else {
      limb* toConvert;
      // Convert discriminant to Montgomery notation.
      CompressLimbsBigInteger(modulus_length, Aux[5].limbs, base);
      ModMult(params,
              Aux[5].limbs, params.MontgomeryMultR2,
              modulus_length, TheModulus,
              Aux[6].limbs);  // u
      if ((prime.limbs[0].x & 7) == 5) {
        // prime mod 8 = 5: use Atkin's method for modular square roots.
        // Step 1. v <- (2u)^((p-5)/8) mod p
        // Step 2. i <- (2uv^2) mod p
        // Step 3. square root of u <- uv (i-1)
        // Step 1.
        // Q <- (prime-5)/8.
        subtractdivide(&Q, 5, 8);
        // 2u
        AddBigNbrModN(Aux[6].limbs, Aux[6].limbs, Aux[7].limbs,
                      TheModulus, modulus_length);
        ModPow(params, modulus_length, TheModulus,
               Aux[7].limbs, Q.limbs, Q.nbrLimbs, Aux[8].limbs);
        // At this moment Aux[7].limbs is v in Montgomery notation.

        // Step 2.
        ModMult(params,
                Aux[8].limbs, Aux[8].limbs,
                modulus_length, TheModulus,
                Aux[9].limbs);  // v^2
        ModMult(params,
                Aux[7].limbs, Aux[9].limbs,
                modulus_length, TheModulus,
                Aux[9].limbs);  // i

        // Step 3.
        // i-1
        SubtBigNbrModN(Aux[9].limbs, params.MontgomeryMultR1, Aux[9].limbs,
                       TheModulus, modulus_length);
        // v*(i-1)
        ModMult(params,
                Aux[8].limbs, Aux[9].limbs,
                modulus_length, TheModulus,
                Aux[9].limbs);
        // u*v*(i-1)
        ModMult(params,
                Aux[6].limbs, Aux[9].limbs,
                modulus_length, TheModulus,
                Aux[9].limbs);
        toConvert = Aux[9].limbs;
      } else {
        // prime = 1 (mod 8). Use Shanks' method for modular square roots.
        // Step 1. Select e >= 3, q odd such that p = 2^e * q + 1.
        // Step 2. Choose x at random in the range 1 < x < p such that jacobi (x, p) = -1.
        // Step 3. Set z <- x^q (mod p).
        // Step 4. Set y <- z, r <- e, x <- a^((q-1)/2) mod p, v <- ax mod p, w <- vx mod p.
        // Step 5. If w = 1, the computation ends with +/-v as the square root.
        // Step 6. Find the smallest value of k such that w^(2^k) = 1 (mod p)
        // Step 7. Set d <- y^(2^(r-k-1)) mod p, y <- d^2 mod p, r <- k, v <- dv mod p, w <- wy mod p.
        // Step 8. Go to step 5.
        int e;
        int r;
        // Step 1.
        subtractdivide(&Q, 1, 1);   // Q <- (prime-1).
        DivideBigNbrByMaxPowerOf2(&e, Q.limbs, &Q.nbrLimbs);
        // Step 2.
        int x = 1;

        do {
          x++;
          intToBigInteger(&Aux[3], x);
        } while (BigIntJacobiSymbol(&Aux[3], &prime) >= 0);

        // Step 3.
        // Get z <- x^q (mod p) in Montgomery notation.
        ModPowBaseInt(params, modulus_length, TheModulus,
                      x, Q.limbs, Q.nbrLimbs, Aux[4].limbs);  // z
        // Step 4.
        NumberLengthBytes = modulus_length * (int)sizeof(limb);
        (void)memcpy(Aux[5].limbs, Aux[4].limbs, NumberLengthBytes); // y
        r = e;
        CopyBigInt(&K1, &Q);
        subtractdivide(&K1, 1, 2);
        ModPow(params, modulus_length, TheModulus,
               Aux[6].limbs, K1.limbs, K1.nbrLimbs, Aux[7].limbs); // x
        ModMult(params,
                Aux[6].limbs, Aux[7].limbs,
                modulus_length, TheModulus,
                Aux[8].limbs);         // v
        ModMult(params,
                Aux[8].limbs, Aux[7].limbs,
                modulus_length, TheModulus,
                Aux[9].limbs);         // w
        // Step 5
        while (memcmp(Aux[9].limbs, params.MontgomeryMultR1,
                      NumberLengthBytes) != 0) {
          // Step 6
          int k = 0;
          (void)memcpy(Aux[10].limbs, Aux[9].limbs, NumberLengthBytes);
          do {
            k++;
            ModMult(params,
                    Aux[10].limbs, Aux[10].limbs,
                    modulus_length, TheModulus,
                    Aux[10].limbs);
          } while (memcmp(Aux[10].limbs, params.MontgomeryMultR1,
                          NumberLengthBytes) != 0);
          // Step 7
          (void)memcpy(Aux[11].limbs, Aux[5].limbs, NumberLengthBytes); // d
          for (int ctr = 0; ctr < (r - k - 1); ctr++) {
            ModMult(params,
                    Aux[11].limbs, Aux[11].limbs,
                    modulus_length, TheModulus,
                    Aux[11].limbs);
          }
          ModMult(params,
                  Aux[11].limbs, Aux[11].limbs,
                  modulus_length, TheModulus,
                  Aux[5].limbs);   // y
          r = k;
          ModMult(params,
                  Aux[8].limbs, Aux[11].limbs,
                  modulus_length, TheModulus,
                  Aux[8].limbs);    // v
          ModMult(params,
                  Aux[9].limbs, Aux[5].limbs,
                  modulus_length, TheModulus,
                  Aux[9].limbs);     // w
        }
        toConvert = Aux[8].limbs;
      }

      // Convert from Montgomery to standard notation.
      NumberLengthBytes = modulus_length * (int)sizeof(limb);
      // Convert power to standard notation.
      (void)memset(Aux[4].limbs, 0, NumberLengthBytes);
      Aux[4].limbs[0].x = 1;
      ModMult(params,
              Aux[4].limbs, toConvert,
              modulus_length, TheModulus,
              toConvert);
      UncompressLimbsBigInteger(modulus_length, toConvert, &SqrtDisc);
    }

    // Obtain inverse of square root stored in SqrtDisc (mod prime).
    BigInteger tmp2;
    intToBigInteger(&tmp2, 1);
    BigIntegerModularDivision(params, modulus_length, TheModulus,
                              &tmp2, &SqrtDisc, &prime, &sqrRoot);
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
      (void)BigIntMultiply(&tmp2, &discriminant, &tmp1);
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
    (void)BigIntMultiply(&sqrRoot, &discriminant, &sqrRoot);
    (void)BigIntRemainder(&sqrRoot, &Q, &sqrRoot);
  }

  // Solve Ax^2 + Bx + C = 0 (mod p^expon).
  bool SolveQuadraticEqModPowerOfP(
      int expon, int factorIndex,
      const BigInteger* pValA, const BigInteger* pValB) {
    int correctBits;
    int nbrLimbs;
    int ctr;
    int deltaZeros;
    int NumberLengthBytes;

    // Number of bits of square root of discriminant to compute: expon + bits_a + 1,
    // where bits_a is the number of least significant bits of a set to zero.
    // To compute the square root, compute the inverse of sqrt, so only multiplications are used.
    // f(x) = invsqrt(x), f_{n+1}(x) = f_n * (3 - x*f_n^2)/2
    // Get maximum power of prime which divide ValA.
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
    (void)BigIntRemainder(&discriminant, &V, &discriminant);

    // Get maximum power of prime which divide discriminant.
    if (BigIntIsZero(&discriminant)) {
      // Discriminant is zero.
      deltaZeros = expon;
    } else {
      // Discriminant is not zero.
      deltaZeros = 0;
      for (;;) {
        BigInteger tmp1;
        (void)BigIntRemainder(&discriminant, &prime, &tmp1);
        if (!BigIntIsZero(&tmp1)) {
          break;
        }
        (void)BigIntDivide(&discriminant, &prime, &discriminant);
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
    nbrLimbs = tmp1.nbrLimbs;

    if (ValAOdd.sign == SIGN_NEGATIVE) {
      ValAOdd.sign = SIGN_POSITIVE;           // Negate 2*A
    } else if (!BigIntIsZero(&ValAOdd)) {
      BigIntSubt(&tmp1, &ValAOdd, &ValAOdd);  // Negate 2*A
    } else {
      // Nothing to do.
    }

    BigInteger tmp2;
    intToBigInteger(&tmp2, 1);
    modulus_length = tmp1.nbrLimbs;
    NumberLengthBytes = modulus_length * (int)sizeof(limb);
    (void)memcpy(TheModulus, tmp1.limbs, NumberLengthBytes);
    TheModulus[modulus_length].x = 0;
    MontgomeryParams params = GetMontgomeryParams(modulus_length, TheModulus);

    {
      BigInteger Tmp;
      BigIntegerModularDivision(params, modulus_length, TheModulus,
                                &tmp2, &ValAOdd, &tmp1, &Tmp);
      CopyBigInt(&ValAOdd, &Tmp);
    }

    if (BigIntIsZero(&discriminant)) {
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
      (void)BigIntRemainder(&discriminant, &tmp1, &discriminant);

      if (discriminant.sign == SIGN_NEGATIVE) {
        BigIntAdd(&discriminant, &tmp1, &discriminant);
      }

      if (nbrLimbs > discriminant.nbrLimbs) {
        int lenBytes = (nbrLimbs - discriminant.nbrLimbs) * (int)sizeof(limb);
        (void)memset(&discriminant.limbs[nbrLimbs], 0, lenBytes);
      }

      {
        BigInteger Tmp;
        (void)BigIntRemainder(&discriminant, &prime, &Tmp);
        if (Tmp.sign == SIGN_NEGATIVE) {
          BigIntAdd(&Tmp, &prime, &Tmp);
        }

        if (BigIntJacobiSymbol(&Tmp, &prime) != 1) {
          return false;         // Not a quadratic residue, so go out.
        }

        // Port note: This used to be passed in Aux[3]. This call might
        // expect more state in Aux, ugh.

        // Compute square root of discriminant.
        ComputeSquareRootModPowerOfP(&Tmp, nbrBitsSquareRoot);
      }

      // Multiply by square root of discriminant by prime^deltaZeros.
      for (ctr = 0; ctr < deltaZeros; ctr++) {
        (void)BigIntMultiply(&sqrRoot, &prime, &sqrRoot);
      }
    }

    correctBits = expon - deltaZeros;
    // Store increment.
    (void)BigIntPowerIntExp(&prime, correctBits, &Q);
    // Compute x = (b + sqrt(discriminant)) / (-2a) and
    //   x = (b - sqrt(discriminant)) / (-2a)
    BigIntAdd(pValB, &sqrRoot, &tmp1);

    for (ctr = 0; ctr < bitsAZero; ctr++) {
      (void)BigIntRemainder(&tmp1, &prime, &tmp2);
      if (!BigIntIsZero(&tmp2)) {
        // Cannot divide by prime, so go out.
        sol1Invalid = true;
        break;
      }
      (void)BigIntDivide(&tmp1, &prime, &tmp1);
    }

    (void)BigIntMultiply(&tmp1, &ValAOdd, &tmp1);
    (void)BigIntRemainder(&tmp1, &Q, &Solution1[factorIndex]);

    if (Solution1[factorIndex].sign == SIGN_NEGATIVE) {
      BigIntAdd(&Solution1[factorIndex], &Q, &Solution1[factorIndex]);
    }

    CHECK(Solution1[factorIndex].nbrLimbs > 0);

    BigIntSubt(pValB, &sqrRoot, &tmp1);
    for (ctr = 0; ctr < bitsAZero; ctr++) {
      (void)BigIntRemainder(&tmp1, &prime, &tmp2);
      if (!BigIntIsZero(&tmp2)) {
        // Cannot divide by prime, so go out.
        sol2Invalid = true;
        break;
      }
      (void)BigIntDivide(&tmp1, &prime, &tmp1);
    }

    (void)BigIntMultiply(&tmp1, &ValAOdd, &tmp1);
    (void)BigIntRemainder(&tmp1, &Q, &Solution2[factorIndex]);

    if (Solution2[factorIndex].sign == SIGN_NEGATIVE) {
      BigIntAdd(&Solution2[factorIndex], &Q, &Solution2[factorIndex]);
    }

    CHECK(Solution2[factorIndex].nbrLimbs > 0);
    return true;
  }

  void QuadraticTermMultipleOfP(
      int expon, int factorIndex,
      const BigInteger *pValA, const BigInteger *pValB, const BigInteger *pValC) {
    // Perform Newton approximation.
    // The next value of x in sequence x_{n+1} is x_n - (a*x_n^2 + b*x_n + c) / (2*a_x + b).
    BigInteger* ptrSolution = &Solution1[factorIndex];
    modulus_length = prime.nbrLimbs;
    int NumberLengthBytes = modulus_length * (int)sizeof(limb);
    (void)memcpy(TheModulus, prime.limbs, NumberLengthBytes);
    TheModulus[modulus_length].x = 0;
    MontgomeryParams params = GetMontgomeryParams(modulus_length, TheModulus);
    BigIntegerModularDivision(params, modulus_length, TheModulus,
                              pValC, pValB, &prime, ptrSolution);
    BigIntNegate(ptrSolution, ptrSolution);
    if (ptrSolution->sign == SIGN_NEGATIVE) {
      BigIntAdd(ptrSolution, &prime, ptrSolution);
    }

    CHECK(ptrSolution->nbrLimbs > 0);

    for (int currentExpon = 2; currentExpon < (2 * expon); currentExpon *= 2) {
      (void)BigIntPowerIntExp(&prime, currentExpon, &V);
      (void)BigIntMultiply(pValA, ptrSolution, &Q);// a*x_n
      CopyBigInt(&L, &Q);
      BigIntAdd(&Q, pValB, &Q);                    // a*x_n + b
      (void)BigIntRemainder(&Q, &V, &Q);
      (void)BigIntMultiply(&Q, ptrSolution, &Q);   // a*x_n^2 + b*x_n
      BigIntAdd(&Q, pValC, &Q);                    // a*x_n^2 + b*x_n + c
      (void)BigIntRemainder(&Q, &V, &Q);           // Numerator.
      MultInt(&L, &L, 2);                          // 2*a*x_n
      BigIntAdd(&L, pValB, &L);                    // 2*a*x_n + b
      (void)BigIntRemainder(&L, &V, &L);           // Denominator
      modulus_length = V.nbrLimbs;
      int NumberLengthBytes = modulus_length * (int)sizeof(limb);
      (void)memcpy(TheModulus, V.limbs, NumberLengthBytes);
      TheModulus[modulus_length].x = 0;
      MontgomeryParams params = GetMontgomeryParams(modulus_length, TheModulus);
      BigIntegerModularDivision(params, modulus_length, TheModulus,
                                &Q, &L, &V, &Aux1);
      BigIntSubt(ptrSolution, &Aux1, ptrSolution);
      (void)BigIntRemainder(ptrSolution, &V, ptrSolution);

      if (ptrSolution->sign == SIGN_NEGATIVE) {
        BigIntAdd(ptrSolution, &V, ptrSolution);
      }
    }
    (void)BigIntPowerIntExp(&prime, expon, &Q);
    (void)BigIntRemainder(ptrSolution, &Q, ptrSolution);
    CopyBigInt(&Solution2[factorIndex], &Solution1[factorIndex]);

    CHECK(Solution1[factorIndex].nbrLimbs > 0);
  }

  bool QuadraticTermNotMultipleOfP(
      int expon, int factorIndex,
      const BigInteger *pValA, const BigInteger* pValB, const BigInteger* pValC) {
    bool solutions;
    sol1Invalid = false;
    sol2Invalid = false;
    // Compute discriminant = ValB^2 - 4*ValA*ValC.
    {
      BigInteger Tmp;
      (void)BigIntMultiply(pValB, pValB, &Tmp);
      (void)BigIntMultiply(pValA, pValC, &discriminant);
      MultInt(&discriminant, &discriminant, 4);
      BigIntSubt(&Tmp, &discriminant, &discriminant);
    }
    if ((prime.nbrLimbs == 1) && (prime.limbs[0].x == 2)) {
      /* Prime p is 2 */
      solutions = SolveQuadraticEqModPowerOf2(expon, factorIndex,
        pValA, pValB, pValC);
    } else {
      // Prime is not 2
      solutions = SolveQuadraticEqModPowerOfP(expon, factorIndex, pValA, pValB);
    }
    if (!solutions || (sol1Invalid && sol2Invalid)) {
      // Both solutions are invalid. Go out.
      return false;
    }

    if (sol1Invalid) {
      // Solution1 is invalid. Overwrite it with Solution2.
      CopyBigInt(&Solution1[factorIndex], &Solution2[factorIndex]);
    } else if (sol2Invalid) {
      // Solution2 is invalid. Overwrite it with Solution1.
      CopyBigInt(&Solution2[factorIndex], &Solution1[factorIndex]);
    } else {
      // Nothing to do.
    }

    {
      BigInteger Tmp;
      BigIntSubt(&Solution2[factorIndex], &Solution1[factorIndex], &Tmp);

      if (Tmp.sign == SIGN_NEGATIVE) {
        // Solution2 is less than Solution1, so exchange them.
        CopyBigInt(&Tmp, &Solution1[factorIndex]);
        CopyBigInt(&Solution1[factorIndex], &Solution2[factorIndex]);
        CopyBigInt(&Solution2[factorIndex], &Tmp);
      }
    }

    CHECK(Solution1[factorIndex].nbrLimbs > 0);
    CHECK(Solution2[factorIndex].nbrLimbs > 0);

    return true;
  }

  // Solve Ax^2 + Bx + C = 0 (mod N).
  void SolveEquation(
      const SolutionFn &solutionCback,
      BigInteger* pValA, const BigInteger* pValB,
      const BigInteger* pValC, BigInteger* pValN,
      BigInteger* pGcdAllParm, BigInteger* pValNnParm) {

    // PERF: no need to copy
    Solution = solutionCback;

    pGcdAll = pGcdAllParm;
    pValNn = pValNnParm;
    {
      BigInteger Tmp;
      (void)BigIntRemainder(pValA, pValN, &Tmp);
      if (BigIntIsZero(&Tmp)) {
        // Linear equation.
        SolveModularLinearEquation(pValA, pValB, pValC, pValN);
        return;
      }
    }

    // PERF: This code will reuse factors. We might want to do that.
    #if 0
    if ((LastModulus.nbrLimbs == 0) || !BigIntEqual(&LastModulus, pValN))
    {     // Last modulus is different from ValN.
      CopyBigInt(&LastModulus, pValN);
      modulus_length = pValN->nbrLimbs;
      BigInteger2IntArray(modulus_length, nbrToFactor, pValN);
      factor(pValN, nbrToFactor, factorsMod, astFactorsMod);
    }
    #endif

    // mimicking original code, but now we aren't using the LastModulus
    // at all, I think
    // CopyBigInt(&LastModulus, pValN);
    std::unique_ptr<Factors> factors = BigFactor(pValN);

    intToBigInteger(&Q, 0);
    const int nbrFactors = factors->product.size();
    const sFactorz *pstFactor = &factors->product[0];
    for (int factorIndex = 0; factorIndex < nbrFactors; factorIndex++) {
      int expon = pstFactor->multiplicity;
      if (expon == 0) {
        intToBigInteger(&Solution1[factorIndex], 0);
        intToBigInteger(&Solution2[factorIndex], 0);
        intToBigInteger(&Increment[factorIndex], 1);
        pstFactor++;
        continue;
      }
      const int number_length = *pstFactor->array;
      modulus_length = number_length;
      IntArray2BigInteger(number_length, pstFactor->array, &prime);
      (void)BigIntPowerIntExp(&prime, expon, &V);
      (void)BigIntRemainder(pValA, &prime, &L);
      if (BigIntIsZero(&L) && !((prime.nbrLimbs == 1) && (prime.limbs[0].x == 2))) {
        // ValA multiple of prime, means linear equation mod prime. Also prime is not 2.
        if ((BigIntIsZero(pValB)) && !(BigIntIsZero(pValC))) {
          // There are no solutions: ValB=0 and ValC!=0
          return;
        }
        QuadraticTermMultipleOfP(expon, factorIndex, pValA, pValB, pValC);
      } else {
        // If quadratic equation mod p
        if (!QuadraticTermNotMultipleOfP(expon, factorIndex, pValA, pValB, pValC)) {
          return;
        }
      }
      CopyBigInt(&Increment[factorIndex], &Q);
      Exponents[factorIndex] = 0;
      pstFactor++;
    }
    PerformChineseRemainderTheorem(*factors);
  }

};
}  // namespace

void SolveEquation(
    const SolutionFn &solutionCback,
    const BigInt &A, const BigInt &B,
    const BigInt &C, const BigInt &N,
    const BigInt &GcdAll, const BigInt &Nn) {
  std::unique_ptr<QuadModLL> qmll = std::make_unique<QuadModLL>();

  BigInteger a, b, c, n, gcd, nn;
  BigIntToBigInteger(A, &a);
  BigIntToBigInteger(B, &b);
  BigIntToBigInteger(C, &c);
  BigIntToBigInteger(N, &n);
  BigIntToBigInteger(GcdAll, &gcd);
  BigIntToBigInteger(Nn, &nn);
  qmll->SolveEquation(solutionCback,
                      &a, &b, &c, &n, &gcd, &nn);
}
