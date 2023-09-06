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
#include "globals.h"
#include "modmult.h"
#include "baseconv.h"

extern int NumberLength;

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

// PERF So big!
struct QuadModLL {
  BigInteger Solution1[400];
  BigInteger Solution2[400];
  BigInteger Increment[400];

  BigInteger Quadr;
  BigInteger Linear;
  BigInteger Const;
  BigInteger sqrRoot;
  BigInteger ValAOdd;
  BigInteger ValBOdd;
  BigInteger ValCOdd;
  BigInteger Mult;
  BigInteger currentSolution;
  BigInteger discriminant;
  BigInteger SqrtDisc;
  BigInteger prime_internal;
  BigInteger bigBase;
  BigInteger K1;
  BigInteger L;
  BigInteger Q;
  BigInteger V;
  int Exponents[400];
  BigInteger Aux[400];
  BigInteger tmp1;
  BigInteger tmp2;
  bool sol1Invalid;
  bool sol2Invalid;
  BigInteger prime;

  BigInteger Aux0;
  BigInteger Aux1;
  BigInteger Aux2;
  BigInteger* pGcdAll;
  BigInteger* pValNn;
  pSolution Solution;
  pShowSolutionsModPrime ShowSolutionsModPrime;
  pShowNoSolsModPrime ShowNoSolsModPrime;

