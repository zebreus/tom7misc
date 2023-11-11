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

#include "quad.h"

#include <memory>
#include <vector>
#include <optional>
#include <utility>

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "factor.h"
#include "quadmodll.h"
#include "modmult.h"
#include "bigconv.h"

#include "base/stringprintf.h"
#include "base/logging.h"
#include "bignum/big.h"
#include "bignum/big-overloads.h"

using namespace std;

static constexpr bool VERBOSE = false;

namespace {

enum class LinSolType {
  // Solutions of the form (Xlin * t + Xind, Ylin * t + Yind).
  SOLUTION_FOUND,
  NO_SOLUTIONS,
  // (x, y) for any x,y.
  INFINITE_SOLUTIONS,
};

// Result of the linear equation solver; might not actually be
// a solution.
struct LinSol {
  LinSolType type = LinSolType::NO_SOLUTIONS;

  LinSol(LinSolType type) : type(type) {}

  // Only meaningful when SOLUTION_FOUND.
  BigInt Xlin, Xind;
  BigInt Ylin, Yind;

  void SwapXY() {
    std::swap(Xlin, Ylin);
    std::swap(Xind, Yind);
  }
};

enum class SolutionNumber {
  FIRST,
  SECOND,
};

enum class QmodCallbackType {
  ELLIPTIC,
};

// Output:
// nullopt: There are no solutions because gcd(P, Q, R) > 1
// some(P, Q, R) with gcd(P, Q, R) = 1.
std::optional<std::tuple<BigInt, BigInt, BigInt>>
PerformTransformation(
    const BigInt &A, const BigInt &B, const BigInt &C, const BigInt &K,
    const BigInt &Value) {
  // writes: P, Q, R, H, I

  const BigInt VA = A * Value;

  // Compute P as (at^2+bt+c)/K
  const BigInt P = ((VA + B) * Value + C) / K;

  // Compute Q <- -(2at + b).
  const BigInt Q = -((VA << 1) + B);

  // Compute R <- aK
  const BigInt R = A * K;

  // Compute gcd of P, Q and R.

  // Note: Used to write H and I as temporaries, but I think they're dead.
  const BigInt I = BigInt::GCD(BigInt::GCD(P, Q), R);
  if (I == 1) {
    // Gcd equals 1.
    return {std::make_tuple(P, Q, R)};
  }

  // No solutions because gcd(P, Q, R) > 1.
  return std::nullopt;
}

// Returns Temp0, Temp1
static std::pair<BigInt, BigInt>
UnimodularSubstitution(const BigInt &M,
                       const BigInt &Z,
                       const BigInt &O) {
  BigInt Temp0, Temp1;
  if (M < 0) {
    // Perform the substitution: x = X + Y, y = (|m|-1)X + |m|Y
    Temp0 = (Z + O);
    Temp1 = Temp0 * -M - Z;
  } else if (M == 0) {
    Temp0 = Z;
    Temp1 = O;
  } else {
    // Perform the substitution: x = mX + (m-1)Y, y = X + Y
    Temp1 = Z + O;
    Temp0 = Temp1 * M - O;
  }
  return std::make_pair(Temp0, Temp1);
}


struct Quad {
  // Solutions accumulated here.
  Solutions solutions;
  // True if we had a solution on a hyperbolic curve and so we should
  // print recursive solutions as well.
  bool hyperbolic_recursive_solution = false;

  void PrintLinear(const LinSol &sol) {
    switch (sol.type) {
    default:
    case LinSolType::NO_SOLUTIONS:
      return;

    case LinSolType::INFINITE_SOLUTIONS:
      solutions.any_integers = true;
      CHECK(false) << "Can't have infinite solutions.";
      return;

    case LinSolType::SOLUTION_FOUND:
      // Port note: This used to actually have the effect of swapping
      // xind/yind xlin/ylin.
      CHECK(false) << "Can't have infinite solutions.";
      return;
    }
  }

  void RecordSolutionXY(const BigInt &x, const BigInt &y) {
    // Negative values are obvious, since x and y appear only under
    // squares. x and y are also interchangeable.
    if (x >= 0 && y >= 0 && x <= y) {
      solutions.points.emplace_back(PointSolution{.X = x, .Y = y});
    }
  }


  // Compute coefficients of x: V3 * w^2 + V2 * w + V1
  // Returns V3, V2, V1
  std::tuple<BigInt, BigInt, BigInt> ComputeXDiscrZero(
      const BigInt &A, const BigInt &B,
      const BigInt &C, const BigInt &D,
      const BigInt &E, const BigInt &Z,
      const BigInt &J, const BigInt &K,
      const BigInt &U2) {
    // Let m = 2be - 4cd
    // U3 <- m
    BigInt U3 = (B * E - ((C * D) << 1)) << 1;
    // Compute V1 <- (x'j - k)/2a + mx'^2
    BigInt V1 = (((U2 * J) - K) / A) >> 1;
    V1 += U3 * U2 * U2;
    // Compute V2 <- (j/2a + 2mx')z
    BigInt V2 = (J / A) >> 1;
    V2 += ((U3 * U2) << 1);
    V2 *= Z;
    // Compute V3 as m*z^2
    BigInt V3 = U3 * Z * Z;
    return std::make_tuple(V3, V2, V1);
  }

