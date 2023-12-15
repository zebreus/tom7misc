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

#include "numbers.h"
#include "bignum/big.h"
#include "bignum/big-overloads.h"
#include "montgomery64.h"

#include "base/stringprintf.h"
#include "base/logging.h"

static constexpr bool SELF_CHECK = false;
static constexpr bool VERBOSE = false;

#undef stderr
#define stderr stdout

// This used to be exposed to the caller for teach mode, but is
// now fully internal.
namespace {

inline uint64_t GetU64(const BigInt &b) {
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

  static uint64_t GetSqrtDisc(uint64_t base, uint64_t prime) {
    // PERF: After moving this to a utility, I added support for
    // base = 0, prime = 2, and non-residues. These are not
    // necessary here I think. We could have SqrtModPKnownResidue
    // to skip the Euler test, for example.
    auto so = SqrtModP(base, prime);
    CHECK(so.has_value());
    return so.value();
  }

  static
  uint64_t ComputeSquareRootModPowerOfP(uint64_t base,
                                        uint64_t prime,
                                        uint64_t discr,
                                        // prime^exp
                                        uint64_t term,
                                        int nbrBitsSquareRoot) {
    // fprintf(stderr, "CSRMPOP discr=%lld\n", discr);
    uint64_t sqrt_disc = GetSqrtDisc(base, prime);
    if (VERBOSE)
      printf("sqrt_disc: %llu\n", sqrt_disc);

    // Obtain inverse of square root stored in SqrtDisc (mod prime).
    // Port note: Alpertron computes using modular division (1 / SqrtDisc)
    // but modular inverse should be faster.
    int64_t inv = ModularInverse64(sqrt_disc, prime);

    if (SELF_CHECK) {
      std::optional<BigInt> Inv =
        BigInt::ModInverse(BigInt(sqrt_disc), BigInt(prime));
      CHECK(Inv.has_value());
      CHECK(Inv == inv);

      printf("inverse: %lld. prime %llu\n", inv, prime);
      printf("Inverse: %s\n", Inv.value().ToString().c_str());
    }

    // This starts as a 64-bit quantity, but the squaring of Q below
    // looks like it can result in larger moduli...
    uint128_t sqr_root(inv);

    int correctBits = 1;

    // Obtain nbrBitsSquareRoot correct digits of inverse square root.
    // Note that nbrBitsSquareRoot here is the exponent of p, so
    // it will usually be small. The worst case appears to be 5^26,
    // which is 75 bits.
    uint128_t q = prime;

    auto Big128 = [](uint128_t x) -> BigInt {
        return (BigInt(Uint128High64(x)) << 64) +
          BigInt(Uint128Low64(x));
      };

    auto Big128i = [&Big128](int128_t x) -> BigInt {
        if (x >= 0) {
          return Big128((uint128_t)x);
        } else {
          return -Big128((uint128_t)-x);
        }
      };

    auto IsU64 = [](uint128_t x) {
        CHECK(Uint128High64(x) == 0) << x;
        return Uint128Low64(x);
      };

    if (VERBOSE)
    printf("Starting loop; cbits: %d nbits: %d\n",
           correctBits, nbrBitsSquareRoot);


    while (correctBits < nbrBitsSquareRoot) {
      // Compute f(x) = invsqrt(x), f_{n+1}(x) = f_n * (3 - x*f_n^2)/2
      correctBits *= 2;
      // Square Q.
      q = q * q;

      CHECK(Uint128High64(sqr_root) == 0) << sqr_root;

      // 128-bit modulus, but result fits in 64 bits.
      uint64_t tmp1a = IsU64((sqr_root * sqr_root) % term);
      // Same; q can be >64 bits, but then it does nothing.
      uint64_t tmp2a = IsU64((uint128_t)tmp1a % q);

      uint128_t tmp1b = (uint128_t)tmp2a * (uint128_t)discr;
      // Tmp1 = Tmp2 * discr;

      if (VERBOSE)
      printf("tmp2 * discr = %s\n",
             Big128(tmp1b).ToString().c_str());

      if (VERBOSE)
      fprintf(stderr,
              "CSRMP(%llu, %llu, %llu, %llu, %d). Q=%s Sqrt=%s\n",
              base, prime, discr, term, nbrBitsSquareRoot,
              // q.ToString().c_str(),
              Big128(q).ToString().c_str(),
              Big128(sqr_root).ToString().c_str());

      uint128_t tmp2b = tmp1b % q;
      CHECK((int128_t)tmp2b >= 0);

      if (VERBOSE)
      printf("tmp2b: %s = %s\n",
             Big128(tmp2b).ToString().c_str(),
             Big128i((int128_t)tmp2b).ToString().c_str());
      // Tmp2 = Tmp1 % Big128(q);
      int128_t tmp2c = (int128_t)3 - (int128_t)tmp2b;

      if (VERBOSE)
      printf("threeminus: %s  sqr_root: %s\n",
             Big128i(tmp2c).ToString().c_str(),
             Big128(sqr_root).ToString().c_str());

      tmp2c %= term;
      if (tmp2c < 0) tmp2c += term;

      int128_t tmp1c = (int128_t)tmp2c * (int128_t)sqr_root;
      // Tmp1 = Tmp2 * SqrRoot;

      if (VERBOSE)
      printf("%s * sqr_root = %s\n",
             Big128i(tmp2c).ToString().c_str(),
             Big128i(tmp1c).ToString().c_str());

      int128_t tmp1d = tmp1c % (int128_t)term;
      if (tmp1d < 0) tmp1d += term;

      CHECK(Uint128High64(tmp1d) == 0);

      uint128_t tmp1e = (uint128_t)tmp1d;

      if (tmp1e & 1) {
        tmp1e += q;
      }

      if (VERBOSE)
      printf("now even tmp1: %s\n", Big128(tmp1e).ToString().c_str());

      tmp1e >>= 1;
      tmp1e %= q;

      sqr_root = tmp1e;

      if (VERBOSE)
      printf("  Loop bot: SqrRoot %s, Q %s\n",
             Big128(sqr_root).ToString().c_str(),
             Big128(q).ToString().c_str());
    }

    if (VERBOSE)
    printf("After loop, sqr_root: %s\n",
           Big128(sqr_root).ToString().c_str());

    // Get square root of discriminant from its inverse by multiplying
    // by discriminant.
    sqr_root = (sqr_root % term) * discr;

    sqr_root %= q;

    // Port note: Alpertron would just return a potentially giant number
    // here. But we only use it modularly, so reduce it now.
    sqr_root %= term;

    auto Problem = [&]() {
        return StringPrintf(
            "Sqrt(%llu) mod p=%llu (discr: %lld, term: %llu; bits: %d)\n"
            "Result = %llu\n"
            "With Q: %s\n",
            base,
            prime,
            discr,
            term,
            nbrBitsSquareRoot,
            sqr_root,
            Big128(q).ToString().c_str());
      };

    if (VERBOSE)
      fprintf(stderr, "%s", Problem().c_str());

    uint64_t ss = IsU64(sqr_root);
    if (VERBOSE)
      printf("Returning %lld\n", ss);
    return ss;
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

    if (SELF_CHECK) {
      CHECK(discr >= 0) << "We added 3 to -1 or 5+ to -4.";
    }
    int64_t sqr_root = ComputeSquareRootModPowerOfP(
        tmp, prime, (uint64_t)discr, term, nbrBitsSquareRoot);

    if (VERBOSE)
      printf("sqr_root: %lld (mod term: %lld)\n",
             sqr_root, sqr_root % term);


    // Multiply by square root of discriminant by prime^deltaZeros.
    // But deltaZeros is 0.
    CHECK(deltaZeros == 0);

    // const int correctBits = expon - deltaZeros;

    // Compute increment.
    // Q <- prime^correctBits
    // correctbits is the same as the exponent.

    int64_t sol1 = BasicModMult64(sqr_root, aodd, term);
    if (sol1 < 0) {
      sol1 += term;
    }

    Sols[factorIndex].solution1 = sol1;

    int64_t sol2 = BasicModMult64(-sqr_root, aodd, term);
    if (sol2 < 0) {
      sol2 += term;
    }

    if (VERBOSE)
      printf("Solutions %lld %lld %lld\n", sol1, sol2, term);
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
