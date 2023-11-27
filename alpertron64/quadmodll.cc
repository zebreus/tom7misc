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
// along with Alpertron Calculators. If not, see <http://www.gnu.org/licenses/>.

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
#include "montgomery64.h"

#include "base/stringprintf.h"
#include "base/logging.h"


#include "util.h"

static constexpr bool SELF_CHECK = false;
static constexpr bool VERBOSE = false;

// This used to be exposed to the caller for teach mode, but is
// now fully internal.
namespace {

// PERF: Compare UDiv128Rem from factorize. Might not handle
// negative args though?

// (a * b) % m
// Could maybe be wrong for largest a,b?
static inline int64_t BasicModMult64(int64_t a, int64_t b,
                                     uint64_t m) {
  // PERF: Check that this is making appropriate simplifications
  // knowing that each high word is zero.
  int128 aa(a);
  int128 bb(b);
  int128 mm(m);

  int128 rr = (aa * bb) % mm;
  return (int64_t)rr;
}

static inline uint64_t Pow64(uint64_t base, int exp) {
  uint64_t res = 1;
  while (exp) {
    if (exp & 1)
      res *= base;
    exp >>= 1;
    base *= base;
  }
  return res;
}

static inline uint64_t GetU64(const BigInt &b) {
  std::optional<uint64_t> uo = b.ToU64();
  CHECK(uo.has_value());
  return uo.value();
}

// This used to be four parallel arrays.
struct Solution {
  uint64_t solution1;
  uint64_t solution2;
  uint64_t increment;
  int exponent;
};

struct QuadModLL {
  // Return values.
  std::vector<uint64_t> values;

  // Parallel arrays (same size as number of factors)
  std::vector<Solution> Sols;

  QuadModLL() {}

  bool sol1Invalid = false;
  bool sol2Invalid = false;
  bool interesting_coverage = false;

  // Use Chinese remainder theorem to obtain the solutions.
  void PerformChineseRemainderTheorem(
      const std::vector<uint64_t> &prime_powers) {
    int T1;

    // Prime powers could go in the Sols struct too?
    const int nbrFactors = prime_powers.size();
    CHECK((int)Sols.size() == nbrFactors);
    CHECK(nbrFactors > 0);
    // Dynamically allocate temporary space. We need one per factor.
    // It could go in Sols?
    std::vector<uint64_t> Tmp(nbrFactors);

    do {
      CHECK(!Sols.empty());

      Tmp[0] = Sols[0].increment * (Sols[0].exponent >> 1);
      if ((Sols[0].exponent & 1) != 0) {
        Tmp[0] += Sols[0].solution2;
      } else {
        Tmp[0] += Sols[0].solution1;
      }

      uint64_t CurrentSolution = Tmp[0];

      // uint64_t prime = factors[0].first;
      uint64_t mult = prime_powers[0];

      for (T1 = 1; T1 < nbrFactors; T1++) {

        // Port note: This used to check that the exponent was 0,
        // but now they are pre-powered. I think this is correct?
        if (prime_powers[T1] == 1) {
          Tmp[T1] = 0;
          continue;
        }

        int expon = Sols[T1].exponent;
        Tmp[T1] = Sols[T1].increment * (expon >> 1);

        if ((expon & 1) != 0) {
          Tmp[T1] += Sols[T1].solution2;
        } else {
          Tmp[T1] += Sols[T1].solution1;
        }

        uint64_t term = prime_powers[T1];

        if (VERBOSE) {
          printf("T1 %d [exp ?]. Term: %llu\n", T1, term);
        }

        for (int E = 0; E < T1; E++) {
          // Should be int64s
          int64_t q1 = Tmp[T1] - Tmp[E];

          const int64_t inv = ModularInverse64(prime_powers[E], term);
          int64_t quot = BasicModMult64(q1, inv, term);

          // Then this is a modmult of 64 bit numbers...
          // int64_t quot = (Q1 * inv) % term;
          if (quot < 0) quot += term;
          Tmp[T1] = quot;
        }

        // PERF: If the loop above was entered, then Tmp[T1] should
        // already be in [0, term), right?
        CHECK(Tmp[T1] >= 0 && Tmp[T1] < term);
        Tmp[T1] %= term;

        // Compute currentSolution as Tmp[T1] * Mult + currentSolution
        uint64_t L2 = Tmp[T1] * mult;
        CurrentSolution += L2;
        mult *= term;
      }

      // Perform loop while V < GcdAll.
      // Since GcdAll is always 1, this ends up just getting called
      // one time, and multiplying 0 * NN.
      values.push_back(CurrentSolution);

      for (T1 = nbrFactors - 1; T1 >= 0; T1--) {
        // term = base^exp
        const uint64_t term = prime_powers[T1];

        if (Sols[T1].solution1 == Sols[T1].solution2) {
          // quad_info.Solution1[T1] == quad_info.Solution2[T1]
          Sols[T1].exponent += 2;
        } else {
          // quad_info.Solution1[T1] != quad_info.Solution2[T1]
          Sols[T1].exponent++;
        }

        // L <- Exponents[T1] * quad_info.Increment[T1]
        const uint64_t L1 = Sols[T1].increment * Sols[T1].exponent;

        // K1 <- 2 * term
        uint64_t K1 = term << 1;
        if (L1 < K1) {
          break;
        }

        Sols[T1].exponent = 0;
      }   /* end for */
    } while (T1 >= 0);
  }