  // Compute coefficients of y: V3 * w^2 + V2 * w + V1
  // Returns V3, V2, V1
  std::tuple<BigInt, BigInt, BigInt> ComputeYDiscrZero(
      const BigInt &U, const BigInt &U2,
      const BigInt &S, const BigInt &R,
      const BigInt &Z) {

    // Compute V1 <- r + sx' + ux'^2
    BigInt V1 = (U * U2 + S) * U2 + R;

    // Compute V2 <- (s + 2ux')z
    BigInt V2 = (((U * U2) << 1) + S) * Z;

    // Compute V3 <- uz^2
    BigInt V3 = U * Z * Z;

    return std::make_tuple(V3, V2, V1);
  }


  // Only the parabolic mode will swap x and y.
  void CallbackQuadModParabolic(
      bool swap_xy,
      const BigInt &A, const BigInt &B, const BigInt &C,
      const BigInt &D, const BigInt &E,
      const BigInt &U, const BigInt &V,
      const BigInt &Value) {

    CHECK(false) << "Not expecting Parabolic form.";
  }

  // Obtain next convergent of continued fraction of U/V
  // Previous convergents U1/V1, U2/V2, U3/V3.
  std::tuple<BigInt, BigInt, BigInt,
             BigInt, BigInt, BigInt> GetNextConvergent(
                 BigInt U, BigInt U1, BigInt U2,
                 BigInt V, BigInt V1, BigInt V2) {
    BigInt Tmp = BigInt::DivFloor(U, V);

    // Compute new value of U and V.
    BigInt Tmp2 = U - Tmp * V;
    U = std::move(V);
    V = Tmp2;

    // Compute new convergents: h_n = a_n*h_{n-1} + h_{n-2}
    // and also k_n = k_n*k_{n-1} + k_{n-2}
    BigInt Tmp3 = Tmp * U1 + U2;

    BigInt U3 = std::move(U2);
    U2 = std::move(U1);
    U1 = Tmp3;

    Tmp *= V1;
    Tmp += V2;
    BigInt V3 = std::move(V2);
    V2 = std::move(V1);
    V1 = Tmp;
    return std::make_tuple(U, U1, U2,
                           V, V1, V2);
  }

  // Use continued fraction of sqrt(B^2-4AC)
  // If the discriminant is 5, the method does not work: use 3, 1 and 7, 3.
  // If the convergent is r/s we get:
  // x(n+1) = Px(n) + Qy(n) + K
  // y(n+1) = Rx(n) + Sy(n) + L
  // where if b is odd:
  //        P = (r - bs)/2, Q = -cs, R = as, S = (r + bs)/2,
  // if b is even:
  //        P = r - (b/2)s, Q = -cs, R = as, S = r + (b/2)s,
  // in any case:
  //        K = (alpha*(1-P) - beta*Q) / D, L = (-alpha*R + beta*(1-S)) / D.
  void GetRecursiveSolution(
      BigInt A, BigInt B, BigInt C,
      const BigInt &ABack, const BigInt &BBack, const BigInt &CBack,
      const BigInt &Alpha, const BigInt &Beta,
      const BigInt &GcdHomog, BigInt Discr) {

    CHECK(false) << "Not expecting GetRecursiveSolution; there need "
      "to be finite solutions.";
  }


  // On input: H: value of u, I: value of v.
  // Output: ((tu - nv)*E, u*E) and ((-tu + nv)*E, -u*E)
  // If m is greater than zero, perform the substitution:
  //    x = mX + (m-1)Y, y = X + Y
  // If m is less than zero, perform the substitution:
  //    x = X + Y, y = (|m|-1)X + |m|Y
  // Do not substitute if m equals zero.
  // Returns true if solution found.
  bool NonSquareDiscrSolutionOne(
      const BigInt &M, const BigInt &E, const BigInt &K,
      const BigInt &Alpha, const BigInt &Beta, const BigInt &Div,
      const BigInt &H, const BigInt &I,
      const BigInt &Value) {

    // Port note: This used to modify the value of K based on the
    // callback type, but now we do that at the call site. (Also there
    // was something suspicious in here where it flipped the sign and
    // then set it negative.)

    // X = (tu - Kv)*E
    const BigInt Z = (Value * H - K * I) * E;
    // Y = u*E
    const BigInt O = H * E;

    // (we get here with both values for two_solutions)

    bool sol_found = false;
    // Undo unimodular substitution
    {
      const auto &[Temp0, Temp1] =
        UnimodularSubstitution(M, Z, O);
      sol_found = ShowPointOne(Temp0, Temp1, Alpha, Beta, Div);
    }

    // Z: (-tu - Kv)*E
    // O: -u*E

    // Undo unimodular substitution
    {
      const auto &[Temp0, Temp1] =
        UnimodularSubstitution(M, -Z, -O);
      sol_found = ShowPointOne(Temp0, Temp1, Alpha, Beta, Div) ||
        sol_found;
    }
    return sol_found;
  }