  // Use Chinese remainder theorem to obtain the solutions.
  void PerformChineseRemainderTheorem(const Factors &factors) {
    int T1;
    int expon;
    do {
      multint(&Aux[0], &Increment[0], Exponents[0] / 2);
      if ((Exponents[0] & 1) != 0) {
        BigIntAdd(&Aux[0], &Solution2[0], &Aux[0]);
      } else {
        BigIntAdd(&Aux[0], &Solution1[0], &Aux[0]);
      }
      CopyBigInt(&currentSolution, &Aux[0]);
      const int nbrFactors = factors.product.size();
      const sFactorz *pstFactor = &factors.product[0];
      IntArray2BigInteger(NumberLength, pstFactor->array, &prime);
      (void)BigIntPowerIntExp(&prime, pstFactor->multiplicity, &Mult);
      for (T1 = 1; T1 < nbrFactors; T1++) {
        pstFactor++;
        if (pstFactor->multiplicity == 0) {
          intToBigInteger(&Aux[T1], 0);
          continue;
        }
        expon = Exponents[T1];
        multint(&Aux[T1], &Increment[T1], expon / 2);
        if ((expon & 1) != 0) {
          BigIntAdd(&Aux[T1], &Solution2[T1], &Aux[T1]);
        } else {
          BigIntAdd(&Aux[T1], &Solution1[T1], &Aux[T1]);
        }
        const int number_length = *pstFactor->array;
        NumberLength = number_length;
        IntArray2BigInteger(number_length, pstFactor->array, &prime);
        (void)BigIntPowerIntExp(&prime, pstFactor->multiplicity, &K1);
        CopyBigInt(&prime, &K1);
        for (int E = 0; E < T1; E++) {
          int NumberLengthBytes;
          BigIntSubt(&Aux[T1], &Aux[E], &Q);
          IntArray2BigInteger(NumberLength, factors.product[E].array, &bigBase);
          (void)BigIntPowerIntExp(&bigBase, factors.product[E].multiplicity, &L);
          NumberLength = prime.nbrLimbs;
          NumberLengthBytes = NumberLength * (int)sizeof(limb);
          (void)memcpy(TestNbr, prime.limbs, NumberLengthBytes);
          TestNbr[NumberLength].x = 0;
          GetMontgomeryParms(NumberLength);
          BigIntModularDivision(&Q, &L, &prime, &Aux[T1]);
        }
        (void)BigIntRemainder(&Aux[T1], &prime, &L);
        CopyBigInt(&Aux[T1], &L);
        // Compute currentSolution as Aux[T1] * Mult + currentSolution
        (void)BigIntMultiply(&Aux[T1], &Mult, &L);
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
        Solution(&K1);
        addbigint(&V, 1);  // V <- V + 1
        BigIntSubt(&V, pGcdAll, &K1);
      }
      for (T1 = nbrFactors - 1; T1 >= 0; T1--) {
        IntArray2BigInteger(NumberLength, factors.product[T1].array, &bigBase);
        (void)BigIntPowerIntExp(&bigBase, factors.product[T1].multiplicity, &prime);
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
    int NumberLengthBytes;
    int powerOf2;
    int solutionNbr = 0;

    // This thing generates its own factors for the Chinese Remainder Theorem call.
    auto factors = std::make_unique<Factors>();
    factors->storage.resize(20000);

    int* ptrFactorsMod = factors->storage.data();
    // struct sFactors* pstFactor = &astFactorsMod[1];

    BigInteger* ptrSolution1 = Solution1;
    BigInteger* ptrSolution2 = Solution2;
    BigIntGcd(pValB, pValN, &Aux[0]);
    if ((Aux[0].nbrLimbs != 1) || (Aux[0].limbs[0].x != 1)) {
      // ValB and ValN are not coprime. Go out.
      return;
    }
    // Calculate z <- -ValC / ValB (mod ValN)
    // Modular division routines used work for power of 2 or odd numbers.
    // This requires to compute the quotient in two steps.
    // N = r*2^k (r = odd)
    DivideBigNbrByMaxPowerOf2(&powerOf2, pValN->limbs, &pValN->nbrLimbs);
    NumberLength = pValN->nbrLimbs;
    NumberLengthBytes = NumberLength * (int)sizeof(limb);
    if ((pValN->nbrLimbs != 1) || (pValN->limbs[0].x != 1)) {
      // ValN is not 1.
      CopyBigInt(&Increment[solutionNbr], pValN);
      Exponents[solutionNbr] = 1;
      (void)memcpy(TestNbr, pValN->limbs, NumberLengthBytes);
      TestNbr[NumberLength].x = 0;
      // Perform division using odd modulus r.
      GetMontgomeryParms(NumberLength);
      // Compute ptrSolution1 as ValC / |ValB|
      BigIntModularDivision(pValC, pValB, pValN, ptrSolution1);
      // Compute ptrSolution1 as -ValC / ValB
      if (!BigIntIsZero(ptrSolution1)) {
        BigIntSubt(pValN, ptrSolution1, ptrSolution1);
      }
      CopyBigInt(ptrSolution2, ptrSolution1);
      BigInteger2IntArray(NumberLength, ptrFactorsMod, pValN);
      factors->product.push_back({.array = ptrFactorsMod,
                                  .multiplicity = 1});
      // in storage, skip the number of limbs we used
      ptrFactorsMod += *ptrFactorsMod;
      // .. and length
      ptrFactorsMod++;

      ptrSolution1++;
      ptrSolution2++;
      solutionNbr++;
    }

    // Perform division using power of 2.
    if (powerOf2 > 0) {
      BigIntPowerOf2(ptrSolution1, powerOf2);
      CopyBigInt(&Increment[solutionNbr], ptrSolution1);
      Exponents[solutionNbr] = 1;
      BigInteger2IntArray(NumberLength, ptrFactorsMod, ptrSolution1);

      // Port note: Original code didn't advance the factor pointer nor
      // storage pointer (probably because this is not in a loop) but
      // that just seems wrong.
      factors->product.push_back({.array = ptrFactorsMod,
                                  .multiplicity = 1});
      // in storage, skip the number of limbs we used
      ptrFactorsMod += *ptrFactorsMod;
      // .. and length
      ptrFactorsMod++;

      GetMontgomeryParmsPowerOf2(powerOf2);
      // Use ValA (which is zero for linear equations) as a temporary area.
      // ptrSolution1 <- 1 / |ValB|
      ComputeInversePower2(pValB->limbs, ptrSolution1->limbs, pValA->limbs);
      // Compute ptrSolution1 as |ValC| / |ValB|
      modmult(ptrSolution1->limbs, pValC->limbs, ptrSolution1->limbs);
      NumberLengthBytes = NumberLength * (int)sizeof(int);
      // Compute ptrSolution1 as -ValC / ValB
      if (pValB->sign == pValC->sign)
      {
        (void)memset(pValA->limbs, 0, NumberLengthBytes);
        SubtractBigNbr(pValA->limbs, ptrSolution1->limbs, ptrSolution1->limbs, NumberLength);
      }
      // Discard bits outside number in most significant limb.
      ptrSolution1->limbs[NumberLength - 1].x &= (1 << (powerOf2 % BITS_PER_GROUP)) - 1;
      ptrSolution1->nbrLimbs = NumberLength;
      ptrSolution1->sign = SIGN_POSITIVE;
      CopyBigInt(ptrSolution2, ptrSolution1);
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
    int lenBytes;
    int correctBits;
    int nbrLimbs;
    // First approximation to inverse of square root.
    // If value is ...0001b, the inverse of square root is ...01b.
    // If value is ...1001b, the inverse of square root is ...11b.
    sqrRoot.limbs[0].x = (((ValCOdd.limbs[0].x & 15) == 1) ? 1 : 3);
    correctBits = 2;
    nbrLimbs = 1;
    while (correctBits < expon) {
      // Compute f(x) = invsqrt(x), f_{n+1}(x) = f_n * (3 - x*f_n^2)/2
      correctBits *= 2;
      nbrLimbs = (correctBits / BITS_PER_GROUP) + 1;
      MultBigNbr(sqrRoot.limbs, sqrRoot.limbs, tmp2.limbs, nbrLimbs);
      MultBigNbr(tmp2.limbs, ValCOdd.limbs, tmp1.limbs, nbrLimbs);
      ChSignBigNbr(tmp1.limbs, nbrLimbs);
      lenBytes = nbrLimbs * (int)sizeof(limb);
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
    lenBytes = nbrLimbs * (int)sizeof(limb);
    (void)memcpy(sqrRoot.limbs, tmp1.limbs, lenBytes);
    setNbrLimbs(&sqrRoot, NumberLength);
    for (int ctr = 0; ctr < (bitsCZero / 2); ctr++)
    {
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
    NumberLength = Aux0.nbrLimbs;
    setNbrLimbs(pSolution, NumberLength);
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
      NumberLength = K1.nbrLimbs;
      if (NumberLength > ValAOdd.nbrLimbs)
      {
        int lenBytes = (NumberLength - ValAOdd.nbrLimbs) * (int)sizeof(int);
        (void)memset(&ValAOdd.limbs[ValAOdd.nbrLimbs], 0, lenBytes);
      }
      if (NumberLength > tmp2.nbrLimbs)
      {
        int lenBytes = (NumberLength - tmp2.nbrLimbs) * (int)sizeof(int);
        (void)memset(&tmp2.limbs[tmp2.nbrLimbs], 0, lenBytes);
      }
      ComputeInversePower2(ValAOdd.limbs, tmp2.limbs, tmp1.limbs);
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
      NumberLength = K1.nbrLimbs;
      ComputeInversePower2(ValAOdd.limbs, tmp2.limbs, tmp1.limbs);
      setNbrLimbs(&tmp2, NumberLength);
      CopyBigInt(&tmp1, pValB);
      BigIntDivideBy2(&tmp1);               // b/2
      (void)BigIntMultiply(&tmp1, &tmp2, &tmp1);  // b/2a
      BigIntChSign(&tmp1);                  // -b/2a
      BigIntAnd(&tmp1, &K1, &tmp1);         // -b/2a mod 2^expon
      BigIntAdd(&tmp1, &sqrRoot, &tmp2);
      BigIntAnd(&tmp2, &K1, &Solution1[factorIndex]);
      BigIntSubt(&tmp1, &sqrRoot, &tmp2);
      BigIntAnd(&tmp2, &K1, &Solution2[factorIndex]);

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

    } else {
      CopyBigInt(&Quadr, pValA);
      CopyBigInt(&Linear, pValB);
      CopyBigInt(&Const, pValC);
      findQuadraticSolution(&Solution1[factorIndex], expon);
      sol2Invalid = true;
    }

    BigIntPowerOf2(&Q, expon);         // Store increment.
    return true;
  }

  void ComputeSquareRootModPowerOfP(int nbrBitsSquareRoot) {
    int correctBits;
    NumberLength = prime.nbrLimbs;
    int NumberLengthBytes = NumberLength * (int)sizeof(limb);
    (void)memcpy(TestNbr, prime.limbs, NumberLengthBytes);
    TestNbr[NumberLength].x = 0;
    GetMontgomeryParms(NumberLength);
    CopyBigInt(&Q, &prime);
    if ((prime.limbs[0].x & 3) == 3) {
      // prime mod 4 = 3
      subtractdivide(&Q, -1, 4);   // Q <- (prime+1)/4.
      BigIntModularPower(&Aux[3], &Q, &SqrtDisc);
    } else {
      limb* toConvert;
      // Convert discriminant to Montgomery notation.
      CompressLimbsBigInteger(NumberLength, Aux[5].limbs, &Aux[3]);
      modmult(Aux[5].limbs, MontgomeryMultR2, Aux[6].limbs);  // u
      if ((prime.limbs[0].x & 7) == 5) {
        // prime mod 8 = 5: use Atkin's method for modular square roots.
        // Step 1. v <- (2u)^((p-5)/8) mod p
        // Step 2. i <- (2uv^2) mod p
        // Step 3. square root of u <- uv (i-1)
        // Step 1.
        // Q <- (prime-5)/8.
        subtractdivide(&Q, 5, 8);
        // 2u
        AddBigNbrModN(Aux[6].limbs, Aux[6].limbs, Aux[7].limbs, TestNbr, NumberLength);
        modPow(Aux[7].limbs, Q.limbs, Q.nbrLimbs, Aux[8].limbs);
        // At this moment Aux[7].limbs is v in Montgomery notation.

        // Step 2.
        modmult(Aux[8].limbs, Aux[8].limbs, Aux[9].limbs);  // v^2
        modmult(Aux[7].limbs, Aux[9].limbs, Aux[9].limbs);  // i

        // Step 3.
        // i-1
        SubtBigNbrModN(Aux[9].limbs, MontgomeryMultR1, Aux[9].limbs, TestNbr, NumberLength);
        // v*(i-1)
        modmult(Aux[8].limbs, Aux[9].limbs, Aux[9].limbs);
        // u*v*(i-1)
        modmult(Aux[6].limbs, Aux[9].limbs, Aux[9].limbs);
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
        int x;
        int e;
        int r;
        // Step 1.
        subtractdivide(&Q, 1, 1);   // Q <- (prime-1).
        DivideBigNbrByMaxPowerOf2(&e, Q.limbs, &Q.nbrLimbs);
        // Step 2.
        x = 1;
        do
        {
          x++;
          intToBigInteger(&Aux[3], x);
        } while (BigIntJacobiSymbol(&Aux[3], &prime) >= 0);
        // Step 3.
        // Get z <- x^q (mod p) in Montgomery notation.
        modPowBaseInt(x, Q.limbs, Q.nbrLimbs, Aux[4].limbs);  // z
        // Step 4.
        NumberLengthBytes = NumberLength * (int)sizeof(limb);
        (void)memcpy(Aux[5].limbs, Aux[4].limbs, NumberLengthBytes); // y
        r = e;
        CopyBigInt(&K1, &Q);
        subtractdivide(&K1, 1, 2);
        modPow(Aux[6].limbs, K1.limbs, K1.nbrLimbs, Aux[7].limbs); // x
        modmult(Aux[6].limbs, Aux[7].limbs, Aux[8].limbs);         // v
        modmult(Aux[8].limbs, Aux[7].limbs, Aux[9].limbs);         // w
        // Step 5
        while (memcmp(Aux[9].limbs, MontgomeryMultR1, NumberLengthBytes) != 0)
        {
          // Step 6
          int k = 0;
          (void)memcpy(Aux[10].limbs, Aux[9].limbs, NumberLengthBytes);
          do
          {
            k++;
            modmult(Aux[10].limbs, Aux[10].limbs, Aux[10].limbs);
          } while (memcmp(Aux[10].limbs, MontgomeryMultR1, NumberLengthBytes) != 0);
          // Step 7
          (void)memcpy(Aux[11].limbs, Aux[5].limbs, NumberLengthBytes); // d
          for (int ctr = 0; ctr < (r - k - 1); ctr++)
          {
            modmult(Aux[11].limbs, Aux[11].limbs, Aux[11].limbs);
          }
          modmult(Aux[11].limbs, Aux[11].limbs, Aux[5].limbs);   // y
          r = k;
          modmult(Aux[8].limbs, Aux[11].limbs, Aux[8].limbs);    // v
          modmult(Aux[9].limbs, Aux[5].limbs, Aux[9].limbs);     // w
        }
        toConvert = Aux[8].limbs;
      }
      // Convert from Montgomery to standard notation.
      NumberLengthBytes = NumberLength * (int)sizeof(limb);
      // Convert power to standard notation.
      (void)memset(Aux[4].limbs, 0, NumberLengthBytes);
      Aux[4].limbs[0].x = 1;
      modmult(Aux[4].limbs, toConvert, toConvert);
      UncompressLimbsBigInteger(NumberLength, toConvert, &SqrtDisc);
    }
    // Obtain inverse of square root stored in SqrtDisc (mod prime).
    intToBigInteger(&tmp2, 1);
    BigIntModularDivision(&tmp2, &SqrtDisc, &prime, &sqrRoot);
    correctBits = 1;
    CopyBigInt(&Q, &prime);
    // Obtain nbrBitsSquareRoot correct digits of inverse square root.
    while (correctBits < nbrBitsSquareRoot)
    {   // Compute f(x) = invsqrt(x), f_{n+1}(x) = f_n * (3 - x*f_n^2)/2
      correctBits *= 2;
      (void)BigIntMultiply(&Q, &Q, &Q);           // Square Q.
      (void)BigIntMultiply(&sqrRoot, &sqrRoot, &tmp1);
      (void)BigIntRemainder(&tmp1, &Q, &tmp2);
      (void)BigIntMultiply(&tmp2, &discriminant, &tmp1);
      (void)BigIntRemainder(&tmp1, &Q, &tmp2);
      intToBigInteger(&tmp1, 3);
      BigIntSubt(&tmp1, &tmp2, &tmp2);
      (void)BigIntMultiply(&tmp2, &sqrRoot, &tmp1);
      if ((tmp1.limbs[0].x & 1) != 0)
      {
        BigIntAdd(&tmp1, &Q, &tmp1);
      }
      BigIntDivide2(&tmp1);
      (void)BigIntRemainder(&tmp1, &Q, &sqrRoot);
    }
    // Get square root of discriminant from its inverse by multiplying by discriminant.
    if (sqrRoot.sign == SIGN_NEGATIVE)
    {
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
    int bitsAZero;
    int ctr;
    int deltaZeros;
    int NumberLengthBytes;
    // Number of bits of square root of discriminant to compute: expon + bits_a + 1,
    // where bits_a is the number of least significant bits of a set to zero.
    // To compute the square root, compute the inverse of sqrt, so only multiplications are used.
    // f(x) = invsqrt(x), f_{n+1}(x) = f_n * (3 - x*f_n^2)/2
    // Get maximum power of prime which divide ValA.
    CopyBigInt(&ValAOdd, pValA);
    bitsAZero = 0;
    for (;;)
    {
      (void)BigIntRemainder(&ValAOdd, &prime, &tmp1);
      if (ValAOdd.sign == SIGN_NEGATIVE)
      {
        BigIntAdd(&ValAOdd, &prime, &ValAOdd);
      }
      if (!BigIntIsZero(&tmp1))
      {
        break;
      }
      (void)BigIntDivide(&ValAOdd, &prime, &ValAOdd);
      bitsAZero++;
    }
    (void)BigIntRemainder(&discriminant, &V, &discriminant);
    // Get maximum power of prime which divide discriminant.
    if (BigIntIsZero(&discriminant))
    {      // Discriminant is zero.
      deltaZeros = expon;
    }
    else
    {      // Discriminant is not zero.
      deltaZeros = 0;
      for (;;)
      {
        (void)BigIntRemainder(&discriminant, &prime, &tmp1);
        if (!BigIntIsZero(&tmp1))
        {
          break;
        }
        (void)BigIntDivide(&discriminant, &prime, &discriminant);
        deltaZeros++;
      }
    }
    if (((deltaZeros & 1) != 0) && (deltaZeros < expon))
    {          // If delta is of type m*prime^n where m is not multiple of prime
               // and n is odd, there is no solution, so go out.
      return false;
    }
    deltaZeros >>= 1;
    // Compute inverse of -2*A (mod prime^(expon - deltaZeros)).
    BigIntAdd(pValA, pValA, &ValAOdd);
    (void)BigIntPowerIntExp(&prime, expon - deltaZeros, &tmp1);
    (void)BigIntRemainder(&ValAOdd, &tmp1, &ValAOdd);
    nbrLimbs = tmp1.nbrLimbs;
    if (ValAOdd.sign == SIGN_NEGATIVE)
    {
      ValAOdd.sign = SIGN_POSITIVE;           // Negate 2*A
    }
    else if (!BigIntIsZero(&ValAOdd))
    {
      BigIntSubt(&tmp1, &ValAOdd, &ValAOdd);  // Negate 2*A
    }
    else
    {            // Nothing to do.
    }
    intToBigInteger(&tmp2, 1);
    NumberLength = tmp1.nbrLimbs;
    NumberLengthBytes = NumberLength * (int)sizeof(limb);
    (void)memcpy(TestNbr, tmp1.limbs, NumberLengthBytes);
    TestNbr[NumberLength].x = 0;
    GetMontgomeryParms(NumberLength);
    BigIntModularDivision(&tmp2, &ValAOdd, &tmp1, &Aux[0]);
    CopyBigInt(&ValAOdd, &Aux[0]);
    if (BigIntIsZero(&discriminant))
    {     // Discriminant is zero.
      int lenBytes = nbrLimbs * (int)sizeof(limb);
      (void)memset(sqrRoot.limbs, 0, lenBytes);
      sqrRoot.nbrLimbs = 1;
      sqrRoot.sign = SIGN_POSITIVE;
    }
    else
    {      // Discriminant is not zero.
      // Find number of digits of square root to compute.
      int nbrBitsSquareRoot = expon + bitsAZero - deltaZeros;
      (void)BigIntPowerIntExp(&prime, nbrBitsSquareRoot, &tmp1);
      nbrLimbs = tmp1.nbrLimbs;
      (void)BigIntRemainder(&discriminant, &tmp1, &discriminant);
      if (discriminant.sign == SIGN_NEGATIVE)
      {
        BigIntAdd(&discriminant, &tmp1, &discriminant);
      }
      if (nbrLimbs > discriminant.nbrLimbs)
      {
        int lenBytes = (nbrLimbs - discriminant.nbrLimbs) * (int)sizeof(limb);
        (void)memset(&discriminant.limbs[nbrLimbs], 0, lenBytes);
      }
      (void)BigIntRemainder(&discriminant, &prime, &Aux[3]);
      if (Aux[3].sign == SIGN_NEGATIVE)
      {
        BigIntAdd(&Aux[3], &prime, &Aux[3]);
      }
      if (BigIntJacobiSymbol(&Aux[3], &prime) != 1)
      {
        return false;         // Not a quadratic residue, so go out.
      }
      // Compute square root of discriminant.
      ComputeSquareRootModPowerOfP(nbrBitsSquareRoot);
      // Multiply by square root of discriminant by prime^deltaZeros.
      for (ctr = 0; ctr < deltaZeros; ctr++)
      {
        (void)BigIntMultiply(&sqrRoot, &prime, &sqrRoot);
      }
    }
    correctBits = expon - deltaZeros;
    (void)BigIntPowerIntExp(&prime, correctBits, &Q);      // Store increment.
    // Compute x = (b + sqrt(discriminant)) / (-2a) and x = (b - sqrt(discriminant)) / (-2a)
    BigIntAdd(pValB, &sqrRoot, &tmp1);
    for (ctr = 0; ctr < bitsAZero; ctr++)
    {
      (void)BigIntRemainder(&tmp1, &prime, &tmp2);
      if (!BigIntIsZero(&tmp2))
      {   // Cannot divide by prime, so go out.
        sol1Invalid = true;
        break;
      }
      (void)BigIntDivide(&tmp1, &prime, &tmp1);
    }
    (void)BigIntMultiply(&tmp1, &ValAOdd, &tmp1);
    (void)BigIntRemainder(&tmp1, &Q, &Solution1[factorIndex]);
    if (Solution1[factorIndex].sign == SIGN_NEGATIVE)
    {
      BigIntAdd(&Solution1[factorIndex], &Q, &Solution1[factorIndex]);
    }
    BigIntSubt(pValB, &sqrRoot, &tmp1);
    for (ctr = 0; ctr < bitsAZero; ctr++)
    {
      (void)BigIntRemainder(&tmp1, &prime, &tmp2);
      if (!BigIntIsZero(&tmp2))
      {   // Cannot divide by prime, so go out.
        sol2Invalid = true;
        break;
      }
      (void)BigIntDivide(&tmp1, &prime, &tmp1);
    }
    (void)BigIntMultiply(&tmp1, &ValAOdd, &tmp1);
    (void)BigIntRemainder(&tmp1, &Q, &Solution2[factorIndex]);
    if (Solution2[factorIndex].sign == SIGN_NEGATIVE)
    {
      BigIntAdd(&Solution2[factorIndex], &Q, &Solution2[factorIndex]);
    }
    return true;
  }

  void QuadraticTermMultipleOfP(
      int expon, int factorIndex,
      const BigInteger *pValA, const BigInteger *pValB, const BigInteger *pValC) {
    // Perform Newton approximation.
    // The next value of x in sequence x_{n+1} is x_n - (a*x_n^2 + b*x_n + c) / (2*a_x + b).
    BigInteger* ptrSolution = &Solution1[factorIndex];
    NumberLength = prime.nbrLimbs;
    int NumberLengthBytes = NumberLength * (int)sizeof(limb);
    (void)memcpy(TestNbr, prime.limbs, NumberLengthBytes);
    TestNbr[NumberLength].x = 0;
    GetMontgomeryParms(NumberLength);
    BigIntModularDivision(pValC, pValB, &prime, ptrSolution);
    BigIntNegate(ptrSolution, ptrSolution);
    if (ptrSolution->sign == SIGN_NEGATIVE) {
      BigIntAdd(ptrSolution, &prime, ptrSolution);
    }

    for (int currentExpon = 2; currentExpon < (2 * expon); currentExpon *= 2) {
      (void)BigIntPowerIntExp(&prime, currentExpon, &V);
      (void)BigIntMultiply(pValA, ptrSolution, &Q);// a*x_n
      CopyBigInt(&L, &Q);
      BigIntAdd(&Q, pValB, &Q);                    // a*x_n + b
      (void)BigIntRemainder(&Q, &V, &Q);
      (void)BigIntMultiply(&Q, ptrSolution, &Q);   // a*x_n^2 + b*x_n
      BigIntAdd(&Q, pValC, &Q);                    // a*x_n^2 + b*x_n + c
      (void)BigIntRemainder(&Q, &V, &Q);           // Numerator.
      multint(&L, &L, 2);                          // 2*a*x_n
      BigIntAdd(&L, pValB, &L);                    // 2*a*x_n + b
      (void)BigIntRemainder(&L, &V, &L);           // Denominator
      NumberLength = V.nbrLimbs;
      int NumberLengthBytes = NumberLength * (int)sizeof(limb);
      (void)memcpy(TestNbr, V.limbs, NumberLengthBytes);
      TestNbr[NumberLength].x = 0;
      GetMontgomeryParms(NumberLength);
      BigIntModularDivision(&Q, &L, &V, &Aux1);
      BigIntSubt(ptrSolution, &Aux1, ptrSolution);
      (void)BigIntRemainder(ptrSolution, &V, ptrSolution);
      if (ptrSolution->sign == SIGN_NEGATIVE)
      {
        BigIntAdd(ptrSolution, &V, ptrSolution);
      }
    }
    (void)BigIntPowerIntExp(&prime, expon, &Q);
    (void)BigIntRemainder(ptrSolution, &Q, ptrSolution);
    CopyBigInt(&Solution2[factorIndex], &Solution1[factorIndex]);
  }

  bool QuadraticTermNotMultipleOfP(
      int expon, int factorIndex,
      const BigInteger *pValA, const BigInteger* pValB, const BigInteger* pValC) {
    bool solutions;
    sol1Invalid = false;
    sol2Invalid = false;
    // Compute discriminant = ValB^2 - 4*ValA*ValC.
    (void)BigIntMultiply(pValB, pValB, &Aux[0]);
    (void)BigIntMultiply(pValA, pValC, &discriminant);
    multint(&discriminant, &discriminant, 4);
    BigIntSubt(&Aux[0], &discriminant, &discriminant);
    if ((prime.nbrLimbs == 1) && (prime.limbs[0].x == 2))
    {         /* Prime p is 2 */
      solutions = SolveQuadraticEqModPowerOf2(expon, factorIndex,
        pValA, pValB, pValC);
    }
    else
    {                        // Prime is not 2
      solutions = SolveQuadraticEqModPowerOfP(expon, factorIndex, pValA, pValB);
    }
    if (!solutions || (sol1Invalid && sol2Invalid))
    {     // Both solutions are invalid. Go out.
      if (ShowNoSolsModPrime != NULL)
      {
        ShowNoSolsModPrime(expon);
      }
      return false;
    }
    if (sol1Invalid)
    {     // Solution1 is invalid. Overwrite it with Solution2.
      CopyBigInt(&Solution1[factorIndex], &Solution2[factorIndex]);
    }
    else if (sol2Invalid)
    {     // Solution2 is invalid. Overwrite it with Solution1.
      CopyBigInt(&Solution2[factorIndex], &Solution1[factorIndex]);
    }
    else
    {              // Nothing to do.
    }
    BigIntSubt(&Solution2[factorIndex], &Solution1[factorIndex], &Aux[0]);
    if (Aux[0].sign == SIGN_NEGATIVE)
    {     // Solution2 is less than Solution1, so exchange them.
      CopyBigInt(&Aux[0], &Solution1[factorIndex]);
      CopyBigInt(&Solution1[factorIndex], &Solution2[factorIndex]);
      CopyBigInt(&Solution2[factorIndex], &Aux[0]);
    }
    if (ShowSolutionsModPrime != NULL)
    {
      ShowSolutionsModPrime(factorIndex, expon, &Q);
    }
    return true;
  }

  // Solve Ax^2 + Bx + C = 0 (mod N).
  void SolveEquation(
      pSolution solutionCback,
      pShowSolutionsModPrime showSolutionsModPrime,
      pShowNoSolsModPrime showNoSolsModPrime,
      BigInteger* pValA, const BigInteger* pValB,
      const BigInteger* pValC, BigInteger* pValN,
      BigInteger* pGcdAllParm, BigInteger* pValNnParm) {

    Solution = solutionCback;
    ShowSolutionsModPrime = showSolutionsModPrime;
    ShowNoSolsModPrime = showNoSolsModPrime;

    pGcdAll = pGcdAllParm;
    pValNn = pValNnParm;
    (void)BigIntRemainder(pValA, pValN, &Aux[0]);
    if (BigIntIsZero(&Aux[0])) {
      // Linear equation.
      SolveModularLinearEquation(pValA, pValB, pValC, pValN);
      return;
    }

    // PERF: This code will reuse factors. We might want to do that.
    #if 0
    if ((LastModulus.nbrLimbs == 0) || !BigIntEqual(&LastModulus, pValN))
    {     // Last modulus is different from ValN.
      CopyBigInt(&LastModulus, pValN);
      NumberLength = pValN->nbrLimbs;
      BigInteger2IntArray(NumberLength, nbrToFactor, pValN);
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
      NumberLength = number_length;
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

void SolveEquation(
    pSolution solutionCback,
    pShowSolutionsModPrime showSolutionsModPrime,
    pShowNoSolsModPrime showNoSolsModPrime,
    BigInteger *pValA, const BigInteger* pValB,
    const BigInteger* pValC, BigInteger* pValN,
    BigInteger *GcdAll, BigInteger *pValNn,
    QuadModLLResult *result) {
  std::unique_ptr<QuadModLL> qmll = std::make_unique<QuadModLL>();
  qmll->SolveEquation(solutionCback,
                      showSolutionsModPrime,
                      showNoSolsModPrime,
                      pValA,
                      pValB,
                      pValC,
                      pValN,
                      GcdAll,
                      pValNn);
  // fprintf(stderr, "solution1 size: %d\n", (int) sizeof (qmll->Solution1));
  memcpy(result->Solution1, qmll->Solution1, sizeof (qmll->Solution1));
  memcpy(result->Solution2, qmll->Solution2, sizeof (qmll->Solution2));
  memcpy(result->Increment, qmll->Increment, sizeof (qmll->Increment));

  memcpy(&result->prime, &qmll->prime, sizeof (BigInteger));
}