  // Solve Ax^2 + Bx + C = 0 (mod 2^expon).
  // If a solution is found, writes to Solution(1,2)[factorIndex]
  // and returns true.
  bool SolveQuadraticEqModPowerOf2(int exponent, int factorIndex) {

    CHECK(exponent > 0);
    CHECK(exponent < 64);
    int expon = exponent;

    if (expon != 1)
      return false;

    // ax^2 + bx + c = 0 (mod 2^expon)
    // This follows the paper Complete solving the quadratic equation mod 2^n
    // of Dehnavi, Shamsabad and Rishakani.

    // These numbers are already odd (or zero).
    static constexpr int bitsCZero = 0;

    // The solution in this case requires square root.
    // compute s = ((b/2)^2 - a*c)/a^2, q = odd part of s,
    // r = maximum exponent of power of 2 that divides s.

    // CHECK((B >> 1) == 0);
    // (b/2)^2
    // CHECK(A * C == 1);

    // (b/2)^2 - a*c = -1
    static constexpr int64_t Tmp1 = -1;

    const uint64_t Mask = (uint64_t{1} << expon) - 1;

    // Port note: Original code overwrote valcodd.
    // (b/2) - a*c mod 2^n
    int64_t C2 = Tmp1 & (int64_t)Mask;
    // It's all 1s, so it should be the same.
    CHECK(C2 == (int64_t)Mask);

    // Port note: The code used to explicitly pad out AOdd (and the
    // output?) if smaller than modulus_length, but the BigInt version
    // does its own padding internally.
    // BigInt Tmp2 = GetInversePower2(AOdd, modulus_length);
    // int64_t Tmp2 = 1;

    // Modular inverse of 1 is always 1.
    // CHECK(Tmp2 == 1);

    // ((b/2) - a*c)/a mod 2^n
    // s = ((b/2) - a*c)/a^2 mod 2^n

    // Since C2 is the same as mask, anding does nothing.
    CHECK(C2 == (int64_t)Mask);
    // C2 &= Mask;

    // since exponent is at least 1, mask is at least 1.
    // so C2 is not 0.
    CHECK(C2 != 0);

    // It also consists of just 11111....11
    CHECK(std::countr_zero<uint64_t>((uint64_t)C2) == 0);

    if (VERBOSE)
      fprintf(stderr, "[expon %d] C2 = %lld. C2 & 7 = %d\n",
              expon,
              C2,
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
    constexpr int64_t SqrRoot = (bitsCZero > 0) ? 0 : 1;

    CHECK(SqrRoot == 1);

    // exponent not modified
    CHECK(expon == exponent);

    // x = sqrRoot - b/2a.
    {
      // New mask for exponent, which was modified above.

      // b/2
      // CHECK((B >> 1) == 0);

      // b/2a

      // -b/2a

      constexpr int64_t Tmp1 = 0;

      // -b/2a mod 2^expon
      // Tmp1 &= Mask;
      constexpr int64_t Tmp2 = SqrRoot;

      uint64_t sol1 = Tmp2 & Mask;
      uint64_t sol2 = (Tmp1 - SqrRoot) & Mask;

      // SqrRoot is 1
      CHECK(sol1 == 1);
      // -1 & Mask is Mask.
      CHECK(sol2 == Mask);

      Sols[factorIndex].solution1 = sol1;
      Sols[factorIndex].solution2 = sol2;
    }

    // Store increment.
    Sols[factorIndex].increment = ((uint64_t)1) << expon;
    return true;
  }

  static
  BigInt GetSqrtDiscOld(uint64_t base,
                        uint64_t prime) {

    const BigInt Base(base);
    const BigInt Prime(prime);

    if (VERBOSE)
    printf("GSD %llu %llu.\n", base, prime);

    const std::unique_ptr<MontgomeryParams> params =
      GetMontgomeryParams(prime);

    const int NumberLengthBytes =
      params->modulus_length * (int)sizeof(limb);

    limb aux5[params->modulus_length];
    limb aux6[params->modulus_length];
    limb aux7[params->modulus_length];
    limb aux8[params->modulus_length];
    limb aux9[params->modulus_length];

    if ((prime & 3) == 3) [[unlikely]] {
      // prime mod 4 = 3
      // subtractdivide(&Q, -1, 4);   // Q <- (prime+1)/4.

      fprintf(stderr, "Prime & 3 == 3\n");
      // Empirically, this branch isn't entered. It must fail
      // the residue test or something?

      return BigIntModularPower(*params, Base, (Prime + 1) >> 2);

    } else {

      // fprintf(stderr, "Prime & 3 != 3\n");

      CHECK((int)params->R1.size() == params->modulus_length + 1);
      CHECK((int)params->R2.size() == params->modulus_length + 1);
      CHECK((int)params->modulus.size() == params->modulus_length + 1);

      limb* toConvert = nullptr;
      // Convert discriminant to Montgomery notation.
      // PERF: We have 64-bit base. Could have like 64ToLimbs
      BigIntToFixedLimbs(Base, params->modulus_length, aux5);
      // u
      ModMult(*params, aux5, params->R2.data(), aux6);

      if (VERBOSE)
      printf("aux5 %s aux6 %s\n",
             LimbsToBigInt(aux5, params->modulus_length).ToString().c_str(),
             LimbsToBigInt(aux6, params->modulus_length).ToString().c_str());

      if ((prime & 7) == 5) {

        if (VERBOSE)
        printf("prime & 7 == 5.\n");

        // prime mod 8 = 5: use Atkin's method for modular square roots.
        // Step 1. v <- (2u)^((p-5)/8) mod p
        // Step 2. i <- (2uv^2) mod p
        // Step 3. square root of u <- uv (i-1)
        // Step 1.
        // Q <- (prime-5)/8.
        const BigInt Q((prime - 5) >> 3);

        if (VERBOSE)
        printf("q: %s\n", Q.ToString().c_str());

        // subtractdivide(&Q, 5, 8);
        // 2u
        AddBigNbrModN(aux6, aux6, aux7,
                      params->modulus.data(), params->modulus_length);
        ModPow(*params, aux7, Q, aux8);
        // At this moment aux7 is v in Montgomery notation.

        if (VERBOSE)
        printf("before step2: aux7 %s aux8 %s\n",
               LimbsToBigInt(aux7, params->modulus_length).ToString().c_str(),
               LimbsToBigInt(aux8, params->modulus_length).ToString().c_str());

        // Step 2.
        // v^2
        ModMult(*params, aux8, aux8, aux9);
        // i
        ModMult(*params, aux7, aux9, aux9);

        if (VERBOSE)
        printf("aux9 before step3: %s\n",
               LimbsToBigInt(aux9, params->modulus_length).ToString().c_str());

        // Step 3.
        // i-1
        SubtBigNbrModN(aux9, params->R1.data(), aux9,
                       params->modulus.data(), params->modulus_length);
        // v*(i-1)
        ModMult(*params, aux8, aux9, aux9);
        // u*v*(i-1)
        ModMult(*params, aux6, aux9, aux9);
        toConvert = aux9;
      } else {

        if (VERBOSE)
        printf("prime & 7 == %llu", prime & 7);

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

        uint64_t qq = prime - 1;
        CHECK(qq != 0);
        int e = std::countr_zero(qq);
        qq >>= e;
        BigInt QQ(qq);

        // Step 2.
        int x = 1;

        {
          do {
            x++;
          } while (Jacobi64(x, prime) >= 0);
        }

        // Step 3.
        // Get z <- x^q (mod p) in Montgomery notation.
        // z
        BigInt Z = ModPowBaseInt(*params, x, QQ);

        // PERF We don't use aux4 (now Z) again, so this could just
        // be a substitution?
        // Step 4.
        // y
        BigIntToFixedLimbs(Z, params->modulus_length, aux5);

        int r = e;

        const BigInt KK1 = (QQ - 1) >> 1;

        // x
        ModPow(*params, aux6, KK1, aux7);
        // v
        ModMult(*params, aux6, aux7, aux8);
        // w
        ModMult(*params, aux8, aux7, aux9);

        // Step 5
        while (memcmp(aux9, params->R1.data(),
                      NumberLengthBytes) != 0) {

          limb aux10[params->modulus_length];
          // Step 6
          int k = 0;
          (void)memcpy(aux10, aux9, NumberLengthBytes);
          do {
            k++;
            ModMult(*params, aux10, aux10, aux10);
          } while (memcmp(aux10, params->R1.data(),
                          NumberLengthBytes) != 0);

          // Step 7
          (void)memcpy(aux10, aux5, NumberLengthBytes); // d
          for (int ctr = 0; ctr < (r - k - 1); ctr++) {
            ModMult(*params, aux10, aux10, aux10);
          }
          // y
          ModMult(*params, aux10, aux10, aux5);
          r = k;
          // v
          ModMult(*params, aux8, aux10, aux8);
          // w
          ModMult(*params, aux9, aux5, aux9);
        }
        toConvert = aux8;
      }

      CHECK(toConvert == aux8 || toConvert == aux9);

      // Convert from Montgomery to standard notation.
      // Convert power to standard notation.
      (void)memset(aux7, 0, NumberLengthBytes);
      aux7[0].x = 1;
      ModMult(*params, aux7, toConvert, toConvert);

      BigInt res = LimbsToBigInt(toConvert, params->modulus_length);
      if (VERBOSE)
      printf("  returning %s\n", res.ToString().c_str());
      return res;
    }
  }


  static
  BigInt GetSqrtDisc(uint64_t base,
                     uint64_t prime) {
    if (VERBOSE)
    printf("GSD %llu %llu.\n", base, prime);

    const BigInt Base(base);
    const BigInt Prime(prime);

    MontgomeryRep64 rep(prime);
    if (VERBOSE)
    printf("(montgomery params: m %llu, inv %llu, r %llu, r^2 %llu)\n",
           rep.modulus, rep.inv, rep.r.x, rep.r_squared);

    if ((prime & 3) == 3) [[unlikely]] {
      // prime mod 4 = 3
      // subtractdivide(&Q, -1, 4);   // Q <- (prime+1)/4.

      fprintf(stderr, "Prime & 3 == 3\n");
      // Empirically, this branch isn't entered. It must fail
      // the residue test or something?

      CHECK(false) << "can implement this, but I was lazy";
      return BigInt(0);
      // return BigIntModularPower(*params, Base, (Prime + 1) >> 2);

    } else {

      // fprintf(stderr, "Prime & 3 != 3\n");

      Montgomery64 toConvert;

      // Convert discriminant to Montgomery notation.
      const Montgomery64 aux6(base);

      // u
      // ModMult(*params, aux5, params->R2.data(), aux6);

      if (VERBOSE)
      printf("aux5 %llu aux6 %llu\n",
             base,
             aux6.x);

      if ((prime & 7) == 5) {
        if (VERBOSE)
        printf("prime & 7 == 5.\n");

        // prime mod 8 = 5: use Atkin's method for modular square roots.
        // Step 1. v <- (2u)^((p-5)/8) mod p
        // Step 2. i <- (2uv^2) mod p
        // Step 3. square root of u <- uv (i-1)
        // Step 1.
        // Q <- (prime-5)/8.
        // const BigInt Q((prime - 5) >> 3);
        CHECK(prime >= 5);
        uint64_t q = (prime - 5) >> 3;

        if (VERBOSE)
        printf("q: %llu\n", q);

        const Montgomery64 aux7 = rep.Add(aux6, aux6);

        // 2u
        // AddBigNbrModN(aux6, aux6, aux7,
        // params->modulus.data(), params->modulus_length);

        const Montgomery64 aux8 = rep.Pow(aux7, q);
        // ModPow(*params, aux7, Q, aux8);
        // At this moment aux7 is v in Montgomery notation.

        if (VERBOSE)
        printf("before step2: aux7 %llu aux8 %llu\n",
               aux7.x, aux8.x);

        // Step 2.
        // v^2
        Montgomery64 aux9 = rep.Mult(aux8, aux8);
        // ModMult(*params, aux8, aux8, aux9);
        // i
        aux9 = rep.Mult(aux7, aux9);
        // ModMult(*params, aux7, aux9, aux9);

        if (VERBOSE)
        printf("aux9 before step3: %llu\n", aux9.x);

        // Step 3.
        // i-1
        aux9 = rep.Sub(aux9, rep.One());
        // SubtBigNbrModN(aux9, params->R1.data(), aux9,
        // params->modulus.data(), params->modulus_length);
        // v*(i-1)
        aux9 = rep.Mult(aux8, aux9);
        // ModMult(*params, aux8, aux9, aux9);

        // u*v*(i-1)
        aux9 = rep.Mult(aux6, aux9);
        // ModMult(*params, aux6, aux9, aux9);
        toConvert = aux9;
      } else {

        if (VERBOSE)
        printf("prime & 7 == %llu\n", prime & 7);

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

        uint64_t qq = prime - 1;
        CHECK(qq != 0);
        int e = std::countr_zero(qq);
        qq >>= e;

        // BigInt QQ(qq);

        // Step 2.
        int x = 1;

        {
          do {
            x++;
          } while (Jacobi64(x, prime) >= 0);
        }

        if (VERBOSE)
        printf("  x: %d  qq: %llu\n", x, qq);

        // Step 3.
        // Get z <- x^q (mod p) in Montgomery notation.
        // z
        Montgomery64 aux5 = rep.Pow(rep.ToMontgomery(x), qq);
        // BigInt Z = ModPowBaseInt(*params, x, QQ);

        // Step 4.
        // y
        // BigIntToFixedLimbs(Z, params->modulus_length, aux5);

        int r = e;

        CHECK(qq > 0);
        uint64_t kk1 = (qq - 1) >> 1;
        // const BigInt KK1 = (QQ - 1) >> 1;

        // x
        Montgomery64 aux7 = rep.Pow(aux6, kk1);
        // ModPow(*params, aux6, KK1, aux7);
        // v
        Montgomery64 aux8 = rep.Mult(aux6, aux7);
        // ModMult(*params, aux6, aux7, aux8);
        // w
        Montgomery64 aux9 = rep.Mult(aux8, aux7);
        // ModMult(*params, aux8, aux7, aux9);

        if (VERBOSE)
        printf("  Z %llu aux7 %llu aux8 %llu aux9 %llu\n",
               aux5.x,
               aux7.x,
               aux8.x,
               aux9.x);

        // Step 5
        while (!rep.Eq(aux9, rep.One())) {
          // memcmp(aux9, params->R1.data(), NumberLengthBytes) != 0

          if (VERBOSE)
          printf("  Start loop aux9=%llu\n", aux9.x);

          // Step 6
          int k = 0;
          Montgomery64 aux10 = aux9;
          // (void)memcpy(aux10, aux9, NumberLengthBytes);
          do {
            k++;
            aux10 = rep.Mult(aux10, aux10);
            // ModMult(*params, aux10, aux10, aux10);
          } while (!rep.Eq(aux10, rep.One()));
          // memcmp(aux10, params->R1.data(), NumberLengthBytes) != 0)

          if (VERBOSE)
          printf(
              "    r %d k %d aux5 %llu aux10 %llu\n",
              r, k, aux5.x, aux10.x);

          // Step 7
          // d
          aux10 = aux5;
          // (void)memcpy(aux10, aux5, NumberLengthBytes);
          for (int ctr = 0; ctr < (r - k - 1); ctr++) {
            aux10 = rep.Mult(aux10, aux10);
            // ModMult(*params, aux10, aux10, aux10);
          }
          if (VERBOSE)
          printf(
              "    aux10 %llu\n", aux10.x);

          // y
          aux5 = rep.Mult(aux10, aux10);
          // ModMult(*params, aux10, aux10, aux5);
          r = k;
          // v
          aux8 = rep.Mult(aux8, aux10);
          // ModMult(*params, aux8, aux10, aux8);
          // w
          aux9 = rep.Mult(aux9, aux5);
          // ModMult(*params, aux9, aux5, aux9);

          if (VERBOSE)
          printf(
              "    aux5 %llu aux8 %llu aux9 %llu aux10 %llu\n",
              aux5.x, aux8.x, aux9.x, aux10.x);
        }
        toConvert = aux8;
      }

      // CHECK(toConvert == aux8 || toConvert == aux9);

      // Convert from Montgomery to standard notation.

      // Port note: Alpertron does this by multiplying by 1 to
      // get the call to Reduce. We skip the multiplication.
      // (void)memset(aux7, 0, NumberLengthBytes);
      // aux7[0].x = 1;
      // ModMult(*params, aux7, toConvert, toConvert);
      // return LimbsToBigInt(toConvert, params->modulus_length);

      uint64_t res = rep.ToInt(toConvert);
      if (VERBOSE)
      printf("  returning %llu\n", res);
      return BigInt(res);
    }
  }

  static
  int64_t ComputeSquareRootModPowerOfP(uint64_t base,
                                       uint64_t prime,
                                       int64_t discr,
                                       // prime^exp
                                       uint64_t term,
                                       int nbrBitsSquareRoot) {
    // BigInt Base(base);
    // BigInt Prime(prime);
    // fprintf(stderr, "CSRMPOP discr=%lld\n", discr);
    const BigInt SqrtDisc = GetSqrtDisc(base, prime);
    if (SELF_CHECK) {
      const BigInt SqrtDisc2 = GetSqrtDiscOld(base, prime);
      CHECK(SqrtDisc == SqrtDisc2) << SqrtDisc.ToString() << " "
                                   << SqrtDisc2.ToString();
    }
    uint64_t sqrt_disc = GetU64(SqrtDisc);
    if (VERBOSE)
    printf("sqrt_disc: %llu\n", sqrt_disc);

    // Obtain inverse of square root stored in SqrtDisc (mod prime).
    // Port note: Alpertron computes using modular division (1 / SqrtDisc)
    // but modular inverse should be faster.
    const int64_t inv = ModularInverse64(sqrt_disc, prime);

    // This starts as a 64-bit quantity, but the squaring of Q below
    // looks like it can result in larger moduli...
    BigInt SqrRoot(inv);

    int correctBits = 1;

    BigInt Q(prime);

    // Obtain nbrBitsSquareRoot correct digits of inverse square root.
    // Note that nbrBitsSquareRoot here is the exponent of p, so
    // it will usually be small (and is less than 64 for sure).
    while (correctBits < nbrBitsSquareRoot) {
      // Compute f(x) = invsqrt(x), f_{n+1}(x) = f_n * (3 - x*f_n^2)/2
      correctBits *= 2;
      // Square Q.
      Q = Q * Q;

      BigInt Tmp1 = SqrRoot * SqrRoot;
      BigInt Tmp2 = Tmp1 % Q;

      Tmp1 = Tmp2 * discr;

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
    SqrRoot = SqrRoot * discr;
    SqrRoot %= Q;

    // Port note: Alpetron would just return a potentially giant number
    // here. But we only use it modularly, so reduce it now.
    int64_t sqr_root = SqrRoot % (int64_t)term;

    auto Problem = [&]() {
        return StringPrintf(
            "Sqrt(%llu) mod p=%llu (discr: %lld, term: %llu; bits: %d)\n"
            "Result = %s\n"
            "With Q: %s\n",
            base,
            prime,
            discr,
            term,
            nbrBitsSquareRoot,
            SqrRoot.ToString().c_str(),
            Q.ToString().c_str());
      };

    if (VERBOSE)
      fprintf(stderr, "%s", Problem().c_str());

    /*
    std::optional<int64_t> uo = SqrRoot.ToInt();
    if (!uo.has_value()) {
      // printf("\033[2J");
      fflush(stderr);
      fprintf(stderr, "\n\n\n\n\n\n\n\n\n\n\n");
      fprintf(stderr, "%s", Problem().c_str());
      fflush(stderr);

      Util::WriteFile("error.txt", Problem());

      CHECK(false) << Problem();
    }
    return uo.value();
    */
    return sqr_root;
  }

  // Solve Ax^2 + Bx + C = 0 (mod p^expon).
  bool SolveQuadraticEqModPowerOfP(
      int expon, int factorIndex,
      int64_t discr,
      uint64_t prime) {

    // fprintf(stderr, "SQEMP discr=%lld\n", discr);

    // Number of bits of square root of discriminant to compute:
    //   expon + bits_a + 1,
    // where bits_a is the number of least significant bits of
    // a set to zero.
    // To compute the square root, compute the inverse of sqrt,
    // so only multiplications are used.
    //   f(x) = invsqrt(x), f_{n+1}(x) = f_n * (3 - x*f_n^2)/2
    // Get maximum power of prime which divides ValA.

    int64_t aodd = 1;
    int bitsAZero = 0;

    const uint64_t term = Pow64(prime, expon);

    CHECK(prime > 2);

    for (;;) {
      uint64_t tmp1 = aodd % prime;
      if (aodd < 0) {
        CHECK(false) << "A starts positive, and nothing in here would "
          "make it negative";
        aodd = aodd + prime;
      }
      if (tmp1 != 0) {
        break;
      }

      aodd = aodd / prime;
      bitsAZero++;
    }

    // Discriminant is -4, but if there is a factor of 3^1, then
    // we do get -1 here.
    discr %= (int64_t)term;
    // fprintf(stderr, "Now Discr %% %llu = %lld\n", term, Discr);

    if (SELF_CHECK) {
      CHECK(aodd == 1) << "Loop above will not find any divisors of 1";
      CHECK(bitsAZero == 0) << "Loop above will not find any divisors of 1";
    }

    /*
    fprintf(stderr, "Discriminant: %s TERM %s\n",
            Discriminant.ToString().c_str(), TERM.ToString().c_str());
    */

    // Get maximum power of prime which divides discriminant.
    constexpr int deltaZeros = 0;
    if (SELF_CHECK) {
      CHECK(discr != 0);
      // Discriminant should be either -1 or -4. Neither will be
      // divisible by the prime, which has no factors of 2.
      CHECK(deltaZeros == 0) << discr << " " << prime;
    }


    // If delta is of type m*prime^n where m is not multiple of prime
    // and n is odd, there is no solution, so go out. This is impossible
    // since deltaZeros is 0.

    // does nothing size deltaZeros is already 0.
    // deltaZeros >>= 1;

    // Compute inverse of -2*A (mod prime^(expon - deltaZeros)).
    // AOdd = BigInt(2);
    // CHECK(AOdd == 2);

    // Negate 2*A
    aodd = ModularInverse64((int64_t)term - 2, term);

    if (SELF_CHECK) {
      CHECK(discr != 0) << "Discriminant should be -1 or -4 here.";
      CHECK(discr < 0) << discr;
    }

    // Discriminant is not zero.
    // Find number of digits of square root to compute.
    // This was expon + bitsAZero - deltaZeros,
    // but ends up just being expon because those are zero.
    const int nbrBitsSquareRoot = expon;

    discr += term;

    if (SELF_CHECK) {
      CHECK(discr >= 0) << "We added 3 to -1 or 5+ to -4.";
    }

    const uint64_t tmp = discr % prime;

    if (Jacobi64((int64_t)tmp, prime) != 1) {
      // Not a quadratic residue, so go out.
      return false;
    }

    // Port note: This used to be passed in Aux3. This call might
    // expect more state in Aux, ugh.

    // Compute square root of discriminant. Note that we do get
    // negative results here.
    int64_t sqr_root = ComputeSquareRootModPowerOfP(
        tmp, prime, discr, term, nbrBitsSquareRoot);

    // Multiply by square root of discriminant by prime^deltaZeros.
    // But deltaZeros is 0.
    CHECK(deltaZeros == 0);

    // const int correctBits = expon - deltaZeros;

    // Compute increment.
    // Q <- prime^correctBits
    // correctbits is the same as the exponent.
    // const BigInt Q(term);

    int64_t sol1 = BasicModMult64(sqr_root, aodd, term);
    if (sol1 < 0) {
      sol1 += term;
    }

    Sols[factorIndex].solution1 = sol1;

    int64_t sol2 = BasicModMult64(-sqr_root, aodd, term);
    if (sol2 < 0) {
      sol2 += term;
    }

    Sols[factorIndex].solution2 = sol2;
    Sols[factorIndex].increment = term;
    return true;
  }

  // If solutions found, writes normalized solutions at factorIndex
  // and returns true.
  bool QuadraticTermNotMultipleOfP(
      uint64_t prime,
      int expon, int factorIndex) {

    sol1Invalid = false;
    sol2Invalid = false;
    // Compute discriminant = B^2 - 4*A*C.
    // Discriminant = B * B - ((A * C) << 2);
    // CHECK(Discriminant == -4);
    constexpr int64_t Discriminant = -4;

    bool solutions = false;
    // CHECK(Prime > 0);
    if (prime == 2) {
      // Prime p is 2
      solutions = SolveQuadraticEqModPowerOf2(expon, factorIndex);
    } else {
      // Prime is not 2
      solutions = SolveQuadraticEqModPowerOfP(expon, factorIndex,
                                              Discriminant,
                                              prime);
    }

    if (!solutions || (sol1Invalid && sol2Invalid)) {
      // Both solutions are invalid. Go out.
      return false;
    }

    if (sol1Invalid) {
      // Solution1 is invalid. Overwrite it with Solution2.
      Sols[factorIndex].solution1 = Sols[factorIndex].solution2;
    } else if (sol2Invalid) {
      // Solution2 is invalid. Overwrite it with Solution1.
      Sols[factorIndex].solution2 = Sols[factorIndex].solution1;
    } else {
      // Nothing to do.
    }

    if (Sols[factorIndex].solution2 < Sols[factorIndex].solution1) {
      std::swap(Sols[factorIndex].solution1,
                Sols[factorIndex].solution2);
    }

    return true;
  }

  // Solve Ax^2 + Bx + C = 0 (mod N).
  void SolveEquation(
      uint64_t n,
      const std::vector<std::pair<uint64_t, int>> &factors) {

    if (SELF_CHECK) {
      uint64_t product = 1;
      for (const auto &[p, e] : factors) {
        for (int i = 0; i < e; i++)
          product *= p;
      }
      CHECK(product == n);
    }

    CHECK(n > 1);

    // (N is the modulus)

    // A (=1) can't be divisible by N (>1).

    const int nbrFactors = factors.size();

    Sols.resize(nbrFactors);

    for (int factorIndex = 0; factorIndex < nbrFactors; factorIndex++) {

      const int expon = factors[factorIndex].second;
      // Native factorization won't have exponents of zero, but the
      // modified lists can.
      if (expon == 0) {
        Sols[factorIndex].solution1 = 0;
        Sols[factorIndex].solution2 = 0;
        Sols[factorIndex].increment = 1;
        // Port note: Was uninitialized.
        Sols[factorIndex].exponent = 0;
        continue;
      }

      uint64_t prime = factors[factorIndex].first;

      // A can't be divisible by prime, since prime >= 2.

      // If quadratic equation mod p
      if (!QuadraticTermNotMultipleOfP(prime,
                                       expon, factorIndex)) {
        return;
      }

      // Port note: This used to set the increment via the value
      // residing in Q, but now we set that in each branch explicitly
      // along with the solution. Probably could be setting Exponents
      // there too?
      Sols[factorIndex].exponent = 0;
    }

    std::vector<uint64_t> prime_powers(factors.size());

    if (VERBOSE) {
      printf("For n=%lld...\n", n);
    }
    for (int i = 0; i < nbrFactors; i++) {
      prime_powers[i] = Pow64(factors[i].first,
                              factors[i].second);
      if (SELF_CHECK && prime_powers[i] == 1) {
        CHECK(factors[i].second == 0);
      }
      if (VERBOSE) {
        printf("  %lld^%d = %lld\n", factors[i].first, factors[i].second,
               prime_powers[i]);
      }
    }
    PerformChineseRemainderTheorem(prime_powers);
  }

};
}  // namespace

std::vector<uint64_t> SolveEquation(
    uint64_t n,
    const std::vector<std::pair<uint64_t, int>> &factors,
    bool *interesting_coverage) {
  std::unique_ptr<QuadModLL> qmll =
    std::make_unique<QuadModLL>();

  qmll->SolveEquation(n, factors);

  if (interesting_coverage != nullptr &&
      qmll->interesting_coverage) {
    *interesting_coverage = true;
  }

  return std::move(qmll->values);
}

int64_t SqrtModP(uint64_t base,
                 uint64_t prime,
                 int64_t discr,
                 uint64_t term,
                 int nbrBitsSquareRoot) {
  return QuadModLL::ComputeSquareRootModPowerOfP(base, prime, discr,
                                                 term,
                                                 nbrBitsSquareRoot);
}