  // Returns true if we found a solution (and so should show
  // the recursive solution).
  bool ShowPointOne(const BigInt &X, const BigInt &Y,
                    const BigInt &Alpha, const BigInt &Beta,
                    const BigInt &Div) {

    // Check first that (X+alpha) and (Y+beta) are multiple of D.
    BigInt tmp1 = X + Alpha;
    BigInt tmp2 = Y + Beta;

    // (I think this should actually be impossible because Div comes from
    // the GCD of the coefficients.)
    CHECK(Div != 0) << "Might be shenanigans with divisibility by zero";

    if (BigInt::DivisibleBy(tmp1, Div) &&
        BigInt::DivisibleBy(tmp2, Div)) {

      if (Div != 0) {
        tmp1 = BigInt::DivExact(tmp1, Div);
        tmp2 = BigInt::DivExact(tmp2, Div);
      }

      // Not HYPERBOLIC.
      // Result box:
      RecordSolutionXY(tmp1, tmp2);
      return true;
    }
    return false;
  }


  void CheckSolutionSquareDiscr(
      const BigInt &CurrentFactor,
      const BigInt &H, const BigInt &I, const BigInt &L,
      const BigInt &M, const BigInt &Z,
      const BigInt &Alpha, const BigInt &Beta, const BigInt &Div) {

    CHECK(CurrentFactor != 0);
    BigInt N = Z / CurrentFactor;

    // (IL - HM)X = NI - cM
    // (IL - HM)Y = cL - NH

    // O = Denominator.
    BigInt O = I * L - H * M;

    // P = Numerator of X.
    BigInt P = N * I - CurrentFactor * M;

    if (VERBOSE) {
      printf("CheckSolutionSquareDiscr %s %s %s %s %s %s\n",
             P.ToString().c_str(),
             O.ToString().c_str(),
             CurrentFactor.ToString().c_str(),
             L.ToString().c_str(),
             N.ToString().c_str(),
             H.ToString().c_str());
    }

    CHECK(O != 0) << "Might have been shenanigans with O = 0?";
    if (BigInt::DivisibleBy(P, O)) {
      // X found.
      BigInt U1 = BigInt::DivExact(P, O);
      // ValP = Numerator of Y.
      P = CurrentFactor * L - N * H;

      CHECK(O != 0);
      if (BigInt::DivisibleBy(P, O)) {
        // Y found.
        BigInt U2 = BigInt::DivExact(P, O);
        // Show results.

        ShowPointOne(U1, U2, Alpha, Beta, Div);
        return;
      }
    }

    // The system of two equations does not have integer solutions.
    // No solution found.
  }

  void SolutionX(bool swap_xy,
                 BigInt Value, const BigInt &Modulus,
                 const BigInt &A, const BigInt &B, const BigInt &C,
                 const BigInt &D, const BigInt &E,
                 const BigInt &M, const BigInt &K,
                 const BigInt &U, const BigInt &V,
                 const BigInt &Alpha, const BigInt &Beta,
                 const BigInt &Div, const BigInt &Discr) {

    if (VERBOSE) {
      printf("SolutionX(%s, %s)\n",
             Value.ToString().c_str(),
             Modulus.ToString().c_str());
    }

    // If 2*value is greater than modulus, subtract modulus.
    if ((Value << 1) > Modulus) {
      Value -= Modulus;
    }

    if (VERBOSE) {
      printf("  with %s %s %s %s %s | %s %s | %s %s\n",
             A.ToString().c_str(),
             B.ToString().c_str(),
             C.ToString().c_str(),
             D.ToString().c_str(),
             E.ToString().c_str(),
             M.ToString().c_str(),
             K.ToString().c_str(),
             U.ToString().c_str(),
             V.ToString().c_str());
    }

    CallbackQuadModElliptic(A, B, C, E, M, K,
                            Alpha, Beta, Div, Discr,
                            Value);
  }

