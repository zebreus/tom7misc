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
      const std::vector<std::pair<BigInt, int>> &factors) {
    int T1;

    const BigInt GcdAll(1);

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

          /*
          */

          BigInt Quot = BigIntModularDivision(*params, Q1, L1, Term);

          {
            std::optional<BigInt> Inv = BigInt::ModInverse(L1, Term);
            CHECK(Inv.has_value());
            BigInt Res = (Q1 * Inv.value()) % Term;
            if (Res < 0) Res += Term;

            if (VERBOSE) {
              fprintf(stderr, "%s / %s mod %s = %s\n"
                      "  or %s * %s mod %s = %s\n" ,
                      Q1.ToString().c_str(),
                      L1.ToString().c_str(),
                      Term.ToString().c_str(),
                      Quot.ToString().c_str(),
                      Q1.ToString().c_str(),
                      Inv.value().ToString().c_str(),
                      Term.ToString().c_str(),
                      Res.ToString().c_str());
            }

            CHECK(Res == Quot);
          }

          Tmp[T1] = Quot;
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

  // Solve Ax^2 + Bx + C = 0 (mod 2^expon).
  // If a solution is found, writes to Solution(1,2)[factorIndex]
  // and returns true.
  bool SolveQuadraticEqModPowerOf2(int exponent, int factorIndex) {
    const BigInt A(1);
    const BigInt B(0);
    const BigInt C(1);

    CHECK(exponent > 0);
    int expon = exponent;

    if (expon != 1)
      return false;

    // ax^2 + bx + c = 0 (mod 2^expon)
    // This follows the paper Complete solving the quadratic equation mod 2^n
    // of Dehnavi, Shamsabad and Rishakani.

    // These numbers are already odd (or zero). Can simplify.

    // const BigInt &AOdd = A;
    static constexpr int bitsAZero = 0;

    const BigInt BOdd = B;
    int bitsBZero = BITS_PER_GROUP;

    // const BigInt &COdd = C;
    int bitsCZero = 0;

    if (bitsAZero > 0 && bitsBZero > 0 && bitsCZero > 0) {
      CHECK(false) << "Impossible!";
    }

    if ((bitsAZero == 0 && bitsBZero == 0 && bitsCZero == 0) ||
        (bitsAZero > 0 && bitsBZero > 0 && bitsCZero == 0)) {
      CHECK(false) << "Impossible! 2";
      return false;   // No solutions, so go out.
    }

    if (bitsAZero == 0 && bitsBZero > 0) {
      // The solution in this case requires square root.
      // compute s = ((b/2)^2 - a*c)/a^2, q = odd part of s,
      // r = maximum exponent of power of 2 that divides s.

      CHECK((B >> 1) == 0);
      // BigInt Tmp1 = B >> 1;

      // (b/2)^2
      // Tmp1 *= Tmp1;

      CHECK(A * C == 1);
      // Tmp1 -= A * C;

      // (b/2)^2 - a*c = -1
      const BigInt Tmp1(-1);

      const BigInt Mask = (BigInt(1) << expon) - 1;

      // Port note: Original code overwrote valcodd.
      // (b/2) - a*c mod 2^n
      BigInt C2 = Tmp1 & Mask;
      // It's all 1s, so it should be the same.
      CHECK(C2 == Mask);

      // int modulus_length = BigIntNumLimbs(Mask);

      // Port note: The code used to explicitly pad out AOdd (and the
      // output?) if smaller than modulus_length, but the BigInt version
      // does its own padding internally.
      // BigInt Tmp2 = GetInversePower2(AOdd, modulus_length);
      BigInt Tmp2(1);

      // Modular inverse of 1 is always 1.
      CHECK(Tmp2 == 1);

      // ((b/2) - a*c)/a mod 2^n
      // C2 *= Tmp2;
      // C2 &= Mask;

      // s = ((b/2) - a*c)/a^2 mod 2^n
      // C2 *= Tmp2;


      // Since C2 is the same as mask, anding does nothing.
      CHECK(C2 == Mask);
      // C2 &= Mask;

      // since exponent is at least 1, mask is at least 1.
      // so C2 is not 0.
      CHECK(C2 != 0);

      // It also consists of just 11111....11
      CHECK(BigInt::BitwiseCtz(C2) == 0);
      // bitsCZero = BigInt::BitwiseCtz(C2);
      // C2 >>= bitsCZero;

      constexpr int bitsCZero = 0;

      if (VERBOSE)
      fprintf(stderr, "[expon %d] C2 = %s. C2 & 7 = %d\n",
              expon,
              C2.ToString().c_str(),
              (int)(C2 & 7));

      // Since C2 is of the form 11111,
      // If C2 & 7 == 1, then expon must be 1.

      // At this moment, bitsCZero = r and ValCOdd = q.
      if ((C2 & 7) != 1 /* || (bitsCZero & 1) */) {
        // q != 1 or p2(r) == 0, so go out.
        return false;
      }

      CHECK(expon == 1);

      // Modulus is 2.
      BigInt SqrRoot = BigInt((bitsCZero > 0) ? 0 : 1);

      CHECK(SqrRoot == 1);

      // exponent not modified
      CHECK(expon == exponent);

      // x = sqrRoot - b/2a.
      {
        // New mask for exponent, which was modified above.
        const BigInt Mask2 = (BigInt(1) << expon) - 1;
        CHECK(Mask == Mask2);
        // const int modulus_length = BigIntNumLimbs(Mask);

        // BigInt Tmp2 = GetInversePower2(AOdd, modulus_length);
        BigInt Tmp2(1);

        // Inverse of 1 is always 1.
        CHECK(Tmp2 == 1);

        // b/2
        CHECK((B >> 1) == 0);
        // BigInt Tmp1 = B >> 1;
        // BigIntDivideBy2(&tmp1);

        // b/2a
        // Tmp1 *= Tmp2;

        // -b/2a
        // Tmp1 = -std::move(Tmp1);

        BigInt Tmp1(0);

        // -b/2a mod 2^expon
        // Tmp1 &= Mask;
        Tmp2 = Tmp1 + SqrRoot;

        BigInt Sol1 = Tmp2 & Mask;
        BigInt Sol2 = (Tmp1 - SqrRoot) & Mask;

        // SqrRoot is 1
        CHECK(Sol1 == 1);
        // -1 & Mask is Mask.
        CHECK(Sol2 == Mask);

        Solution1[factorIndex] = Sol1;
        Solution2[factorIndex] = Sol2;
      }
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
    // BigInt SqrRoot =
    //   BigIntModularDivision(*params, BigInt(1), SqrtDisc, Prime);

    std::optional<BigInt> Inv = BigInt::ModInverse(SqrtDisc, Prime);
    CHECK(Inv.has_value());

    BigInt SqrRoot = std::move(Inv.value());

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

    if (VERBOSE)
    fprintf(stderr, "Sqrt(%s) mod %s (bits: %d) = %s\n",
            Base.ToString().c_str(),
            Prime.ToString().c_str(),
            nbrBitsSquareRoot,
            SqrRoot.ToString().c_str());
    return SqrRoot;
  }

  // Solve Ax^2 + Bx + C = 0 (mod p^expon).
  bool SolveQuadraticEqModPowerOfP(
      int expon, int factorIndex,
      const BigInt &Prime) {

    const BigInt A(1);
    const BigInt B(0);

    // Number of bits of square root of discriminant to compute:
    //   expon + bits_a + 1,
    // where bits_a is the number of least significant bits of
    // a set to zero.
    // To compute the square root, compute the inverse of sqrt,
    // so only multiplications are used.
    //   f(x) = invsqrt(x), f_{n+1}(x) = f_n * (3 - x*f_n^2)/2
    // Get maximum power of prime which divides ValA.
    BigInt AOdd(1);
    int bitsAZero = 0;

    const BigInt VV = BigInt::Pow(Prime, expon);

    CHECK(Prime > 2);

    for (;;) {
      BigInt Tmp1 = AOdd % Prime;
      if (AOdd < 0) {
        CHECK(false) << "A starts positive, and nothing in here would "
          "make it negative";
        AOdd += Prime;
      }
      if (Tmp1 != 0) {
        break;
      }

      // PERF: I think this can be divexact?
      AOdd /= Prime;
      bitsAZero++;
    }

    // Discriminant is -4, but if there is a factor of 3^1, then
    // we do get -1 here.
    Discriminant %= VV;

    CHECK(AOdd == 1) << "Loop above will not find any divisors of 1";
    CHECK(bitsAZero == 0) << "Loop above will not find any divisors of 1";

    /*
    fprintf(stderr, "Discriminant: %s VV %s\n",
            Discriminant.ToString().c_str(), VV.ToString().c_str());
    */

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

    // Discriminant should be either -1 or -4. Neither will be
    // divisible by the prime, which has no factors of 2.
    CHECK(deltaZeros == 0) << Discriminant.ToString() << " "
                           << Prime.ToString();

    if ((deltaZeros & 1) != 0 && deltaZeros < expon) {
      CHECK(false) << "Should be impossible; deltaZeros is 0.";
      // If delta is of type m*prime^n where m is not multiple of prime
      // and n is odd, there is no solution, so go out.
      return false;
    }

    deltaZeros >>= 1;
    // Compute inverse of -2*A (mod prime^(expon - deltaZeros)).
    AOdd = BigInt(2);
    // CHECK(AOdd == 2);

    const BigInt &Tmp1 = VV;
    CHECK(Tmp1 >= 2);

    // AOdd %= Tmp1;
    CHECK(AOdd == 2) << "Since Tmp1 > 2, AOdd stays 2.";

    // Negate 2*A
    AOdd = Tmp1 - 2;

    {
      const std::optional<BigInt> Inv = BigInt::ModInverse(AOdd, Tmp1);
      CHECK(Inv.has_value()) << AOdd.ToString() << " " << Tmp1.ToString();

      AOdd = std::move(Inv.value());
      // PERF: modular inverse?
      // AOdd = BigIntModularDivision(*params, BigInt(1), AOdd, Tmp1);
      // CHECK(Inv.value() == AOdd);
    }

    CHECK(Discriminant != 0) << "Discriminant should be -1 or -4 here.";


    // Discriminant is not zero.
    // Find number of digits of square root to compute.
    // This was expon + bitsAZero - deltaZeros,
    // but ends up just being expon because those are zero.
    const int nbrBitsSquareRoot = expon;

    {
      // Equal to VV, right?
      // const BigInt Tmp1 = BigInt::Pow(Prime, nbrBitsSquareRoot);
      const BigInt &Tmp1 = VV;

      // in which case this does nothing...
      // CHECK((Discriminant % Tmp1) == Discriminant);
      // Discriminant %= Tmp1;

      CHECK(Discriminant < 0);

      if (Discriminant < 0) {
        Discriminant += Tmp1;
      }
    }

    CHECK(Discriminant >= 0) << "We added 3 to -1 or 5+ to -4.";

    BigInt Tmp = Discriminant % Prime;
    if (Tmp < 0) {
      CHECK(false) << "So this should be impossible now...";
      Tmp += Prime;
    }

    if (BigInt::Jacobi(Tmp, Prime) != 1) {
      // Not a quadratic residue, so go out.
      return false;
    }

    // Port note: This used to be passed in Aux3. This call might
    // expect more state in Aux, ugh.

    // Compute square root of discriminant.
    BigInt SqrRoot = ComputeSquareRootModPowerOfP(
        Tmp, Prime, nbrBitsSquareRoot);

    // Multiply by square root of discriminant by prime^deltaZeros.
    // But deltaZeros is 0.
    CHECK(deltaZeros == 0);

    // const int correctBits = expon - deltaZeros;

    // Compute increment.
    // Q <- prime^correctBits
    // correctbits is the same as the exponent.
    const BigInt &Q = VV;

    BigInt S1 = B + SqrRoot;

    Solution1[factorIndex] = BigInt::CMod(S1 * AOdd, Q);

    if (Solution1[factorIndex] < 0) {
      Solution1[factorIndex] += Q;
    }

    BigInt S2 = B - SqrRoot;

    Solution2[factorIndex] = BigInt::CMod(S2 * AOdd, Q);

    if (Solution2[factorIndex] < 0) {
      Solution2[factorIndex] += Q;
    }

    Increment[factorIndex] = Q;
    return true;
  }

  // If solutions found, writes normalized solutions at factorIndex
  // and returns true.
  bool QuadraticTermNotMultipleOfP(
      const BigInt &Prime,
      int expon, int factorIndex) {
    const BigInt GcdAll(1);

    const BigInt A(1);
    const BigInt B(0);
    const BigInt C(1);

    sol1Invalid = false;
    sol2Invalid = false;
    // Compute discriminant = B^2 - 4*A*C.
    Discriminant = B * B - ((A * C) << 2);

    CHECK(Discriminant == -4);

    bool solutions = false;
    CHECK(Prime > 0);
    if (Prime == 2) {
      // Prime p is 2
      solutions = SolveQuadraticEqModPowerOf2(expon, factorIndex);
    } else {
      // Prime is not 2
      solutions = SolveQuadraticEqModPowerOfP(expon, factorIndex, Prime);
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
      const BigInt &N,
      const std::vector<std::pair<BigInt, int>> &factors) {

    const BigInt A(1);
    const BigInt B(0);
    const BigInt C(1);

    CHECK(N > 1);

    const BigInt GcdAll(1);

    // (N is the modulus)

    NN = N;

    if (BigInt::DivisibleBy(A, N)) {
      CHECK(false) << "Impossible because N > 1 and A == 1";
      // Linear equation.
      // SolveModularLinearEquation(GcdAll, A, B, C, N);
      return;
    }

    const int nbrFactors = factors.size();

    Solution1.resize(nbrFactors);
    Solution2.resize(nbrFactors);
    Increment.resize(nbrFactors);
    Exponents.resize(nbrFactors);

    for (int factorIndex = 0; factorIndex < nbrFactors; factorIndex++) {
      // XXX we never return a 0 exponent, right?
      const int expon = factors[factorIndex].second;
      CHECK(expon != 0) << "Should not have any 0 exponents in factors";

      const BigInt &Prime = factors[factorIndex].first;
      if (Prime != 2 &&
          BigInt::DivisibleBy(A, Prime)) {
        CHECK(false) << "Should be impossible since Prime > 1: "
                     << A.ToString()
                     << " "
                     << Prime.ToString();

        // ValA multiple of prime means a linear equation mod prime.
        // Also prime is not 2.
        // ... there would not be solutions anyway!
        if (B == 0 && C != 0) {
          // There are no solutions: ValB=0 and ValC!=0
          return;
        }
      }

      // If quadratic equation mod p
      if (!QuadraticTermNotMultipleOfP(Prime,
                                       expon, factorIndex)) {
        return;
      }

      // Port note: This used to set the increment via the value
      // residing in Q, but now we set that in each branch explicitly
      // along with the solution. Probably could be setting Exponents
      // there too?
      Exponents[factorIndex] = 0;
    }

    PerformChineseRemainderTheorem(factors);
  }

};
}  // namespace

void SolveEquation(
    const SolutionFn &solutionCback,
    const BigInt &N,
    const std::vector<std::pair<BigInt, int>> &factors,
    bool *interesting_coverage) {
  std::unique_ptr<QuadModLL> qmll =
    std::make_unique<QuadModLL>(solutionCback);

  qmll->SolveEquation(N, factors);

  if (interesting_coverage != nullptr &&
      qmll->interesting_coverage) {
    *interesting_coverage = true;
  }
}