  // Solve congruence an^2 + bn + c = 0 (mod n) where n is different from zero.
  void SolveQuadModEquation(
      bool swap_xy,
      const BigInt &coeffQuadr,
      const BigInt &coeffLinear,
      const BigInt &coeffIndep,
      BigInt Modulus,
      const BigInt &A, const BigInt &B, const BigInt &C, const BigInt &D, const BigInt &E,
      const BigInt &M, const BigInt &K, const BigInt &U, const BigInt &V,
      const BigInt &Alpha, const BigInt &Beta, const BigInt &Div, const BigInt &Discr) {

    if (VERBOSE) {
      printf("[SQME] %s %s %s %s\n",
             coeffQuadr.ToString().c_str(),
             coeffLinear.ToString().c_str(),
             coeffIndep.ToString().c_str(),
             Modulus.ToString().c_str());
    }

    CHECK(Modulus > 0);

    BigInt coeff_quadr = BigInt::CMod(coeffQuadr, Modulus);
    if (coeff_quadr < 0) coeff_quadr += Modulus;

    BigInt coeff_linear = BigInt::CMod(coeffLinear, Modulus);
    if (coeff_linear < 0) coeff_linear += Modulus;

    BigInt coeff_indep = BigInt::CMod(coeffIndep, Modulus);
    if (coeff_indep < 0) coeff_indep += Modulus;

    BigInt GcdAll = BigInt::GCD(coeff_indep,
                                BigInt::GCD(coeff_quadr, coeff_linear));

    // For a GCD of zero here, original code would cause and ignore
    // a division by zero, then read 0 from the temporary.

    if (GcdAll != 0 && !BigInt::DivisibleBy(coeff_indep, GcdAll)) {
      // C must be multiple of gcd(A, B).
      // Otherwise go out because there are no solutions.
      return;
    }

    GcdAll = BigInt::GCD(Modulus, GcdAll);

    // Divide all coefficients by gcd(A, B).
    if (GcdAll != 0) {
      coeff_quadr = BigInt::DivExact(coeff_quadr, GcdAll);
      coeff_linear = BigInt::DivExact(coeff_linear, GcdAll);
      coeff_indep = BigInt::DivExact(coeff_indep, GcdAll);
      Modulus = BigInt::DivExact(Modulus, GcdAll);
    }

    BigInt ValNn = Modulus;

    if (ValNn == 1) {
      // All values from 0 to GcdAll - 1 are solutions.
      if (GcdAll > 5) {
        printf("allvalues coverage\n");
        solutions.interesting_coverage = true;

        // XXX should we call SolutionX here?
        // Seems like we would want to do so until we find
        // a solution, at least?

        CHECK(false) << "Might be possible? but unsupported";
        // ShowText("\nAll values of x between 0 and ");

        // XXX Suspicious that this modifies GcdAll in place (I
        // think just to display it?) but uses it again below.
        GcdAll -= 1;
        // ShowBigInt(GcdAll);
        // ShowText(" are solutions.");
      } else {
        // must succeed; is < 5 and non-negative

        const int n = GcdAll.ToInt().value();
        for (int ctr = 0; ctr < n; ctr++) {
          SolutionX(swap_xy,
                    BigInt(ctr), Modulus,
                    A, B, C, D, E,
                    M, K,
                    U, V,
                    Alpha, Beta, Div, Discr);
        }
      }
      return;
    }

    if (BigInt::DivisibleBy(coeff_quadr, Modulus)) {
      // Linear equation.
      printf("linear-eq coverage\n");
      solutions.interesting_coverage = true;

      if (BigInt::GCD(coeff_linear, Modulus) != 1) {
        // B and N are not coprime. Go out.
        return;
      }

      // Calculate z <- -C / B (mod N)

      // Is it worth it to convert to montgomery form for one division??
      const std::unique_ptr<MontgomeryParams> params =
        GetMontgomeryParams(Modulus);

      BigInt z =
        BigIntModularDivision(*params, coeff_indep, coeff_linear, Modulus);

      if (z != 0) {
        // not covered by cov.sh :(
        printf("new coverage z != 0\n");
        solutions.interesting_coverage = true;

        // XXX is this a typo for ValNn in the original?
        // ValN is only set in CheckSolutionSquareDiscr.
        // Since it was static, it would usually be zero here.
        // z = ValN - z;
        z = 0 - z;
      }
      BigInt Temp0 = ValNn * GcdAll;

      for (;;) {
        // also not covered :(
        printf("new coverage: loop zz");
        solutions.interesting_coverage = true;
        SolutionX(swap_xy,
                  z, Modulus,
                  A, B, C, D, E,
                  M, K,
                  U, V,
                  Alpha, Beta, Div, Discr);
        z += Modulus;
        if (z < Temp0) break;
      }

      return;
    }

    if (VERBOSE) {
      printf("[Call SolveEq] %s %s %s %s %s %s\n",
             coeff_quadr.ToString().c_str(),
             coeff_linear.ToString().c_str(),
             coeff_indep.ToString().c_str(),
             Modulus.ToString().c_str(),
             GcdAll.ToString().c_str(),
             ValNn.ToString().c_str());
    }

    bool interesting = false;
    SolveEquation(
        SolutionFn([&](const BigInt &Value) {
            this->SolutionX(
                swap_xy,
                Value,
                Modulus,
                A, B, C, D, E,
                M, K,
                U, V,
                Alpha, Beta, Div, Discr);
          }),
        coeff_quadr, coeff_linear, coeff_indep,
        Modulus, GcdAll, ValNn,
        &interesting);
    if (interesting) {
      printf("INTERESTING!\n");
      solutions.interesting_coverage = true;
    }
  }

  // Solve ax^2+bxy+cy^2 = K
  // The quadratic modular equation algorithm requires that gcd(a, n) = 1.
  // At this point gcd(a, b, c) = 1
  // The possibilities are:
  // - gcd(a, K) = 1. There is nothing to do.
  // Otherwise perform the transformation
  //    x = PX + QY, y = RX + SY with PS - QR = 1.
  // In particular perform: x = mX + (m-1)Y, y = X + Y
  // We get:
  //    (am^2+bm+c)*X^2 + (2(am^2+bm+c) - (2am+b))*XY +
  //    ((am^2+bm+c) - (2am+b) + a)*Y^2
  // Also perform: x = X + Y, y = (m-1)X + mY
  // We get:
  //    (a+(m-1)*b+(m-1)^2*c)*X^2 + (2a + b(2m-1) + 2cm(m-1))*X*Y +
  //    (a+bm+cm^2)*Y^2
  // The discriminant of the new formula does not change.
  // Compute m=1, 2, 3,... until gcd(am^2+bm+c, K) = 1.
  // When using the second formula, change sign of m so we know the
  // formula used when undoing the unimodular transformation later.

  // Since the algorithm discovers only primitive solutions,
  // i.e. solutions (x,y) where gcd(x,y) = 1, we need to solve
  //     ax'^2+bx'y'+cy'^2 = K/R^2 where R^2 is a divisor of K.
  // Then we get x = Rx', y = Ry'.
  void NonSquareDiscriminant(BigInt A, BigInt B, BigInt C,
                             BigInt K,
                             const BigInt &D,
                             BigInt Discr,
                             BigInt Alpha, BigInt Beta, const BigInt &Div) {

    // These were actually uninitialized, and probably unused?
    const BigInt U(0);
    const BigInt V(0);

    // Find GCD(a,b,c)
    BigInt GcdHomog = BigInt::GCD(BigInt::GCD(A, B), C);
    // Divide A, B, C and K by this GCD.
    if (GcdHomog != 0) {
      A = BigInt::DivExact(A, GcdHomog);
      B = BigInt::DivExact(B, GcdHomog);
      C = BigInt::DivExact(C, GcdHomog);
      K = BigInt::DivExact(K, GcdHomog);
      // Divide discriminant by the square of GCD.
      Discr /= GcdHomog;
      Discr /= GcdHomog;
    }

    if (K == 0) {
      // If k=0, the only solution is (X, Y) = (0, 0)
      (void)ShowPointOne(BigInt(0), BigInt(0), Alpha, Beta, Div);
      return;
    }

    if (VERBOSE) {
      printf("start NSD %s %s %s | %s %s | %s %s %s\n",
             A.ToString().c_str(), B.ToString().c_str(), C.ToString().c_str(),
             K.ToString().c_str(), Discr.ToString().c_str(),
             Alpha.ToString().c_str(), Beta.ToString().c_str(),
             Div.ToString().c_str());
    }

    // ughhh
    BigInt ABack = A;
    BigInt BBack = B;
    BigInt CBack = C;

    // Factor independent term.

    // Note that we modify the factors (multiplicities) in place below.
    std::vector<std::pair<BigInt, int>> factors = BigIntFactor(BigInt::Abs(K));

    if (VERBOSE) {
      for (const auto &[f, m] : factors) {
        printf("%s^%d * ", f.ToString().c_str(), m);
      }
      printf("\n");
    }
    // Find all indices of prime factors with even multiplicity.
    // (XXX parallel. could be pair)
    // Index of prime factors with even multiplicity
    // PORT NOTE: was 1-based in original code; now 0-based
    std::vector<int> indexEvenMultiplicity, originalMultiplicities;
    const int numFactors = factors.size();
    for (int i = 0; i < numFactors; i++) {
      const auto &[fact, multiplicity] = factors[i];
      if (multiplicity > 1) {
        // At least prime is squared.
        // Port note: The original code stored factorNbr, which was 1-based
        // because of the factor header.
        indexEvenMultiplicity.push_back(i);
        // Convert to even.
        originalMultiplicities.push_back(multiplicity & ~1);
      }
    }

    std::vector<int> counters(400, 0);
    std::vector<bool> is_descending(400, false);

    BigInt E = BigInt(1);
    // Loop that cycles through all square divisors of the independent term.
    BigInt M(0);
    if (BigInt::GCD(A, K) != 1) {
      // gcd(a, K) is not equal to 1.

      BigInt UU1, UU2;
      do {
        // printf("uu1uu2 loop\n");

        // Compute U1 = cm^2 + bm + a and exit loop if this
        // value is not coprime to K.

        UU2 = C * M;
        UU1 = (UU2 + B) * M + A;

        if (VERBOSE) {
          printf("%s GCD %s = %s\n",
                 UU1.ToString().c_str(),
                 K.ToString().c_str(),
                 BigInt::GCD(UU1, K).ToString().c_str());
        }

        if (BigInt::GCD(UU1, K) == 1) {
          // Increment M and change sign to indicate type.
          M = -(M + 1);
          break;
        }

        M += 1;

        // Compute U1 = am^2 + bm + c and loop while this
        // value is not coprime to K.

        UU2 = A * M;
        UU1 = (UU2 + B) * M + C;

        if (VERBOSE) {
          printf("loopy %s | %s %s | %s %s %s | %s (%s)\n",
                 M.ToString().c_str(),
                 UU1.ToString().c_str(),
                 UU2.ToString().c_str(),
                 A.ToString().c_str(),
                 B.ToString().c_str(),
                 C.ToString().c_str(),
                 K.ToString().c_str(),
                 BigInt::GCD(UU1, K).ToString().c_str());
        }

      } while (BigInt::GCD(UU1, K) != 1);

      // Compute 2am + b or 2cm + b as required.
      UU2 = (UU2 << 1) + B;

      if (M >= 0) {
        // Compute c.
        B = (UU1 - UU2);
        C = B + A;
        // Compute b.
        B += UU1;
        // Compute a.
        A = UU1;
      } else {
        // Compute c.
        B = UU1 + UU2;
        C += B;
        // Compute b.
        B += UU1;
        // Compute a.
        A = UU1;
      }
    }

    if (VERBOSE)
      printf("second NSD %s %s %s\n",
             A.ToString().c_str(),
             B.ToString().c_str(),
             C.ToString().c_str());

    // We will have to solve several quadratic modular
    // equations. To do this we have to factor the modulus and
    // find the solution modulo the powers of the prime factors.
    // Then we combine them by using the Chinese Remainder
    // Theorem. The different moduli are divisors of the
    // right hand side, so we only have to factor it once.

    CHECK(!hyperbolic_recursive_solution) << "Only set in SQME below.";

    for (;;) {

      SolveQuadModEquation(
          // Never swapping x,y on this path.
          false,
          // Coefficients and modulus
          A, B, C, BigInt::Abs(K),
          // Problem state
          A, B, C, D, E,
          M, K, U, V,
          Alpha, Beta, Div, Discr);

      // Adjust counters.
      // This modifies the factors (multiplicities) in place.
      int index;
      CHECK(indexEvenMultiplicity.size() ==
            originalMultiplicities.size());
      if (VERBOSE) printf("factors: ");
      for (index = 0; index < (int)indexEvenMultiplicity.size(); index++) {
        if (VERBOSE) printf("%d ", index);
        // Loop that increments counters.
        if (!is_descending[index]) {
          // Ascending.

          const int fidx = indexEvenMultiplicity[index];
          if (counters[index] == originalMultiplicities[index]) {
            // Next time it will be descending.
            is_descending[index] = true;
            continue;
          } else {
            BigInt UU3 = factors[fidx].first * factors[fidx].first;
            factors[fidx].second -= 2;
            // Divide by square of prime.
            K /= UU3;
            // Multiply multiplier by prime.counters[index]++
            E *= factors[fidx].first;
            counters[index] += 2;
            break;
          }
        } else {
          // Descending.
          const int fidx = indexEvenMultiplicity[index];
          if (counters[index] <= 1) {
            // Next time it will be ascending.
            is_descending[index] = false;
            continue;
          } else {
            BigInt UU3 = factors[fidx].first * factors[fidx].first;
            factors[fidx].second += 2;
            // Multiply by square of prime.
            K *= UU3;
            // Divide multiplier by prime.counters[index]++
            E /= factors[fidx].first;
            counters[index] -= 2;
            break;
          }
        }
      }

      // Note: This seems to just be a performance hint; we've changed
      // the factors array and the modulus in tandem. But it's so messy
      // to try to keep those in sync. It seems like it'd only matter if
      // the caller runs several related queries, as we do not factor
      // in a loop within this code.
      //
      // Do not try to factor the number again.

      if (index == (int)indexEvenMultiplicity.size()) {
        // All factors have been found. Exit loop.
        break;
      }
    }
    if (VERBOSE) printf(".\n");

    if (VERBOSE) {
      printf("bottom %s %s / %s %s %s %s\n",
             K.ToString().c_str(),
             E.ToString().c_str(),
             Alpha.ToString().c_str(),
             Beta.ToString().c_str(),
             GcdHomog.ToString().c_str(),
             Discr.ToString().c_str());
    }
  }

  void NegativeDiscriminant(
      const BigInt &A, const BigInt &B, const BigInt &C,
      const BigInt &K,
      const BigInt &D,
      const BigInt &Discr,
      const BigInt &Alpha, const BigInt &Beta,
      const BigInt &Div) {

    NonSquareDiscriminant(
        A, B, C, K, D, Discr, Alpha, Beta, Div);
  }

  void CallbackQuadModElliptic(
      const BigInt &A, const BigInt &B, const BigInt &C,
      const BigInt &E, const BigInt &M, const BigInt &K,
      const BigInt &Alpha, const BigInt &Beta, const BigInt &Div,
      const BigInt &Discr,
      const BigInt &Value) {

    auto pqro = PerformTransformation(A, B, C, K, Value);
    if (!pqro.has_value()) {
      // No solutions because gcd(P, Q, R) > 1.
      return;
    }

    const auto &[P, Q, R] = pqro.value();

    CHECK(Discr <= 0);

    std::optional<int64_t> plow_opt = P.ToInt();
    if (plow_opt.has_value() && plow_opt.value() >= 0) {
      int64_t plow = plow_opt.value();
      if (Discr < -4 && plow == 1) {
        // Discriminant is less than -4 and P equals 1.

        NonSquareDiscrSolutionOne(
            M, E, K,
            Alpha, Beta, Div,
            BigInt(1), BigInt(0),
            Value);

        return;
      }

      if (Discr == -4) {
        // Discriminant is equal to -4.
        BigInt G = Q >> 1;

        if (plow == 1) {
          NonSquareDiscrSolutionOne(
              M, E, K,
              Alpha, Beta, Div,
              BigInt(1), BigInt(0),
              Value);

          NonSquareDiscrSolutionOne(
              M, E, K,
              Alpha, Beta, Div,
              // (Q/2, -1)
              G, BigInt(-1),
              Value);

          return;
        } if (plow == 2) {

          NonSquareDiscrSolutionOne(
              M, E, K,
              Alpha, Beta, Div,
              // ((Q/2-1)/2, -1)
              (G - 1) >> 1, BigInt(-1),
              Value);

          NonSquareDiscrSolutionOne(
              M, E, K,
              Alpha, Beta, Div,
              // ((Q/2+1)/2, -1)
              (G + 1) >> 1, BigInt(-1),
              Value);

          return;
        }
      }

      if (Discr == -3) {
        // Discriminant is equal to -3.
        if (plow == 1) {

          NonSquareDiscrSolutionOne(
              M, E, K,
              Alpha, Beta, Div,
              BigInt(1), BigInt(0),
              Value);

          NonSquareDiscrSolutionOne(
              M, E, K,
              Alpha, Beta, Div,
              // ((Q-1)/2, -1)
              (Q - 1) >> 1, BigInt(-1),
              Value);

          NonSquareDiscrSolutionOne(
              M, E, K,
              Alpha, Beta, Div,
              // ((Q+1)/2, -1)
              (Q + 1) >> 1, BigInt(-1),
              Value);

          return;
        } else if (plow == 3) {

          // printf("plow3 coverage\n");

          NonSquareDiscrSolutionOne(
              M, E, K,
              Alpha, Beta, Div,
              // ((Q+3)/6, -1)
              (Q + 3) / 6, BigInt(-1),
              Value);

          NonSquareDiscrSolutionOne(
              M, E, K,
              Alpha, Beta, Div,
              // (Q/3, -2)
              Q / 3, BigInt(-2),
              Value);

          NonSquareDiscrSolutionOne(
              M, E, K,
              Alpha, Beta, Div,
              // ((Q-3)/6, -1)
              (Q - 3) / 6, BigInt(-1),
              Value);

          return;
        }
      }
    }

    const BigInt LL = (P << 2) / Discr;
    /*
    fprintf(stderr, "P = %s, Discr = %s, LL = %s\n",
            P.ToString().c_str(), Discr.ToString().c_str(),
            LL.ToString().c_str());
    */

    // Compute bound L = sqrt(|4P/(-D)|)
    // Port note: Original code flips the sign, but on the input
    // -10 -10 -10 -10 -8 -8, that results in sqrt(-1). Alpertron's
    // sqrt function ignores the sign.
    const BigInt L = BigInt::Sqrt(BigInt::Abs(LL));

    // Initial value of last convergent: 1/0.
    BigInt U1(1);
    BigInt V1(0);
    // Initial value of next to last convergent: 0/1.
    BigInt U2(0);
    BigInt V2(1);

    // Compute continued fraction expansion of U/V = -Q/2P.
    BigInt U = -Q;
    BigInt V = P << 1;

    while (V != 0) {
      std::tie(U, U1, U2, V, V1, V2) =
        GetNextConvergent(U, U1, U2,
                                V, V1, V2);

      // Check whether the denominator of convergent exceeds bound.
      BigInt BigTmp = L - V1;
      if (BigTmp < 0) {
        // Bound exceeded, so go out.
        break;
      }

      // Test whether P*U1^2 + Q*U1*V1 + R*V1^2 = 1.
      BigInt O = (P * U1 + Q * V1) * U1 + R * V1 * V1;

      if (O == 1) {

        // a*U1^2 + b*U1*V1 + c*V1^2 = 1.
        NonSquareDiscrSolutionOne(
            M, E, K,
            Alpha, Beta, Div,
            U1, V1,
            Value);

        std::optional<int64_t> dopt = Discr.ToInt();
        if (!dopt.has_value()) break;
        int64_t d = dopt.value();
        CHECK(d < 0) << "Original code seemed to assume this.";

        if (d < -4) {
          // Discriminant is less than -4, go out.
          break;
        }

        if (d == -3 || d == -4) {
          // Discriminant is equal to -3 or -4.
          std::tie(U, U1, U2, V, V1, V2) =
            GetNextConvergent(U, U1, U2,
                                    V, V1, V2);

          NonSquareDiscrSolutionOne(
              M, E, K,
              Alpha, Beta, Div,
              U1, V1,
              Value);

          if (d == -3) {
            std::tie(U, U1, U2, V, V1, V2) =
                GetNextConvergent(U, U1, U2, V, V1, V2);

            NonSquareDiscrSolutionOne(
                M, E, K,
                Alpha, Beta, Div,
                U1, V1,
                Value);
          }

          break;
        }
      }
    }
  }

  // PS: This is where to understand the meaning of Alpha, Beta, K, Div.
  void SolveQuadEquation(const BigInt &F) {
    BigInt A(1);
    BigInt B(0);
    BigInt C(1);
    BigInt D(0);
    BigInt E(0);

    BigInt gcd = BigInt::GCD(BigInt::GCD(A, B),
                             BigInt::GCD(BigInt::GCD(C, D),
                                         E));

    CHECK(gcd == 1) << "Expecting GCD to always be 1: " << gcd.ToString();

    // F is always divisible by gcd of 1.
    // No need to reduce coefficients by GCD of 1.

    // Test whether the equation is linear. A = B = C = 0.
    if (A == 0 && B == 0 && C == 0) {
      CHECK(false) << "Not expecting linear!\n";
      return;
    }

    // Compute discriminant: b^2 - 4ac.
    const BigInt Discr = B * B - ((A * C) << 2);
    // 0 - (1 * 4)

    CHECK(Discr == -4) << "Expecting discriminant of exactly -4.";

    if (Discr == 0) {
      CHECK(false) << "Impossible";
      return;
    }

    // Compute gcd(a,b,c).

    BigInt UU1 = BigInt::GCD(BigInt::GCD(A, B), C);
    BigInt Div, K, Alpha, Beta;

    // Discriminant is not zero.
    if (D == 0 && E == 0) {
      // Do not translate origin.
      Div = BigInt(1);
      K = -F;
      Alpha = BigInt(0);
      Beta = BigInt(0);
    } else {
      CHECK(false) << "Not expecting to translate origin.";
    }

    // If k is not multiple of gcd(A, B, C), there are no solutions.
    if (!BigInt::DivisibleBy(K, UU1)) {
      // There are no solutions.
      return;
    }

    if (K < 0) {
      // The algorithm requires the constant coefficient
      // to be positive, so we multiply both RHS and LHS by -1.
      A = -A;
      B = -B;
      C = -C;
      K = -K;
    }

    if (Discr < 0) {

      NegativeDiscriminant(A, B, C, K, D,
                           Discr, Alpha, Beta, Div);
      return;
    } else {
      CHECK(false) << "Not expecting non-negative discriminant.";
    }
  }

  void QuadBigInt(const BigInt &F) {
    const BigInt A(1);
    const BigInt B(0);
    const BigInt C(1);
    const BigInt D(0);
    const BigInt E(0);

    CHECK(F <= 0);

    // size_t preamble_size = (output == nullptr) ? 0 : output->size();

    SolveQuadEquation(F);
  }

  Quad() {}
};

}  // namespace

Solutions QuadBigInt(const BigInt &f) {
  std::unique_ptr<Quad> quad(new Quad);
  quad->QuadBigInt(f);
  return std::move(quad->solutions);
}
