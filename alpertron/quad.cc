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

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "bignbr.h"
#include "factor.h"
#include "quadmodll.h"
#include "modmult.h"
#include "bigconv.h"

#include "base/stringprintf.h"
#include "base/logging.h"
#include "bignum/big.h"
#include "bignum/big-overloads.h"

static constexpr bool VERBOSE = false;

enum class LinearSolutionType {
  SOLUTION_FOUND,
  NO_SOLUTIONS,
  INFINITE_SOLUTIONS,
};

struct LinearSolution {
  LinearSolutionType type = LinearSolutionType::NO_SOLUTIONS;

  LinearSolution(LinearSolutionType type) : type(type) {}

  // Only meaningful when SOLUTION_FOUND.
  BigInt Xlin, Xind;
  BigInt Ylin, Yind;

  void SwapXY() {
    std::swap(Xlin, Ylin);
    std::swap(Xind, Yind);
  }
};

enum eShowSolution {
  ONE_SOLUTION = 0,
  TWO_SOLUTIONS,
  FIRST_SOLUTION,
  SECOND_SOLUTION,
};

enum eCallbackQuadModType {
  CBACK_QMOD_PARABOLIC = 0,
  CBACK_QMOD_ELLIPTIC,
  CBACK_QMOD_HYPERBOLIC,
};

// TODO: Test this heuristic without converting.
static bool IsBig(const BigInt &bg, int num_limbs) {
  BigInteger tmp;
  BigIntToBigInteger(bg, &tmp);
  return tmp.nbrLimbs > num_limbs;
}

static LinearSolution LinearEq(BigInt coeffX, BigInt coeffY, BigInt coeffInd) {
  if (VERBOSE) {
    printf("LinearEq %s %s %s\n",
           coeffX.ToString().c_str(),
           coeffY.ToString().c_str(),
           coeffInd.ToString().c_str());
  }
  // A linear equation. X + Y + IND = 0.

  if (coeffX == 0) {
    if (coeffY == 0) {
      if (coeffInd != 0) {
        return LinearSolution(LinearSolutionType::NO_SOLUTIONS);
      } else {
        return LinearSolution(LinearSolutionType::INFINITE_SOLUTIONS);
      }
    }

    if (BigInt::CMod(coeffInd, coeffY) != 0) {
      return LinearSolution(LinearSolutionType::NO_SOLUTIONS);
    } else {
      LinearSolution sol(LinearSolutionType::SOLUTION_FOUND);
      sol.Xind = BigInt(0);
      sol.Xlin = BigInt(1);
      // intToBigInteger(&Xind, 0);
      // intToBigInteger(&Xlin, 1);
      // PERF QuotRem
      sol.Yind = -(coeffInd / coeffY);
      // BigIntToBigInteger(yy, &Yind);
      // BigIntNegate(&Yind, &Yind);
      sol.Ylin = BigInt(0);
      // intToBigInteger(&Ylin, 0);
      return sol;
    }
  }

  if (coeffY == 0) {

    const auto [qq, rr] = BigInt::QuotRem(coeffInd, coeffX);

    if (rr != 0) {
      return LinearSolution(LinearSolutionType::NO_SOLUTIONS);
    } else {
      LinearSolution sol(LinearSolutionType::SOLUTION_FOUND);
      sol.Yind = BigInt(0);
      sol.Ylin = BigInt(1);
      // intToBigInteger(&Yind, 0);
      // intToBigInteger(&Ylin, 1);
      sol.Xind = -qq;
      // BigIntToBigInteger(qq, &Xind);
      // BigIntNegate(&Xind, &Xind);
      sol.Xlin = BigInt(0);
      // intToBigInteger(&Xlin, 0);
      return sol;
    }
  }

  const BigInt gcdxy = BigInt::GCD(coeffX, coeffY);

  if (gcdxy != 1) {
    // GCD is not 1.
    // To solve it, we first find the greatest common divisor of the
    // linear coefficients, that is: gcd(coeffX, coeffY) = gcdxy.

    // PERF divisibility test
    BigInt r = BigInt::CMod(coeffInd, gcdxy);

    if (r != 0) {
      // The independent coefficient is not a multiple of
      // the gcd, so there are no solutions.
      return LinearSolution(LinearSolutionType::NO_SOLUTIONS);
    }

    // Divide all coefficients by the gcd.
    if (gcdxy != 0) {
      // PERF known divisible
      coeffX /= gcdxy;
      coeffY /= gcdxy;
      coeffInd /= gcdxy;
    }
  }

  // Now the generalized Euclidean algorithm.
  // (BigInt may have an implementation of this?)

  BigInt U1(1);
  BigInt U2(0);
  BigInt U3 = coeffX;
  BigInt V1(0);
  BigInt V2(1);
  BigInt V3 = coeffY;

  BigInt q;

  while (V3 != 0) {

    if (VERBOSE) {
      printf("Euclid Step: %s %s %s %s %s %s\n",
             U1.ToString().c_str(),
             U2.ToString().c_str(),
             U3.ToString().c_str(),
             V1.ToString().c_str(),
             V2.ToString().c_str(),
             V3.ToString().c_str());
    }

    // q <- floor(U3 / V3).
    q = FloorDiv(U3, V3);

    {
      // T <- U1 - q * V1
      BigInt T = U1 - q * V1;
      U1 = std::move(V1);
      V1 = std::move(T);
    }

    {
      BigInt T = U2 - q * V2;
      U2 = std::move(V2);
      V2 = std::move(T);
    }

    {
      BigInt T = U3 - q * V3;
      U3 = std::move(V3);
      V3 = std::move(T);
    }
  }

  CHECK(U3 != 0);
  // Compute q as -coeffInd / U3
  q = -coeffInd / U3;

  // Compute Xind as -U1 * coeffInd / U3
  BigInt xind = U1 * q;

  BigInt xlin = coeffY;

  // Compute Yind as -U2 * coeffInd / U3
  BigInt yind = U2 * q;

  BigInt ylin = -coeffX;

  if (VERBOSE) {
    printf("Step: %s %s %s %s %s %s | %s %s %s %s\n",
           U1.ToString().c_str(),
           U2.ToString().c_str(),
           U3.ToString().c_str(),
           V1.ToString().c_str(),
           V2.ToString().c_str(),
           V3.ToString().c_str(),

           coeffX.ToString().c_str(),
           coeffY.ToString().c_str(),
           xind.ToString().c_str(),
           yind.ToString().c_str());
  }

  // Substitute variables so the independent coefficients can be minimized.
  // Reuse variables U1, U2, U3, V1, V2, V3.

  // U1 <- coeffX^2 + coeffY^2
  U1 = coeffX * coeffX + coeffY * coeffY;

  // U2 <- (coeffX^2 + coeffY^2)/2
  U2 = U1 >> 1;

  U2 += coeffX * yind;
  U2 -= coeffY * xind;

  // U1 <- delta to add to t'
  U1 = FloorDiv(U2, U1);

  if (VERBOSE) {
    printf("After subst: %s %s %s %s %s %s\n",
           U1.ToString().c_str(),
           U2.ToString().c_str(),
           U3.ToString().c_str(),
           V1.ToString().c_str(),
           V2.ToString().c_str(),
           V3.ToString().c_str());
  }

  // Xind <- Xind + coeffY * delta
  q = U1 * coeffY;
  xind += q;

  // Yind <- Yind - coeffX * delta
  q = U1 * coeffX;
  yind -= q;

  if (xlin < 0 && ylin < 0) {
    // If both coefficients are negative, make them positive.
    // printf("negate_coeff coverage\n");
    xlin = -xlin;
    ylin = -ylin;
  }

  LinearSolution sol(LinearSolutionType::SOLUTION_FOUND);

  sol.Xlin = std::move(xlin);
  sol.Xind = std::move(xind);
  sol.Ylin = std::move(ylin);
  sol.Yind = std::move(yind);

  return sol;
}

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

// First check: |u| < g.
// Second check: |u+g| > |v|
// Third check: |u-g| < |v|
// On input ValG = floor(g), g > 0.
// g is not an integer number.
static bool CheckStartOfContinuedFractionPeriod(const BigInt &U,
                                                const BigInt &V,
                                                const BigInt &G) {
  // CopyBigInt(&bigTmp, &ValU);
  // Set bigTmp to |u|
  // bigTmp.sign = SIGN_POSITIVE;
  // Compute bigTmp as floor(g) - |u|
  BigInt BigTmp = G - BigInt::Abs(U);
  // BigIntSubt(&ValG, &bigTmp, &bigTmp);

  if (BigTmp >= 0) {
    BigInt Tmp1 = BigInt::Abs(V);
    // First check |u| < g passed.
    // CopyBigInt(&Tmp1, &ValV);
    // Set Tmp1 to |v|
    // Tmp1.sign = SIGN_POSITIVE;
    // Compute Tmp2 as u + floor(g) which equals floor(u+g)
    BigInt Tmp2 = U + G;
    // BigIntAdd(&ValU, &ValG, &Tmp2);

    if (Tmp2 < 0) {
      // Round to number nearer to zero.
      Tmp2 += 1;
      // addbigint(&Tmp2, 1);
    }

    Tmp2 = BigInt::Abs(Tmp2);

    // Compute Tmp2 as floor(|u+g|)
    // Tmp2.sign = SIGN_POSITIVE;

    // Compute bigTmp as floor(|u+g|) - |v|
    BigTmp = Tmp2 - Tmp1;
    // BigIntSubt(&Tmp2, &Tmp1, &bigTmp);

    if (BigTmp >= 0) {
      // Second check |u+g| > |v| passed.
      // Compute Tmp2 as u - floor(g)
      Tmp2 = U - G;
      // BigIntSubt(&ValU, &ValG, &Tmp2);

      if (Tmp2 <= 0) {
        // Round down number to integer.
        Tmp2 -= 1;
        // addbigint(&Tmp2, -1);
      }

      Tmp2 = BigInt::Abs(Tmp2);

      // Compute Tmp2 as floor(|u-g|)
      // Tmp2.sign = SIGN_POSITIVE;
      // Compute Tmp2 as |v| - floor(|u-g|)
      // BigIntSubt(&Tmp1, &Tmp2, &bigTmp);
      BigTmp = Tmp1 - Tmp2;

      if (BigTmp >= 0) {
        // Third check |u-g| < |v| passed.
        // Save U and V to check period end.
        return true;
      }
    }
  }
  return false;
}



struct Quad {
  // XXX can be retired for two_solutions arg
  enum eShowSolution showSolution;
  // TODO: Lots of these could be local; dynamically sized.
  enum eCallbackQuadModType callbackQuadModType;
  char isDescending[400];
  BigInteger Aux0, Aux1, Aux2, Aux3;
  BigInteger ValA;
  BigInteger ValB;
  BigInteger ValC;
  BigInteger ValD;
  BigInteger ValE;
  BigInteger ValF;
  BigInteger ValI;
  BigInteger ValL;
  BigInteger ValM;
  BigInteger ValN;
  BigInteger ValO;
  BigInteger ValP;
  BigInteger ValQ;
  BigInteger ValU;
  BigInteger ValV;
  BigInteger ValG;
  BigInteger ValR;
  BigInteger ValK;
  BigInteger ValZ;
  BigInteger ValAlpha;
  BigInteger ValBeta;
  BigInteger ValDen;
  BigInteger ValDiv;
  BigInteger ValABak;
  BigInteger ValBBak;
  BigInteger ValCBak;
  BigInteger ValGcdHomog;
  BigInteger Tmp1;
  BigInteger Tmp2;
  int SolNbr;
  int showRecursiveSolution;
  // BigInt Xind, Yind, Xlin, Ylin;
  bool solFound;

  bool ExchXY = false;

  const char *divgcd;
  const char *varT = "t";
  BigInteger discr;
  BigInteger U1;
  BigInteger U2;
  BigInteger U3;
  BigInteger V1;
  BigInteger V2;
  BigInteger V3;
  BigInteger bigTmp;
  BigInteger startPeriodU;
  BigInteger startPeriodV;

  BigInteger modulus;

  std::optional<BigInt> Xplus;
  std::optional<BigInt> Xminus;
  std::optional<BigInt> Yplus;
  std::optional<BigInt> Yminus;

  // These will point to xplus/minus etc. above, and those
  // get set through the pointers. Gross!
  std::optional<BigInt> *Xbak = nullptr, *Ybak = nullptr;

  int equationNbr;
  int contfracEqNbr;
  const char *ptrVarNameX;
  const char *ptrVarNameY;
  const char *varX;
  const char *varY;
  bool firstSolutionX;
  bool positiveDenominator;

  // Functions that have been expunged of state above.
  struct Clean {

    std::string *output = nullptr;
    // presentational. Can probably remove it.
    bool also = false;

    void ShowText(const std::string &text) {
      if (output != nullptr)
        *output += text;
    }

    inline void ShowChar(char c) {
      if (output != nullptr)
        output->push_back(c);
    }

    void showMinus() {
      if (output != nullptr)
        *output += "&minus;";
    }

    void ShowNumber(const BigInteger* value) {
      if (output != nullptr) {
        BigInt b = BigIntegerToBigInt(value);
        *output += b.ToString();
      }
    }

    void ShowBigInt(const BigInt &value) {
      if (output != nullptr) {
        *output += value.ToString();
      }
    }

    void showInt(int value) {
      if (output != nullptr) {
        StringAppendF(output, "%d", value);
      }
    }

    void showSquare() {
      ShowText("&sup2;");
    }

    void showAlso() {
      if (also) {
        ShowText("and also:<br>");
      } else {
        also = 1;
      }
    }

    void ShowLin(const BigInt &coeffX, const BigInt &coeffY,
                 const BigInt &coeffInd,
                 const char *x, const char *y) {
      LinearSolutionType t;
      t = Show(coeffX, x, LinearSolutionType::SOLUTION_FOUND);
      t = Show(coeffY, y, t);
      Show1(coeffInd, t);
    }

    void ShowLinInd(const BigInt &lin, const BigInt &ind,
                    const std::string &var) {
      if (ind == 0 && lin == 0) {
        ShowText("0");
      }
      if (ind != 0) {
        ShowBigInt(ind);
      }
      ShowChar(' ');

      if (lin < 0) {
        showMinus();
      } else if (lin != 0 && ind != 0) {
        ShowChar('+');
      } else {
        // Nothing to do.
      }
      ShowChar(' ');
      if (lin != 0) {
        if (BigInt::Abs(lin) != 1) {
          // abs(lin) is not 1
          // CopyBigInt(&Aux0, lin);
          // Do not show negative sign twice.
          // Aux0.sign = SIGN_POSITIVE;
          ShowBigInt(BigInt::Abs(lin));
        }
        ShowChar(' ');
        ShowText(var);
      }
    }


    void PrintLinear(const LinearSolution &sol, const string &var) {
      if (sol.type == LinearSolutionType::NO_SOLUTIONS) {
        return;
      }
      if (var == "t") {
        showAlso();
      }
      if (sol.type == LinearSolutionType::INFINITE_SOLUTIONS) {
        ShowText("<p>x, y: any integer</p>");
        return;
      }
      // Port note: This used to actually have the effect of swapping
      // xind/yind xlin/ylin.
      ShowText("<p>x = ");
      ShowLinInd(sol.Xlin, sol.Xind, var);
      ShowText("<br>y = ");
      ShowLinInd(sol.Ylin, sol.Yind, var);
      ShowText("</p>");
      return;
    }

    void ShowSolutionXY(const BigInt &x, const BigInt &y) {
      ShowText("<p>x = ");
      ShowBigInt(x);
      ShowText("<BR>y = ");
      ShowBigInt(y);
      ShowText("</p>");
    }

    void PrintQuad(const BigInt &T2, const BigInt &T,
                   const BigInt &Ind,
                   const char *var1, const char *var2) {
      if (BigInt::Abs(T2) == 1) {
        // abs(coeffT2) = 1
        if (T2 == 1) {
          // coeffT2 = 1
          ShowChar(' ');
        } else {
          // coeffT2 = -1
          showMinus();
        }
        ShowText(var1);
        showSquare();
      } else if (T2 != 0) {
        // coeffT2 is not zero.
        ShowBigInt(T2);
        ShowChar(' ');
        ShowText(var1);
        showSquare();
      } else {
        // Nothing to do.
      }

      if (T < 0) {
        ShowText(" &minus; ");
      } else if (T != 0 && T2 != 0) {
        ShowText(" + ");
      } else {
        // Nothing to do.
      }

      if (BigInt::Abs(T) == 1) {
        // abs(coeffT) = 1
        ShowText(var1);
        ShowText("&#8290;");
        if (var2 != NULL) {
          ShowText(var2);
        }
        ShowText(" ");
      } else if (T != 0) {
        // Port note: original called showlimbs if negative, which I
        // think is just a way of printing the absolute value without
        // any copying.
        ShowBigInt(BigInt::Abs(T));
        ShowText(" ");
        ShowText(var1);
        if (var2 != NULL) {
          ShowText("&#8290;");
          ShowText(var2);
        }
      } else {
        // Nothing to do.
      }

      if (Ind != 0) {
        if (T != 0 || T2 != 0) {
          if (Ind < 0) {
            ShowText(" &minus; ");
          } else {
            ShowText(" + ");
          }
        } else if (Ind < 0) {
          ShowText(" &minus;");
        } else {
          // Nothing to do.
        }

        if (var2 == NULL) {
          // Same trick for abs value.
          ShowBigInt(BigInt::Abs(Ind));
        } else if (BigInt::Abs(Ind) != 1) {
          ShowBigInt(BigInt::Abs(Ind));
          ShowText("&nbsp;&#8290;");
          ShowText(var2);
          showSquare();
        } else {
          ShowText(var2);
          showSquare();
        }
      }
    }


    // XXX why does this take/return "linear solution type" ?
    LinearSolutionType Show(const BigInt &num, const string &str,
                            LinearSolutionType t) {
      LinearSolutionType tOut = t;
      if (num != 0) {
        // num is not zero.
        if (t == LinearSolutionType::NO_SOLUTIONS && num >= 0) {
          ShowText(" +");
        }

        if (num < 0) {
          ShowText(" -");
        }

        if (num != 1 && num != -1) {
          // num is not 1 or -1.
          ShowChar(' ');
          ShowBigInt(BigInt::Abs(num));
          ShowText("&nbsp;&#8290;");
        } else {
          ShowText("&nbsp;");
        }

        if (output != nullptr)
          *output += str;

        if (t == LinearSolutionType::SOLUTION_FOUND) {
          tOut = LinearSolutionType::NO_SOLUTIONS;
        }
      }
      return tOut;
    }

    void Show1(const BigInt &num, LinearSolutionType t) {
      const LinearSolutionType u = Show(num, "", t);
      ShowChar(' ');
      // Port note: This used to test u & 1 as a "trick" for detecting NO_SOLUTIONS?
      if (u != LinearSolutionType::NO_SOLUTIONS || num == 1 || num == -1) {
        // Show absolute value of num.
        ShowBigInt(BigInt::Abs(num));
      }
    }

    void ShowEq(
        const BigInt &coeffA, const BigInt &coeffB,
        const BigInt &coeffC, const BigInt &coeffD,
        const BigInt &coeffE, const BigInt &coeffF,
        const char *x, const char *y) {

      LinearSolutionType t;
      string vxx = StringPrintf("%s&sup2;", x);
      t = Show(coeffA, vxx, LinearSolutionType::SOLUTION_FOUND);

      string vxy = StringPrintf("%s&#8290;%s", x, y);
      t = Show(coeffB, vxy, t);

      string vyy = StringPrintf("%s&sup2;", y);
      t = Show(coeffC, vyy, t);

      t = Show(coeffD, x, t);

      t = Show(coeffE, y, t);
      Show1(coeffF, t);
    }

    void ShowRecSol(char variable,
                    const BigInt &cx,
                    const BigInt &cy,
                    const BigInt &ci) {
      ShowChar(variable);
      ShowText("<sub>n+1</sub> = ");
      LinearSolutionType t = Show(cx, "x<sub>n</sub>",
                                  LinearSolutionType::SOLUTION_FOUND);
      t = Show(cy, "y<sub>n</sub>", t);
      Show1(ci, t);
    }

    void ShowResult(const char *text, const BigInt &value) {
      ShowText(text);
      ShowText(" = ");
      ShowBigInt(value);
      ShowText("<br>");
    }


    // Compute coefficients of x: V1 + V2 * w + V3 * w^2
    // Returns V1, V2, V3
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
      return std::make_tuple(V1, V2, V3);
    }

    // Compute coefficients of y: V1 + V2 * w + V3 * w^2
    // Returns V1, V2, V3
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

      return std::make_tuple(V1, V2, V3);
    }


    void CallbackQuadModParabolic(
        bool swap_xy,
        const BigInt &A, const BigInt &B, const BigInt &C,
        const BigInt &D, const BigInt &E,
        const BigInt &U, const BigInt &V,
        const BigInt &I,
        const BigInt &Value) {
      // The argument of this function is T. t = T - d + uk (k arbitrary).
      // Compute ValR <- (T^2 - v)/u

      BigInt R = ((Value * Value) - V) / U;
      // Compute ValS as 2*T
      BigInt S = Value << 1;

      // Find k from the congruence
      //  jk = K (mod z) where j = u-bs, K = d+br-T, z = 2a
      // Compute j <- u-bs
      BigInt J = U - B * S;
      // Compute K <- d+br-T
      BigInt K = (D - B * R) - Value;
      // Compute z <- 2a
      BigInt Z = A << 1;
      // If K is not multiple of gcd(j, z) there is no solution.
      BigInt BigTmp = BigInt::GCD(J, Z);
      CHECK(BigTmp != 0);
      if (K % BigTmp != 0) {
        return;
      }

      // Compute g = gcd(j, K, z), then recalculate j <- j/g, K <- K/g, z <- z/g
      BigInt U1 = BigInt::GCD(BigTmp, K);
      CHECK(U1 != 0);
      // U2 <- j
      BigInt U2 = J / U1;
      // U3 <- K
      BigInt U3 = K / U1;
      if (U1 != 0) Z /= U1;
      // Use positive sign for modulus.
      Z = BigInt::Abs(Z);

      if (Z != 0) U2 %= Z;
      // PERF: Can just use Mod?
      if (U2 < 0) U2 += Z;

      if (Z != 0) U3 %= Z;
      if (U3 < 0) U3 += Z;

      if (U2 != 0) {
        // M and N equal zero.
        // In this case 0*k = 0 (mod z) means any k is valid.
        Z = BigInt(1);
      } else {
        // U2 <- x'
        U2 = GeneralModularDivision(U2, U3, Z);
      }

      {
        const auto &[VV1, VV2, VV3] =
          swap_xy ?
          ComputeYDiscrZero(U, U2, S, R, Z) :
          ComputeXDiscrZero(A, B, C, D, E, Z, J, K, U2);

        showAlso();

        // Result box:
        ShowText("<p><var>x</var> = ");
        PrintQuad(VV3, VV2, VV1,
                        "<var>k</var>", NULL);
        ShowText("<br>");
      }

      {
        const auto &[VV1, VV2, VV3] =
          swap_xy ?
          ComputeXDiscrZero(A, B, C, D, E, Z, J, K, U2) :
          ComputeYDiscrZero(U, U2, S, R, Z);

        ShowText("<var>y</var> = ");
        PrintQuad(VV3, VV2, VV1,
                        "<var>k</var>", NULL);
      }

      ShowText("</p>");
    }

    // Obtain next convergent of continued fraction of ValU/ValV
    // Previous convergents U1/V1, U2/V2, U3/V3.
    std::tuple<BigInt, BigInt, BigInt, BigInt,
               BigInt, BigInt, BigInt, BigInt> GetNextConvergent(
                   BigInt U, BigInt U1, BigInt U2,
                   BigInt V, BigInt V1, BigInt V2) {
      BigInt Tmp = FloorDiv(U, V);
      // floordiv(&ValU, &ValV, &bigTmp);

      // Values of U3 and V3 are not used, so they can be overwritten now.
      // Compute new value of ValU and ValV.
      BigInt Tmp2 = U - Tmp * V;
      U = std::move(V);
      V = Tmp2;
      // (void)BigIntMultiply(&bigTmp, &ValV, &U3);
      // BigIntSubt(&ValU, &U3, &U3);
      // CopyBigInt(&ValU, &ValV);
      // CopyBigInt(&ValV, &U3);

      // Compute new convergents: h_n = a_n*h_{n-1} + h_{n-2}
      // and also k_n = k_n*k_{n-1} + k_{n-2}
      BigInt Tmp3 = Tmp * U1 + U2;
      // (void)BigIntMultiply(&bigTmp, &U1, &V3);
      // BigIntAdd(&V3, &U2, &V3);

      BigInt U3 = std::move(U2);
      U2 = std::move(U1);
      U1 = Tmp3;
      // CopyBigInt(&U3, &U2);
      // CopyBigInt(&U2, &U1);
      // CopyBigInt(&U1, &V3);

      Tmp *= V1;
      Tmp += V2;
      // (void)BigIntMultiply(&bigTmp, &V1, &bigTmp);
      // BigIntAdd(&bigTmp, &V2, &bigTmp);
      BigInt V3 = std::move(V2);
      V2 = std::move(V1);
      V1 = Tmp;
      // CopyBigInt(&V3, &V2);
      // CopyBigInt(&V2, &V1);
      // CopyBigInt(&V1, &bigTmp);
      return std::make_tuple(U, U1, U2, U3,
                             V, V1, V2, V3);
    }

    void ShowAllRecSols(
        // XXX original code modifies these; was that just a bug?
        BigInt P, BigInt Q,
        BigInt R, BigInt S,
        BigInt K, BigInt L,
        const BigInt &Alpha, const BigInt &Beta) {

      if (IsBig(P, 2) || IsBig(Q, 2)) {
        if (Alpha == 0 && Beta == 0) {
          ShowText("x<sub>n+1</sub> = P&nbsp;&#8290;x<sub>n</sub> + "
                   "Q&nbsp;&#8290;y<sub>n</sub><br>"
                   "y<sub>n+1</sub> = R&nbsp;&#8290;x<sub>n</sub> + "
                   "S&nbsp;&#8290;y<sub>n</sub></p><p>");
        } else {
          ShowText("x<sub>n+1</sub> = P&nbsp;&#8290;x<sub>n</sub> + "
                   "Q&nbsp;&#8290;y<sub>n</sub> + K<br>"
                   "y<sub>n+1</sub> = R&nbsp;&#8290;x<sub>n</sub> + "
                   "S&nbsp;&#8290;y<sub>n</sub> + L</p><p>");
        }
        ShowText("where:</p><p>");
        ShowResult("P", P);
        ShowResult("Q", Q);
        if (Alpha != 0 || Beta != 0) {
          ShowResult("K", K);
        }
        ShowResult("R", R);
        ShowResult("S", S);
        if (Alpha != 0 || Beta != 0) {
          ShowResult("L", L);
        }
      } else {
        ShowRecSol('x', P, Q, K);
        ShowText("<br>");
        ShowRecSol('y', R, S, L);
      }

      // Compute x_{n-1} from x_n and y_n
      // Compute new value of K and L as: Knew <- L*Q - K*S and Lnew <- K*R - L*P
      BigInt Tmp1 = L * Q - K * S;
      L = K * R - L * P;
      K = std::move(Tmp1);

      // Compute new values of P, Q, R and S as:
      // Pnew <- S, Qnew <- -Q, Rnew <- -R, Snew <- P
      Q = -std::move(Q);
      R = -std::move(R);

      BigInt Tmp = P;
      P = S;
      S = std::move(Tmp);

      ShowText("<p>and also:</p>");
      if (IsBig(P, 2) || IsBig(Q, 2)) {
        ShowText("<p>");
        ShowResult("P", P);
        ShowResult("Q", Q);
        if (Alpha != 0 || Beta != 0) {
          ShowResult("K", K);
        }
        ShowResult("R", R);
        ShowResult("S", S);
        if (Alpha != 0 || Beta != 0) {
          ShowResult("L", L);
        }
      } else {
        ShowRecSol('x', P, Q, K);
        ShowText("<br>");
        ShowRecSol('y', R, S, L);
      }
      ShowText("</p>");
    }

    bool SolutionFoundFromContFraction(bool isBeven,
                                       int V,
                                       const BigInt &Alpha,
                                       const BigInt &Beta,
                                       const BigInt &A,
                                       const BigInt &B,
                                       const BigInt &C,
                                       const BigInt &Discr,
                                       const BigInt &U1,
                                       const BigInt &V1) {
      BigInt P, S;
      if (isBeven) {
        // P <- r - (b/2)s
        // S <- r + (b/2)s
        BigInt tmp = (B >> 1) * V1;
        P = U1 - tmp;
        S = U1 + tmp;
      } else {
        // P <- r - bs
        // S <- r + bs
        BigInt tmp = B * V1;
        P = U1 - tmp;
        S = U1 + tmp;
        if (V == 4) {
          // P <- (r - bs)/2
          // S <- (r + bs)/2
          P >>= 1;
          S >>= 1;
        }
      }

      // Q <- -cs
      BigInt Q = -(C * V1);
      // R <- as
      BigInt R = A * V1;

      if (!isBeven && V == 1) {
        // Q <- -2cs
        Q <<= 1;
        // R <- 2as
        R <<= 1;
      }

      BigInt K = (Alpha * P) + (Beta * Q);
      BigInt L = (Alpha * R) + (Beta * S);

      if (VERBOSE) {
        printf("contf: %s %s %s %s | %s %s %s %s | %s %s | %s %s\n",
               A.ToString().c_str(),
               B.ToString().c_str(),
               C.ToString().c_str(),
               Discr.ToString().c_str(),
               P.ToString().c_str(),
               Q.ToString().c_str(),
               R.ToString().c_str(),
               S.ToString().c_str(),
               Alpha.ToString().c_str(),
               Beta.ToString().c_str(),
               K.ToString().c_str(),
               L.ToString().c_str());
      }

      CHECK(Discr != 0) << "Original code may have had shenanigans "
        "with dividing by zero";

      // Check whether alpha - K and beta - L are multiple of discriminant.
      // PERF divisibility checks
      if (BigInt::CMod(Alpha - K, Discr) == 0 &&
          BigInt::CMod(Beta - L, Discr) == 0) {
        // Solution found.
        // PERF as below, known-divisible tests or quotrem.
        K = (Alpha - K) / Discr;
        L = (Beta - L) / Discr;
        ShowAllRecSols(P, Q, R, S,
                       K, L, Alpha, Beta);
        return true;
      }

      // Check whether alpha + K and beta + L are multiple of discriminant.
      // PERF divisibility checks
      if (BigInt::CMod(Alpha + K, Discr) == 0 &&
          BigInt::CMod(Beta + L, Discr) == 0) {
        // Solution found.
        // PERF: Use quotrem, or known-divisible test!
        K = (Alpha + K) / Discr;
        L = (Beta + L) / Discr;

        ShowAllRecSols(-P, -Q, -R, -S,
                       K, L, Alpha, Beta);
        return true;
      }
      return false;
    }


  };  // Clean


  Clean clean;

  void MarkUninitialized() {
    // Port note: There are various interleaved code paths where
    // different state (e.g. callback type) results in these member
    // variables being initialized or not. At least set them to
    // valid state so that we can convert them to BigInt (and discard).
    for (BigInteger *b : {
          &Aux0, &Aux1, &Aux2, &Aux3,
          &ValA, &ValB, &ValC, &ValD, &ValE, &ValF,
          &ValI, &ValL, &ValM, &ValN, &ValO, &ValP, &ValQ,
          &ValU, &ValV, &ValG, &ValR, &ValK, &ValZ,
          &ValAlpha, &ValBeta, &ValDen, &ValDiv,
          &ValABak, &ValBBak, &ValCBak,
          &ValGcdHomog, &Tmp1, &Tmp2,
          &discr,
          &U1, &U2, &U3, &V1, &V2, &V3,
          &bigTmp, &startPeriodU, &startPeriodV,
          &modulus}) {
      intToBigInteger(b, 0xCAFE);
    }
  }

  // This weird function either shows the solution or continues
  // to try to find the minimum, storing the state in Xbak, Ybak.
  // XXX need to take some kind of state to accumulate Xbak, Ybak...
  void ShowXY(bool two_solutions, bool swap_xy, const BigInt &X, const BigInt &Y) {
    if (two_solutions) {
      solFound = true;

      CHECK(Xbak != nullptr);
      CHECK(Ybak != nullptr);

      // This is basically nullopt state.
      if (!Xbak->has_value()) {
        Xbak->emplace(X);
        Ybak->emplace(Y);
      } else {
        CHECK(Xbak->has_value());
        CHECK(Ybak->has_value());

        // Use the lowest of |X| + |Y| and |Xbak| + |Ybak|
        BigInt BX = Xbak->value();
        BigInt BY = Ybak->value();

        if (BigInt::Abs(X) + BigInt::Abs(Y) <=
            BigInt::Abs(BX) + BigInt::Abs(BY)) {
          // At this moment |x| + |y| <= |xbak| + |ybak|
          Xbak->emplace(X);
          Ybak->emplace(Y);
        }
      }

    } else {
      // ONE_SOLUTION: Show it.
      ShowXYOne(swap_xy, X, Y);
    }
  }

  void ShowXYOne(bool swap_xy, const BigInt &X, const BigInt &Y) {
    CHECK(showSolution == ONE_SOLUTION);
    clean.showAlso();
    if (swap_xy)
      clean.ShowSolutionXY(Y, X);
    else
      clean.ShowSolutionXY(X, Y);
  }

  // TODO: Try to make this dispatch (callbackQuadModType) static.
  void SolutionX(BigInt Value, const BigInt &Modulus) {
    if (VERBOSE) {
      printf("SolutionX(%s, %s)\n",
             Value.ToString().c_str(),
             Modulus.ToString().c_str());
    }
    SolNbr++;

    {
      BigInt mm = BigIntegerToBigInt(&modulus);
      CHECK(Modulus == mm) <<
        Modulus.ToString() << " vs " << mm.ToString();
    }

    // If 2*value is greater than modulus, subtract modulus.
    // BigInt Modulus = BigIntegerToBigInt(&modulus);
    if ((Value << 1) > Modulus) {
      Value -= Modulus;
    }

    BigInt A = BigIntegerToBigInt(&ValA);
    BigInt B = BigIntegerToBigInt(&ValB);
    BigInt C = BigIntegerToBigInt(&ValC);
    BigInt D = BigIntegerToBigInt(&ValD);
    BigInt E = BigIntegerToBigInt(&ValE);
    BigInt U = BigIntegerToBigInt(&ValU);

    BigInt V = BigIntegerToBigInt(&ValV);
    BigInt I = BigIntegerToBigInt(&ValI);

    if (VERBOSE) {
      printf("  with %s %s %s %s %s %s | %s %s\n",
             A.ToString().c_str(),
             B.ToString().c_str(),
             C.ToString().c_str(),
             D.ToString().c_str(),
             E.ToString().c_str(),
             U.ToString().c_str(),
             V.ToString().c_str(),
             I.ToString().c_str());
    }

    switch (callbackQuadModType) {
    case CBACK_QMOD_PARABOLIC:
      clean.CallbackQuadModParabolic(ExchXY,
                                     A, B, C, D, E,
                                     U, V, I, Value);
      break;

    case CBACK_QMOD_ELLIPTIC:
      callbackQuadModElliptic(Value);
      break;

    case CBACK_QMOD_HYPERBOLIC:
      callbackQuadModHyperbolic(Value);
      break;

    default:
      break;
    }
  }

  // Solve congruence an^2 + bn + c = 0 (mod n) where n is different from zero.
  void SolveQuadModEquation(const BigInt &coeffQuadr,
                            const BigInt &coeffLinear,
                            const BigInt &coeffIndep,
                            const BigInt &modulus_in) {

    if (VERBOSE) {
      printf("[SQME] %s %s %s %s\n",
             coeffQuadr.ToString().c_str(),
             coeffLinear.ToString().c_str(),
             coeffIndep.ToString().c_str(),
             modulus_in.ToString().c_str());
    }

    BigInt modulus = BigInt::Abs(modulus_in);

    firstSolutionX = true;

    BigInt coeff_quadr = BigInt::CMod(coeffQuadr, modulus);
    if (coeff_quadr < 0) coeff_quadr += modulus;

    BigInt coeff_linear = BigInt::CMod(coeffLinear, modulus);
    if (coeff_linear < 0) coeff_linear += modulus;

    BigInt coeff_indep = BigInt::CMod(coeffIndep, modulus);
    if (coeff_indep < 0) coeff_indep += modulus;

    BigInt GcdAll = BigInt::GCD(coeff_indep,
                                BigInt::GCD(coeff_quadr, coeff_linear));

    // For a GCD of zero here, original code would cause and ignore
    // a division by zero, then read 0 from the temporary.

    if (GcdAll != 0 && BigInt::CMod(coeff_indep, GcdAll) != 0) {
      // ValC must be multiple of gcd(ValA, ValB).
      // Otherwise go out because there are no solutions.
      return;
    }

    GcdAll = BigInt::GCD(modulus, GcdAll);

    // PERF: version of division where we know it's divisible.
    // Divide all coefficients by gcd(ValA, ValB).
    if (GcdAll != 0) {
      coeff_quadr /= GcdAll;
      coeff_linear /= GcdAll;
      coeff_indep /= GcdAll;
      modulus /= GcdAll;
    }

    BigInt ValNn = modulus;

    if (ValNn == 1) {
      // All values from 0 to GcdAll - 1 are solutions.
      if (GcdAll > 5) {
        clean.ShowText("<p>All values of <var>x</var> between 0 and ");

        // XXX Suspicious that this modifies GcdAll in place (I
        // think just to display it?) but uses it again below.
        GcdAll -= 1;
        clean.ShowBigInt(GcdAll);
        clean.ShowText(" are solutions.</p>");
      } else {
        // must succeed; is < 5 and non-negative
        const int n = GcdAll.ToInt().value();
        for (int ctr = 0; ctr < n; ctr++) {
          SolutionX(BigInt(ctr), modulus);
        }
      }
      return;
    }

    // PERF divisibility check
    if (BigInt::CMod(coeff_quadr, modulus) == 0) {
      // Linear equation.
      printf("linear-eq coverage\n");

      if (BigInt::GCD(coeff_linear, modulus) != 1) {
        // ValB and ValN are not coprime. Go out.
        return;
      }

      // Calculate z <- -ValC / ValB (mod ValN)

      // We only use this right here, so we could have a version of MGParams
      // that just took a BigInt modulus, at least for this code.
      limb TheModulus[MAX_LEN];
      const int modulus_length = BigIntToLimbs(modulus, TheModulus);
      TheModulus[modulus_length].x = 0;

      // Is it worth it to convert to montgomery form for one division??
      const MontgomeryParams params = GetMontgomeryParams(modulus_length, TheModulus);

      BigInt z = BigIntModularDivision(params, coeff_indep, coeff_linear, modulus);

      if (z != 0) {
        // not covered by cov.sh :(
        printf("new coverage z != 0\n");
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
        SolutionX(z, modulus);
        z += modulus;
        if (z < Temp0) break;
      }

      return;
    }

    if (callbackQuadModType == CBACK_QMOD_PARABOLIC) {
      // For elliptic case, the factorization is already done.

      // To solve this quadratic modular equation we have to
      // factor the modulus and find the solution modulo the powers
      // of the prime factors. Then we combine them by using the
      // Chinese Remainder Theorem.
    }

    {
      // XXX two different moduli here
      // SolveEquation used to modify the modulus and Nn. Was that
      // why?
      // CHECK(modulus == BigIntegerToBigInt(&this->modulus));
      BigIntToBigInteger(modulus, &this->modulus);

      if (VERBOSE) {
        printf("[Call SolveEq] %s %s %s %s %s %s\n",
               coeff_quadr.ToString().c_str(),
               coeff_linear.ToString().c_str(),
               coeff_indep.ToString().c_str(),
               modulus.ToString().c_str(),
               GcdAll.ToString().c_str(),
               ValNn.ToString().c_str());
      }

      SolveEquation(
          SolutionFn([this](BigInteger *value) {
              this->SolutionX(BigIntegerToBigInt(value),
                              BigIntegerToBigInt(&this->modulus));
            }),
          coeff_quadr, coeff_linear, coeff_indep,
          modulus, GcdAll, ValNn);
    }
  }

  void DiscriminantIsZero(BigInt A, BigInt B, BigInt C,
                          BigInt D, BigInt E, BigInt F) {

    // Next algorithm does not work if A = 0. In this case, exchange x and y.
    ExchXY = false;
    if (A == 0) {
      ExchXY = true;
      // Exchange coefficients of x^2 and y^2.
      std::swap(A, C);
      // Exchange coefficients of x and y.
      std::swap(D, E);
    }

    const bool swap_xy = ExchXY;

    // discriminant should be known zero on this code path.
    // BigIntToBigInteger(Discr, &discr);

    // ax^2 + bxy + cx^2 + dx + ey + f = 0 (1)
    // Multiplying by 4a:
    // (2ax + by)^2 + 4adx + 4aey + 4af = 0
    // Let t = 2ax + by. So (1) becomes: (t + d)^2 = uy + v.

    // Compute u <- 2(bd - 2ae)
    BigInt U = (B * D - ((A * E) << 1)) << 1;

    // Compute v <- d^2 - 4af
    BigInt V = D * D - ((A * F) << 2);

    // XXX remove this state
    BigIntToBigInteger(A, &ValA);
    BigIntToBigInteger(B, &ValB);
    BigIntToBigInteger(C, &ValC);
    BigIntToBigInteger(D, &ValD);
    BigIntToBigInteger(E, &ValE);
    BigIntToBigInteger(F, &ValF);
    BigIntToBigInteger(U, &ValU);
    BigIntToBigInteger(V, &ValV);

    if (U == 0) {
      // u equals zero, so (t+d)^2 = v.

      if (V < 0) {
        // There are no solutions when v is negative,
        // since a square cannot be equal to a negative number.
        return;
      }

      if (V == 0) {
        // printf("disczero_vzero coverage\n");
        // v equals zero, so (1) becomes 2ax + by + d = 0
        LinearSolution sol = LinearEq(A << 1, B, D);
        // Result box:
        if (swap_xy) sol.SwapXY();
        clean.PrintLinear(sol, "<var>t</var>");
        return;
      }

      // u equals zero but v does not.
      // v must be a perfect square, otherwise there are no solutions.
      BigInt G = BigInt::Sqrt(V);
      if (V != G * G) {
        // v is not perfect square, so there are no solutions.
        return;
      }

      // The original equation is now: 2ax + by + (d +/- g) = 0
      BigInt A2 = A << 1;

      // This equation represents two parallel lines.
      {
        LinearSolution sol = LinearEq(A2, B, D + G);
        if (swap_xy) sol.SwapXY();
        // Result box:
        clean.PrintLinear(sol, "<var>t</var>");
      }

      {
        LinearSolution sol = LinearEq(A2, B, D - G);
        if (swap_xy) sol.SwapXY();
        clean.PrintLinear(sol, "<var>t</var>");
      }

      return;
    }

    // At this moment u does not equal zero.
    // We have to solve the congruence
    //     T^2 = v (mod u) where T = t+d and t = 2ax+by.

    BigInt Modulus = BigIntegerToBigInt(&ValU);
    // XXX eliminate this state
    CopyBigInt(&modulus, &ValU);

    callbackQuadModType = CBACK_QMOD_PARABOLIC;
    equationNbr = 3;

    SolveQuadModEquation(
        BigInt(1),
        BigInt(0),
        -V,
        Modulus);
  }

  void ShowPoint(bool two_solutions,
                 const BigInt &X, const BigInt &Y,
                 const BigInt &Alpha, const BigInt &Beta,
                 const BigInt &Div) {

    if (two_solutions) {
      CHECK(showSolution == TWO_SOLUTIONS);
    } else {
      CHECK(showSolution == ONE_SOLUTION);
    }

    if (VERBOSE) {
      printf("ShowPoint %s %s %s %s %s\n",
             X.ToString().c_str(),
             Y.ToString().c_str(),
             Alpha.ToString().c_str(),
             Beta.ToString().c_str(),
             Div.ToString().c_str());
    }

    // Check first that (X+alpha) and (Y+beta) are multiple of D.
    BigInt tmp1 = X + Alpha;
    BigInt tmp2 = Y + Beta;

    CHECK(Div != 0) << "Might be shenanigans with divisibility by zero";

    // PERF divisibility tests.
    if (tmp1 % Div == 0 &&
        tmp2 % Div == 0) {

      // PERF known divisible
      if (Div != 0) {
        tmp1 /= Div;
        tmp2 /= Div;
      }

      // XXX is two_solutions statically known here?
      if (callbackQuadModType == CBACK_QMOD_HYPERBOLIC) {
        ShowXY(two_solutions, ExchXY, tmp1, tmp2);
      } else {
        // Result box:
        ShowXY(two_solutions, ExchXY, tmp1, tmp2);
      }

      // Show recursive solution if it exists.
      showRecursiveSolution = 1;
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
                             BigInt K, BigInt Discr,
                             BigInt Alpha, BigInt Beta, const BigInt &Div) {
    // Find GCD(a,b,c)
    BigInt GcdHomog = BigInt::GCD(BigInt::GCD(A, B), C);
    // BigIntGcd(&ValA, &ValB, &bigTmp);
    // BigIntGcd(&ValC, &bigTmp, &ValGcdHomog);
    // Divide A, B, C and K by this GCD.
    if (GcdHomog != 0) {
      A /= GcdHomog;
      B /= GcdHomog;
      C /= GcdHomog;
      K /= GcdHomog;
      // (void)BigIntDivide(&ValA, &ValGcdHomog, &ValA);
      // (void)BigIntDivide(&ValB, &ValGcdHomog, &ValB);
      // (void)BigIntDivide(&ValC, &ValGcdHomog, &ValC);
      // (void)BigIntDivide(&ValK, &ValGcdHomog, &ValK);
      // Divide discriminant by the square of GCD.
      Discr /= GcdHomog;
      Discr /= GcdHomog;
      // (void)BigIntDivide(&discr, &ValGcdHomog, &discr);
      // (void)BigIntDivide(&discr, &ValGcdHomog, &discr);
    }

    if (K == 0) {
      // If k=0, the only solution is (X, Y) = (0, 0)
      ShowPoint(false, BigInt(0), BigInt(0), Alpha, Beta, Div);
      return;
    }

    // XXX
    BigIntToBigInteger(A, &ValA);
    BigIntToBigInteger(B, &ValB);
    BigIntToBigInteger(C, &ValC);
    BigIntToBigInteger(K, &ValK);
    BigIntToBigInteger(Discr, &discr);

    BigIntToBigInteger(Alpha, &ValAlpha);
    BigIntToBigInteger(Beta, &ValBeta);
    BigIntToBigInteger(Div, &ValDiv);

    if (VERBOSE)
    printf("start NSD %s %s %s | %s %s | %s %s %s\n",
           A.ToString().c_str(), B.ToString().c_str(), C.ToString().c_str(),
           K.ToString().c_str(), Discr.ToString().c_str(),
           Alpha.ToString().c_str(), Beta.ToString().c_str(), Div.ToString().c_str());

    // ughhh
    CopyBigInt(&ValABak, &ValA);
    CopyBigInt(&ValBBak, &ValB);
    CopyBigInt(&ValCBak, &ValC);

    // Factor independent term.

    // std::unique_ptr<Factors> factors = BigFactor(&ValK);
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
        // Port note: The original code stored factorNbr, which was 1-based because
        // of the factor header.
        indexEvenMultiplicity.push_back(i);
        // Convert to even.
        originalMultiplicities.push_back(multiplicity & ~1);
      }
    }

    std::vector<int> counters(400, 0);
    (void)memset(isDescending, 0, sizeof(isDescending));

    BigInt E = BigInt(1);
    // intToBigInteger(&ValE, 1);  // Initialize multiplier to 1.
    // Loop that cycles through all square divisors of the independent term.
    equationNbr = 2;
    // BigIntGcd(&ValA, &ValK, &bigTmp);
    BigInt M(0);
    // intToBigInteger(&ValM, 0);
    if (BigInt::GCD(A, K) != 1) {
      // gcd(a, K) is not equal to 1.

      // printf("GCD != 1\n");
      BigInt UU1, UU2;
      do {
        // printf("uu1uu2 loop\n");

        // Compute U1 = cm^2 + bm + a and exit loop if this
        // value is not coprime to K.

        UU2 = C * M;
        UU1 = (UU2 + B) * M + A;
        // (void)BigIntMultiply(&ValC, &ValM, &U2);
        // BigIntAdd(&U2, &ValB, &U1);
        // (void)BigIntMultiply(&U1, &ValM, &U1);
        // BigIntAdd(&U1, &ValA, &U1);

        // BigIntGcd(&U1, &ValK, &bigTmp);
        if (VERBOSE)
        printf("%s GCD %s = %s\n",
               UU1.ToString().c_str(),
               K.ToString().c_str(),
               BigInt::GCD(UU1, K).ToString().c_str());

        if (BigInt::GCD(UU1, K) == 1) {
          // Increment M and change sign to indicate type.
          M = -(M + 1);
          // addbigint(&ValM, 1);  // Increment M.
          // BigIntChSign(&ValM);  // Change sign to indicate type.
          break;
        }

        M += 1;
        // addbigint(&ValM, 1);    // Increment M.

        // Compute U1 = am^2 + bm + c and loop while this
        // value is not coprime to K.

        UU2 = A * M;
        UU1 = (UU2 + B) * M + C;
        // (void)BigIntMultiply(&ValA, &ValM, &U2);
        // BigIntAdd(&U2, &ValB, &U1);
        // (void)BigIntMultiply(&U1, &ValM, &U1);
        // BigIntAdd(&U1, &ValC, &U1);
        // BigIntGcd(&U1, &ValK, &bigTmp);

        if (VERBOSE)
        printf("loopy %s | %s %s | %s %s %s | %s (%s)\n",
               M.ToString().c_str(),
               UU1.ToString().c_str(),
               UU2.ToString().c_str(),
               A.ToString().c_str(),
               B.ToString().c_str(),
               C.ToString().c_str(),
               K.ToString().c_str(),
               BigInt::GCD(UU1, K).ToString().c_str());

      } while (BigInt::GCD(UU1, K) != 1);

      // Compute 2am + b or 2cm + b as required.
      UU2 = (UU2 << 1) + B;
      // BigIntAdd(&U2, &U2, &U2);
      // BigIntAdd(&U2, &ValB, &U2);

      if (M >= 0) {
        // Compute c.
        B = (UU1 - UU2);
        C = B + A;
        // BigIntSubt(&U1, &U2, &ValB);
        // BigIntAdd(&ValB, &ValA, &ValC);
        // Compute b.
        B += UU1;
        // BigIntAdd(&ValB, &U1, &ValB);
        // Compute a.
        A = UU1;
        // CopyBigInt(&ValA, &U1);
      } else {
        // Compute c.
        B = UU1 + UU2;
        C += B;
        // BigIntAdd(&U1, &U2, &ValB);
        // BigIntAdd(&ValB, &ValC, &ValC);
        // Compute b.
        B += UU1;
        // BigIntAdd(&ValB, &U1, &ValB);
        // Compute a.
        A = UU1;
        // CopyBigInt(&ValA, &U1);
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

    for (;;) {
      // printf("solve loop\n");
      BigIntToBigInteger(K, &modulus);
      // CopyBigInt(&modulus, &ValK);
      modulus.sign = SIGN_POSITIVE;

      // XXX We modified the modulus, but
      // CopyBigInt(&LastModulus, &modulus);

      // Ugh, SQME depends on additional state (SolutionX);
      BigIntToBigInteger(E, &ValE);
      BigIntToBigInteger(A, &ValA);
      BigIntToBigInteger(B, &ValB);
      BigIntToBigInteger(C, &ValC);
      BigIntToBigInteger(M, &ValM);
      BigIntToBigInteger(K, &ValK);

      SolveQuadModEquation(
          // PERF just construct directly above.
          A, B, C,
          BigInt::Abs(K));

      // Adjust counters.
      // This modifies the factors (multiplicities) in place.
      int index;
      CHECK(indexEvenMultiplicity.size() ==
            originalMultiplicities.size());
      if (VERBOSE) printf("factors: ");
      for (index = 0; index < (int)indexEvenMultiplicity.size(); index++) {
        if (VERBOSE) printf("%d ", index);
        // Loop that increments counters.
        if (isDescending[index] == 0) {
          // Ascending.

          const int fidx = indexEvenMultiplicity[index];
          // const auto &[fact, multiplicity] = factors[fidx];
          if (counters[index] == originalMultiplicities[index]) {
            // Next time it will be descending.
            isDescending[index] = 1;
            continue;
          } else {
            // const int number_length = *fact->array;
            // modulus_length = number_length;
            // IntArray2BigInteger(number_length, fact->array, &bigTmp);
            // (void)BigIntMultiply(&bigTmp, &bigTmp, &U3);
            BigInt UU3 = factors[fidx].first * factors[fidx].first;
            factors[fidx].second -= 2;
            // Divide by square of prime.
            K /= UU3;
            // (void)BigIntDivide(&ValK, &U3, &ValK);
            // Multiply multiplier by prime.counters[index]++
            E *= factors[fidx].first;
            // (void)BigIntMultiply(&ValE, &bigTmp, &ValE);
            counters[index] += 2;
            break;
          }
        } else {
          // Descending.
          // auto &[fact, multiplicity] = factors[indexEvenMultiplicity[index]];
          const int fidx = indexEvenMultiplicity[index];
          // sFactorz *fact = &factors->product[indexEvenMultiplicity[index]];
          if (counters[index] <= 1) {
            // Next time it will be ascending.
            isDescending[index] = 0;
            continue;
          } else {
            // const int number_length = *fact->array;
            // modulus_length = number_length;
            // IntArray2BigInteger(number_length, fact->array, &bigTmp);
            BigInt UU3 = factors[fidx].first * factors[fidx].first;
            // (void)BigIntMultiply(&bigTmp, &bigTmp, &U3);
            factors[fidx].second += 2;
            // Multiply by square of prime.
            K *= UU3;
            // (void)BigIntMultiply(&ValK, &U3, &ValK);
            // Divide multiplier by prime.counters[index]++
            E /= factors[fidx].first;
            // (void)BigIntDivide(&ValE, &bigTmp, &ValE);
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
      // CopyBigInt(&LastModulus, &ValK);

      if (index == (int)indexEvenMultiplicity.size()) {
        // All factors have been found. Exit loop.
        break;
      }
    }
    if (VERBOSE) printf(".\n");

    // Note: G not necessarily initialized by here if
    // the condition below isn't true.
    BigInt G = BigIntegerToBigInt(&ValG);
    BigInt L = BigIntegerToBigInt(&ValL);

    if (VERBOSE)
    printf("bottom %s %s # %s %s / %s %s %s %s\n",
           K.ToString().c_str(),
           E.ToString().c_str(),
           BigIntegerToBigInt(&ValG).ToString().c_str(),
           BigIntegerToBigInt(&ValL).ToString().c_str(),

           Alpha.ToString().c_str(),
           Beta.ToString().c_str(),
           GcdHomog.ToString().c_str(),
           Discr.ToString().c_str());

    if (showRecursiveSolution &&
        callbackQuadModType == CBACK_QMOD_HYPERBOLIC) {

      // Show recursive solution.
      RecursiveSolution(A, B, C, G, L,
                        Alpha, Beta, GcdHomog, Discr);
    }
  }

  void NegativeDiscriminant(const BigInt &A, const BigInt &B, const BigInt &C,
                            const BigInt &K, const BigInt &Discr,
                            const BigInt &Alpha, const BigInt &Beta, const BigInt &Div) {
    callbackQuadModType = CBACK_QMOD_ELLIPTIC;
    NonSquareDiscriminant(A, B, C, K, Discr, Alpha, Beta, Div);
  }

  // Returns Temp0, Temp1
  std::pair<BigInt, BigInt>
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

  // On input: H: value of u, I: value of v.
  // Output: ((tu - nv)*E, u*E) and ((-tu + nv)*E, -u*E)
  // If m is greater than zero, perform the substitution: x = mX + (m-1)Y, y = X + Y
  // If m is less than zero, perform the substitution: x = X + Y, y = (|m|-1)X + |m|Y
  // Do not substitute if m equals zero.
  void NonSquareDiscrSolution(bool two_solutions,
                              const BigInt &M, const BigInt &E, const BigInt &K,
                              const BigInt &Alpha, const BigInt &Beta, const BigInt &Div,
                              const BigInt &H, const BigInt &I,
                              const BigInt &Value) {
    if (two_solutions) {
      CHECK(showSolution == TWO_SOLUTIONS);
    } else {
      CHECK(showSolution == ONE_SOLUTION);
    }

    BigInt KK;
    if (callbackQuadModType == CBACK_QMOD_HYPERBOLIC) {
      // Get K
      KK = -BigInt::Abs(K);
      // Port note: This code used to flip the sign of BigTmp,
      // then set it to negative. Certainly unnecessary; maybe
      // a bug?
      // BigIntChSign(&bigTmp);
      // bigTmp.sign = SIGN_NEGATIVE;
    } else {
      KK = K;
    }

    // X = (tu - Kv)*E
    const BigInt Z = (Value * H - KK * I) * E;
    // Y = u*E
    BigInt O = H * E;

    // Only need this in the case that there are two solutions.
    // It may be known at the call site?
    Xbak = &Xplus;
    Ybak = &Yplus;

    // Undo unimodular substitution
    {
      const auto &[Temp0, Temp1] =
        UnimodularSubstitution(M, Z, O);
      ShowPoint(two_solutions,
                Temp0,
                Temp1,
                Alpha, Beta, Div);
    }

    // Z: (-tu - Kv)*E
    // O: -u*E

    Xbak = &Xminus;
    Ybak = &Yminus;

    // Undo unimodular substitution
    {
      const auto &[Temp0, Temp1] =
        UnimodularSubstitution(M, -Z, -O);
      ShowPoint(two_solutions,
                Temp0,
                Temp1,
                Alpha, Beta, Div);
    }

    // Restore value.
    // BigIntToBigInteger(Tmp12, value);
  }

  void callbackQuadModElliptic(const BigInt &Value) {
    const BigInt A = BigIntegerToBigInt(&ValA);
    const BigInt B = BigIntegerToBigInt(&ValB);
    const BigInt C = BigIntegerToBigInt(&ValC);
    const BigInt K = BigIntegerToBigInt(&ValK);

    auto pqro = PerformTransformation(A, B, C, K, Value);
    if (!pqro.has_value()) {
      // No solutions because gcd(P, Q, R) > 1.
      return;
    }

    const auto &[P, Q_, R_] = pqro.value();
    BigIntToBigInteger(P, &ValP);
    BigIntToBigInteger(Q_, &ValQ);
    BigIntToBigInteger(R_, &ValR);

    // XXX
    // BigIntegerToBigInt(Value, &value);

    const BigInt M = BigIntegerToBigInt(&ValM);
    const BigInt E = BigIntegerToBigInt(&ValE);
    const BigInt Alpha = BigIntegerToBigInt(&ValAlpha);
    const BigInt Beta = BigIntegerToBigInt(&ValBeta);
    const BigInt Div = BigIntegerToBigInt(&ValDiv);
    // const BigInt P = BigIntegerToBigInt(&ValP);
    const BigInt Discr = BigIntegerToBigInt(&discr);

    CHECK(Discr <= 0);

    std::optional<int64_t> plow_opt = P.ToInt();
    if (plow_opt.has_value() && plow_opt.value() >= 0) {
      int64_t plow = plow_opt.value();
      if (Discr < -4 && plow == 1) {
        // Discriminant is less than -4 and P equals 1.

        NonSquareDiscrSolution(false,
                               M, E, K,
                               Alpha, Beta, Div,
                               BigInt(1), BigInt(0),
                               Value);   // (1, 0)
        equationNbr += 2;
        return;
      }

      BigInt Q = BigIntegerToBigInt(&ValQ);
      if (Discr == -4) {
        // Discriminant is equal to -4.
        BigInt G = Q >> 1;

        if (plow == 1) {
          NonSquareDiscrSolution(false,
                                 M, E, K,
                                 Alpha, Beta, Div,
                                 BigInt(1), BigInt(0),
                                 Value);
          intToBigInteger(&ValI, -1);
          NonSquareDiscrSolution(false,
                                 M, E, K,
                                 Alpha, Beta, Div,
                                 G, BigInt(-1),
                                 Value);   // (Q/2, -1)
          equationNbr += 2;
          return;
        } if (plow == 2) {

          NonSquareDiscrSolution(false,
                                 M, E, K,
                                 Alpha, Beta, Div,
                                 (G - 1) >> 1, BigInt(-1),
                                 Value);   // ((Q/2-1)/2, -1)

          NonSquareDiscrSolution(false,
                                 M, E, K,
                                 Alpha, Beta, Div,
                                 (G + 1) >> 1, BigInt(-1),
                                 Value);   // ((Q/2+1)/2, -1)
          equationNbr += 2;
          return;
        }
      }

      if (Discr == -3) {
        // Discriminant is equal to -3.
        if (plow == 1) {

          // printf("plow1 coverage\n");
          NonSquareDiscrSolution(false,
                                 M, E, K,
                                 Alpha, Beta, Div,
                                 BigInt(1), BigInt(0),
                                 Value);   // (1, 0)

          NonSquareDiscrSolution(false,
                                 M, E, K,
                                 Alpha, Beta, Div,
                                 (Q - 1) >> 1, BigInt(-1),
                                 Value);   // ((Q-1)/2, -1)

          NonSquareDiscrSolution(false,
                                 M, E, K,
                                 Alpha, Beta, Div,
                                 (Q + 1) >> 1, BigInt(-1),
                                 Value);   // ((Q+1)/2, -1)
          equationNbr += 2;
          return;
        } else if (plow == 3) {

          // printf("plow3 coverage\n");

          NonSquareDiscrSolution(false,
                                 M, E, K,
                                 Alpha, Beta, Div,
                                 (Q + 3) / 6, BigInt(-1),
                                 Value);   // ((Q+3)/6, -1)

          NonSquareDiscrSolution(false,
                                 M, E, K,
                                 Alpha, Beta, Div,
                                 Q / 3, BigInt(-2),
                                 Value);   // (Q/3, -2)

          NonSquareDiscrSolution(false,
                                 M, E, K,
                                 Alpha, Beta, Div,
                                 (Q - 3) / 6, BigInt(-1),
                                 Value);   // ((Q-3)/6, -1)
          equationNbr += 2;
          return;
        }
      }
    }

    // Compute bound L = sqrt(4P/(-D))
    // MultInt(&U1, &ValP, 4);
    // (void)BigIntDivide(&U1, &discr, &U1);
    // BigIntChSign(&U1);               // 4P/(-D)
    // SquareRoot(U1.limbs, ValL.limbs, U1.nbrLimbs, &ValL.nbrLimbs);  // sqrt(4P/(-D))
    const BigInt L = BigInt::Sqrt((P << 2) / -Discr);
    BigIntToBigInteger(L, &ValL);

    intToBigInteger(&U1, 1);         // Initial value of last convergent: 1/0.
    intToBigInteger(&V1, 0);
    intToBigInteger(&U2, 0);         // Initial value of next to last convergent: 0/1.
    intToBigInteger(&V2, 1);
    // Compute continued fraction expansion of U/V = -Q/2P.
    CopyBigInt(&ValU, &ValQ);
    BigIntChSign(&ValU);
    BigIntAdd(&ValP, &ValP, &ValV);

    while (!BigIntIsZero(&ValV)) {
      {
        const auto &[U, UU1, UU2, UU3,
                     V, VV1, VV2, VV3] =
          clean.GetNextConvergent(BigIntegerToBigInt(&ValU),
                                  BigIntegerToBigInt(&U1),
                                  BigIntegerToBigInt(&U2),
                                  BigIntegerToBigInt(&ValV),
                                  BigIntegerToBigInt(&V1),
                                  BigIntegerToBigInt(&V2));
        BigIntToBigInteger(U, &ValU);
        BigIntToBigInteger(UU1, &U1);
        BigIntToBigInteger(UU2, &U2);
        BigIntToBigInteger(UU3, &U3);
        BigIntToBigInteger(V, &ValV);
        BigIntToBigInteger(VV1, &V1);
        BigIntToBigInteger(VV2, &V2);
        BigIntToBigInteger(VV3, &V3);
      }

      // Check whether the denominator of convergent exceeds bound.
      BigIntSubt(&ValL, &V1, &bigTmp);
      if (bigTmp.sign == SIGN_NEGATIVE) {
        // Bound exceeded, so go out.
        break;
      }

      // Test whether P*U1^2 + Q*U1*V1 + R*V1^2 = 1.
      (void)BigIntMultiply(&ValP, &U1, &ValO);      // P*U1
      (void)BigIntMultiply(&ValQ, &V1, &bigTmp);    // Q*V1
      BigIntAdd(&ValO, &bigTmp, &ValO);             // P*U1 + Q*V1
      (void)BigIntMultiply(&ValO, &U1, &ValO);      // P*U1^2 + Q*U1*V1
      (void)BigIntMultiply(&ValR, &V1, &bigTmp);    // R*V1
      (void)BigIntMultiply(&bigTmp, &V1, &bigTmp);  // R*V1^2
      BigIntAdd(&ValO, &bigTmp, &ValO);       // P*U1^2 + Q*U1*V1 + R*V1^2

      if ((ValO.sign == SIGN_POSITIVE) && (ValO.nbrLimbs == 1) && (ValO.limbs[0].x == 1)) {
        // a*U1^2 + b*U1*V1 + c*V1^2 = 1.
        NonSquareDiscrSolution(false,
                               M, E, K,
                               Alpha, Beta, Div,
                               BigIntegerToBigInt(&U1),
                               BigIntegerToBigInt(&V1),
                               Value);        // (U1, V1)
        int D = discr.limbs[0].x;

        if (discr.nbrLimbs > 1 || D > 4) {
          // Discriminant is less than -4, go out.
          break;
        }

        if (D == 3 || D == 4) {
          // Discriminant is equal to -3 or -4.
          {
            const auto &[U, UU1, UU2, UU3,
                         V, VV1, VV2, VV3] =
              clean.GetNextConvergent(BigIntegerToBigInt(&ValU),
                                      BigIntegerToBigInt(&U1),
                                      BigIntegerToBigInt(&U2),
                                      BigIntegerToBigInt(&ValV),
                                      BigIntegerToBigInt(&V1),
                                      BigIntegerToBigInt(&V2));
            BigIntToBigInteger(U, &ValU);
            BigIntToBigInteger(UU1, &U1);
            BigIntToBigInteger(UU2, &U2);
            BigIntToBigInteger(UU3, &U3);
            BigIntToBigInteger(V, &ValV);
            BigIntToBigInteger(VV1, &V1);
            BigIntToBigInteger(VV2, &V2);
            BigIntToBigInteger(VV3, &V3);
          }

          CopyBigInt(&ValI, &V1);

          NonSquareDiscrSolution(false,
                                 M, E, K,
                                 Alpha, Beta, Div,
                                 BigIntegerToBigInt(&U1),
                                 BigIntegerToBigInt(&V1),
                                 Value);      // (U1, V1)
          if (D == 3) {

            {
              const auto &[U, UU1, UU2, UU3,
                           V, VV1, VV2, VV3] =
                clean.GetNextConvergent(BigIntegerToBigInt(&ValU),
                                        BigIntegerToBigInt(&U1),
                                        BigIntegerToBigInt(&U2),
                                        BigIntegerToBigInt(&ValV),
                                        BigIntegerToBigInt(&V1),
                                        BigIntegerToBigInt(&V2));
              BigIntToBigInteger(U, &ValU);
              BigIntToBigInteger(UU1, &U1);
              BigIntToBigInteger(UU2, &U2);
              BigIntToBigInteger(UU3, &U3);
              BigIntToBigInteger(V, &ValV);
              BigIntToBigInteger(VV1, &V1);
              BigIntToBigInteger(VV2, &V2);
              BigIntToBigInteger(VV3, &V3);
            }

            NonSquareDiscrSolution(false,
                                   M, E, K,
                                   Alpha, Beta, Div,
                                   BigIntegerToBigInt(&U1),
                                   BigIntegerToBigInt(&V1),
                                   Value);    // (U1, V1)
          }
          break;
        }
      }
    }
    equationNbr += 3;
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

    if (VERBOSE)
    printf("CheckSolutionSquareDiscr %s %s %s %s %s %s\n",
           P.ToString().c_str(),
           O.ToString().c_str(),
           CurrentFactor.ToString().c_str(),
           L.ToString().c_str(),
           N.ToString().c_str(),
           H.ToString().c_str());

    CHECK(O != 0) << "Might have been shenanigans with O = 0?";
    if (P % O == 0) {
      // PERF divisibility test followed by divide
      // X found.
      BigInt U1 = P / O;
      // ValP = Numerator of Y.
      P = CurrentFactor * L - N * H;

      CHECK(O != 0);
      if (P % O == 0) {
        // Y found.
        BigInt U2 = P / O;
        // Show results.

        ShowPoint(false, U1, U2, Alpha, Beta, Div);
        return;
      }
    }

    // The system of two equations does not have integer solutions.
    // No solution found.
  }

  // Discr = G^2
  void PerfectSquareDiscriminant(
      const BigInt &A, const BigInt &B, const BigInt &C,
      const BigInt &G, const BigInt &K,
      const BigInt &Alpha, const BigInt &Beta, const BigInt &Div,
      const BigInt &Discr) {
    // only used on path where A != 0
    BigInt S(0xCAFE);
    BigInt R;
    if (A == 0) {
      // Let R = gcd(b, c)
      // (bX + cY) Y = k
      R = BigInt::GCD(B, C);
    } else {
      // Multiplying by 4a we get (2aX + (b+g)Y)(2aX + (b-g)Y) = 4ak
      // Let R = gcd(2a, b+g)
      BigInt A2 = A << 1;
      R = BigInt::GCD(A2, B + G);
      // Let S = gcd(2a, b-g)
      S = BigInt::GCD(A2, B - G);
      // Let L = 4ak
      // Port note: L is dead. It was only used in teach mode.
      // L = (A * K) << 2;
    }

    if (K == 0) {

      // k equals zero.
      if (A == 0) {
        // printf("kzeroazero coverage\n");
        // Coefficient a does equals zero.
        // Solve Dy + beta = 0

        {
          LinearSolution sol = LinearEq(BigInt(0), Discr, Beta);
          CHECK(!ExchXY);
          // Result box:
          clean.PrintLinear(sol, "t");
        }

        // Solve bDx + cDy + b*alpha + c*beta = 0
        BigInt Aux0 = B * Discr;
        BigInt Aux1 = C * Discr;
        BigInt Aux2 = B * Alpha + C * Beta;

        {
          LinearSolution sol = LinearEq(Aux0, Aux1, Aux2);
          CHECK(!ExchXY);
          // Result box:
          clean.PrintLinear(sol, "t");
        }

      } else {
        // printf("kzeroanzero coverage\n");
        // Coefficient a does not equal zero.

        const BigInt AAlpha2 = (A * Alpha) << 1;

        // Solve 2aD x + (b+g)D y = 2a*alpha + (b+g)*beta
        BigInt Aux0 = (A * Discr) << 1;
        // (void)BigIntMultiply(&ValA, &discr, &Aux0);
        // BigIntAdd(&Aux0, &Aux0, &Aux0);
        BigInt Aux1 = (B + G) * Discr;
        // BigIntAdd(&ValB, &ValG, &Aux1);
        // (void)BigIntMultiply(&Aux1, &discr, &Aux1);
        BigInt Aux2 = -(AAlpha2 + (B + G) * Beta);
        // (void)BigIntMultiply(&ValA, &ValAlpha, &Aux2);
        //  BigIntAdd(&Aux2, &Aux2, &Aux2);
        // BigIntAdd(&ValB, &ValG, &bigTmp);
        // (void)BigIntMultiply(&bigTmp, &ValBeta, &bigTmp);
        // BigIntAdd(&Aux2, &bigTmp, &Aux2);
        // BigIntChSign(&Aux2);
        {
          LinearSolution sol = LinearEq(Aux0, Aux1, Aux2);
          // Result box:
          CHECK(!ExchXY);
          clean.PrintLinear(sol, "t");
        }

        // Solve the equation 2aD x + (b-g)D y = 2a*alpha + (b-g)*beta
        Aux0 = A << 1;
        Aux1 *= (B - G) * Discr;
        Aux2 = -(AAlpha2 + (B - G) * Beta);
        {
          LinearSolution sol = LinearEq(Aux0, Aux1, Aux2);
          // Result box:
          CHECK(!ExchXY);
          clean.PrintLinear(sol, "t");
        }
      }

      return;
    }

    // k does not equal zero.
    BigInt U1, U3;
    if (A == 0) {
      // printf("knzaz coverage\n");
      // If R does not divide k, there is no solution.
      U3 = K;
      U1 = R;
    } else {
      // printf("knzanz coverage\n");
      // If R*S does not divide 4ak, there is no solution.
      U1 = R * S;
      U3 = (A * K) << 2;
    }

    BigInt U2 = BigInt::CMod(U3, U1);

    if (U2 != 0) {
      return;
    }

    // PERF: Known divisible
    const BigInt Z = U3 / U1;
    // (void)BigIntDivide(&U3, &U1, &ValZ);

    // We have to find all factors of the right hand side.

    // Compute all factors of Z = 4ak/RS

    // Factor positive number.
    std::vector<std::pair<BigInt, int>> factors =
      BigIntFactor(BigInt::Abs(Z));

    // Do not factor again same modulus.
    // CopyBigInt(&LastModulus, &ValZ);

    // x = (NI - JM) / D(IL - MH) and y = (JL - NH) / D(IL - MH)
    // The denominator cannot be zero here.
    // H = 2a/R, I = (b+g)/R, J = F + H * alpha + I * beta
    // L = 2a/S, M = (b-g)/S, N = Z/F + L * alpha + M * beta
    // F is any factor of Z (positive or negative).
    const int nbrFactors = factors.size();

    BigInt H, I, L, M;
    if (A == 0) {
      H = BigInt(0);
      I = BigInt(1);
      // L <- b/R
      L = B / R;
      // M <- c/R
      M = C / R;
    } else {
      // 2a
      BigInt UU3 = A << 1;
      // H <- 2a/R
      H = UU3 / R;
      // L <- 2a/S
      L = UU3 / S;
      // I <- (b+g)/R
      I = (B + G) / R;
      // M <- (b-g)/S
      M = (B - G) / S;
    }


    // Compute denominator: D(IL - MH)
    const BigInt Den = Discr * (I * L - M * H);
    // O <- L * alpha + M * beta
    const BigInt O = L * Alpha + M * Beta;
    // K <- H * alpha + I * beta
    const BigInt NewK = H * Alpha + I * Beta;

    // Dead? Maybe just teach mode?
    // I think some of these are used in PositiveDiscriminant
    BigIntToBigInteger(Den, &ValDen);
    BigIntToBigInteger(O, &ValO);
    BigIntToBigInteger(NewK, &ValK);

    // Loop that finds all factors of Z.
    // Use Gray code to use only one big number.
    // Gray code: 0->000, 1->001, 2->011, 3->010, 4->110, 5->111, 6->101, 7->100.
    // Change from zero to one means multiply, otherwise divide.
    std::vector<int> counters(400, 0);
    (void)memset(isDescending, 0, sizeof(isDescending));

    BigInt CurrentFactor(1);
    for (;;) {
      // Process positive divisor.
      CheckSolutionSquareDiscr(CurrentFactor,
                               H, I, L, M, Z,
                               Alpha, Beta, Div);
      // Process negative divisor.
      CheckSolutionSquareDiscr(-CurrentFactor,
                               H, I, L, M, Z,
                               Alpha, Beta, Div);

      int fidx = 0;
      int index;
      for (index = 0; index < nbrFactors; index++) {
        // Loop that increments counters.
        if (isDescending[index] == 0) {
          // Ascending.
          if (counters[index] == factors[fidx].second) {
            isDescending[index] = 1;    // Next time it will be descending.
            fidx++;
            continue;
          }

          const BigInt &p = factors[fidx].first;
          CurrentFactor *= p;
          counters[index]++;
          break;
        }

        if (counters[index] == 0) {
          // Descending.
          isDescending[index] = 0;    // Next time it will be ascending.
          fidx++;
          // pstFactor++;
          continue;
        }

        // XXX same
        const BigInt &p = factors[fidx].first;
        CurrentFactor /= p;

        counters[index]--;
        break;
      }

      if (index == nbrFactors) {
        // All factors have been found. Exit loop.
        break;
      }
    }
  }

  void PositiveDiscriminant(const BigInt &A, const BigInt &B, const BigInt &C,
                            const BigInt &K, const BigInt &Discr,
                            const BigInt &Alpha, const BigInt &Beta, const BigInt &Div) {
    callbackQuadModType = CBACK_QMOD_HYPERBOLIC;
    NonSquareDiscriminant(A, B, C, K, Discr, Alpha, Beta, Div);
  }

  //  PQa algorithm for (P+G)/Q where G = sqrt(discriminant):
  //  Set U1 to 1 and U2 to 0.
  //  Set V1 to 0 and V2 to 1.
  //  Perform loop:
  //  Compute a as floor((U + G)/V)
  //  Set U3 to U2, U2 to U1 and U1 to a*U2 + U3
  //  Set V3 to V2, V2 to V1 and V1 <- a*V2 + V3
  //  Set U to a*V - U
  //  Set V to (D - U^2)/V
  //  Inside period when: 0 <= G - U < V
  void ContFrac(BigInteger *value, enum eShowSolution solutionNbr) {
    int periodIndex = 0;

    bool isBeven = ((ValB.limbs[0].x & 1) == 0);
    // If (D-U^2) is not multiple of V, exit routine.
    (void)BigIntMultiply(&ValU, &ValU, &bigTmp); // V <- (D - U^2)/V
    BigIntSubt(&ValL, &bigTmp, &bigTmp);   // D - U^2
    (void)BigIntRemainder(&bigTmp, &ValV, &bigTmp);
    if (!BigIntIsZero(&bigTmp)) {
      return;
    }
    // back up value
    BigInt Tmp11 = BigIntegerToBigInt(value);

    // Initialize variables.
    intToBigInteger(&U1, 1);
    intToBigInteger(&U2, 0);
    intToBigInteger(&V1, 0);
    intToBigInteger(&V2, 1);
    // Less than zero means outside period.
    intToBigInteger(&startPeriodU, -1);
    int index = 0;

    if (solutionNbr == SECOND_SOLUTION) {
      index++;
    }

    bool isIntegerPart = true;

    for (;;) {
      if ((ValV.nbrLimbs == 1) && (ValV.limbs[0].x == (isBeven ? 1 : 2)) &&
          ((index & 1) == ((ValK.sign == ValV.sign)? 0 : 1))) {

        // XXX replaced with two_solutions
        showSolution = TWO_SOLUTIONS;
        solFound = false;

        const BigInt M = BigIntegerToBigInt(&ValM);
        const BigInt E = BigIntegerToBigInt(&ValE);
        const BigInt K = BigIntegerToBigInt(&ValK);
        const BigInt Alpha = BigIntegerToBigInt(&ValAlpha);
        const BigInt Beta = BigIntegerToBigInt(&ValBeta);
        const BigInt Div = BigIntegerToBigInt(&ValDiv);

        // Found solution.
        if ((discr.nbrLimbs == 1) && (discr.limbs[0].x == 5) && (ValA.sign != ValK.sign) &&
            (solutionNbr == FIRST_SOLUTION)) {
          // Determinant is 5 and aK < 0. Use exceptional solution (U1-U2)/(V1-V2).
          // BigIntSubt(&U1, &U2, &ValI);

          // printf("aaaaaaa coverage\n");

          NonSquareDiscrSolution(true,
                                 M, E, K,
                                 Alpha, Beta, Div,
                                 BigIntegerToBigInt(&V1) - BigIntegerToBigInt(&V2),
                                 BigIntegerToBigInt(&U1) - BigIntegerToBigInt(&U2),
                                 BigIntegerToBigInt(value));

        } else {
          // Determinant is not 5 or aK > 0. Use convergent U1/V1 as solution.

          NonSquareDiscrSolution(true,
                                 M, E, K,
                                 Alpha, Beta, Div,
                                 BigIntegerToBigInt(&V1),
                                 BigIntegerToBigInt(&U1),
                                 BigIntegerToBigInt(value));
        }

        if (solFound) {
          break;                             // Solution found. Exit loop.
        }
      }

      if (startPeriodU.sign == SIGN_POSITIVE) {
        // Already inside period.
        periodIndex++;
        if ((BigIntEqual(&ValU, &startPeriodU) &&
             BigIntEqual(&ValV, &startPeriodV)) &&
            // New period started.
            ((periodIndex & 1) == 0)) {
          // Two periods of period length is odd, one period if even.
          break;  // Go out in this case.
        }
      } else if (!isIntegerPart) {
        // Check if periodic part of continued fraction has started.
        BigInt U = BigIntegerToBigInt(&ValU);
        BigInt V = BigIntegerToBigInt(&ValV);
        BigInt G = BigIntegerToBigInt(&ValG);

        if (CheckStartOfContinuedFractionPeriod(U, V, G)) {
          CopyBigInt(&startPeriodU, &ValU);
          CopyBigInt(&startPeriodV, &ValV);
        }
      }

      // Get continued fraction coefficient.
      BigIntAdd(&ValU, &ValG, &bigTmp);
      if (ValV.sign == SIGN_NEGATIVE) {
        // If denominator is negative, round square root upwards.
        addbigint(&bigTmp, 1);
      }

      // Tmp1 = Term of continued fraction.
      floordiv(&bigTmp, &ValV, &Tmp1);
      // Update convergents.
      // U3 <- U2, U2 <- U1, U1 <- a*U2 + U3
      CopyBigInt(&U3, &U2);
      CopyBigInt(&U2, &U1);
      (void)BigIntMultiply(&Tmp1, &U2, &U1);
      BigIntAdd(&U1, &U3, &U1);

      // V3 <- V2, V2 <- V1, V1 <- a*V2 + V3
      CopyBigInt(&V3, &V2);
      CopyBigInt(&V2, &V1);
      (void)BigIntMultiply(&Tmp1, &V2, &V1);
      BigIntAdd(&V1, &V3, &V1);
      // Update numerator and denominator.
      (void)BigIntMultiply(&Tmp1, &ValV, &bigTmp); // U <- a*V - U
      BigIntSubt(&bigTmp, &ValU, &ValU);
      (void)BigIntMultiply(&ValU, &ValU, &bigTmp); // V <- (D - U^2)/V
      BigIntSubt(&ValL, &bigTmp, &bigTmp);
      (void)BigIntDivide(&bigTmp, &ValV, &Tmp1);
      CopyBigInt(&ValV, &Tmp1);
      index++;
      isIntegerPart = false;
    }

    // Restore value.
    BigIntToBigInteger(Tmp11, value);
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
  void RecursiveSolution(
      BigInt A, BigInt B, BigInt C,
      BigInt G, BigInt L,
      const BigInt &Alpha, const BigInt &Beta,
      const BigInt &GcdHomog, BigInt Discr) {

    BigIntToBigInteger(G, &ValG); // XXX

    BigInt H = Discr;

    const bool isBeven = B.IsEven();
    if (isBeven) {
      H >>= 2;
    }

    // Obtain original discriminant.
    Discr *= GcdHomog;
    Discr *= GcdHomog;

    std::optional<int64_t> gcdo = GcdHomog.ToInt();
    CHECK(gcdo.has_value()) << "Original code seems to assume this, "
      "accessing the first limb directly.";
    const int64_t gcd_homog = gcdo.value();


    if (Discr == 5) {
      // Discriminant is 5.
      // Do not use continued fraction because it does not work.

      // 3,1 is first solution to U1^2 - 5*V1^2 = 4
      if (clean.SolutionFoundFromContFraction(isBeven, 4,
                                              Alpha, Beta,
                                              A, B, C,
                                              Discr,
                                              BigInt(3),
                                              BigInt(1))) {
        return;
      }

      // 9,4 is first solution to U1^2 - 5*V1^2 = 1
      (void)clean.SolutionFoundFromContFraction(isBeven, 1,
                                                Alpha, Beta,
                                                A, B, C,
                                                Discr,
                                                BigInt(9),
                                                BigInt(4));
      return;
    }

    // g <- sqrt(discr).
    G = BigInt::Sqrt(H);
    // Port note: Was explicit SIGN_POSITIVE here in original, but I think that
    // was just because it was manipulating the limbs directly? Sqrt
    // is always non-negative...
    CHECK(G >= 0);
    // XXX should be unnecessary
    BigIntToBigInteger(G, &ValG);

    int periodLength = 1;

    BigInt U(0);
    BigInt V(1);

    BigInt UU3 = BigIntegerToBigInt(&U3);
    BigInt UU2(0);
    BigInt UU1(1);

    BigInt VV3 = BigIntegerToBigInt(&V3);
    BigInt VV2(1);
    BigInt VV1(0);

    BigInt UBak, VBak;

    if (gcd_homog != 1) {
      periodLength = -1;
      do {
        BigInt BigTmp = U + G;
        if (V < 0) {
          // If denominator is negative, round square root upwards.
          BigTmp += 1;
        }

        // Tmp1 = Term of continued fraction.
        BigInt Tmp1 = FloorDiv(BigTmp, V);

        // U <- a*V - U
        U = Tmp1 * V - U;

        // V <- (D - U^2)/V
        V = (L - U * U) / V;

        if (periodLength < 0) {
          UBak = U;
          VBak = V;
        }
        periodLength++;
      } while (periodLength == 1 || U != UBak || V != VBak);
      // Reset values of U and V.
      U = BigInt{0};
      V = BigInt{1};
    }

    clean.ShowText("<p>Recursive solutions:</p><p>");

    // XXX should not be necessary
    CopyBigInt(&ValA, &ValABak);
    CopyBigInt(&ValB, &ValBBak);
    CopyBigInt(&ValC, &ValCBak);

    A = BigIntegerToBigInt(&ValA);
    B = BigIntegerToBigInt(&ValB);
    C = BigIntegerToBigInt(&ValC);

    int periodNbr = 0;
    enum eSign sign = SIGN_POSITIVE;
    for (;;) {
      BigInt BigTmp = U + G;
      if (V < 0) {
        // If denominator is negative, round square root upwards.
        BigTmp += 1;
      }
      // Tmp1 = Term of continued fraction.
      BigInt Tmp1 = FloorDiv(BigTmp, V);

      // U3 <- U2, U2 <- U1, U1 <- a*U2 + U3
      UU3 = UU2;
      UU2 = UU1;
      UU1 = Tmp1 * UU2 + UU3;

      // V3 <- V2, V2 <- V1, V1 <- a*V2 + V3
      VV3 = VV2;
      VV2 = VV1;
      VV1 = Tmp1 * VV2 + VV3;

      U = Tmp1 * V - U;
      V = (H - U * U) / V;

      if (sign == SIGN_POSITIVE) {
        sign = SIGN_NEGATIVE;
      } else {
        sign = SIGN_POSITIVE;
      }

      if (VERBOSE)
      printf("FS: %c %s %s %s %d\n",
             isBeven ? 'e' : 'o',
             V.ToString().c_str(),
             Alpha.ToString().c_str(),
             Beta.ToString().c_str(),
             periodNbr);

      // V must have the correct sign.
      if ((sign == SIGN_NEGATIVE) ? V >= 0 : V < 0) {
        continue;
      }

      // Expecting denominator to be 1 (B even or odd)
      // or 4 (B odd) with correct sign.
      if (BigInt::Abs(V) != 1 &&
          (isBeven || BigInt::Abs(V) != 4)) {
        continue;
      }

      periodNbr++;
      if (((periodNbr*periodLength) % gcd_homog) != 0) {
        continue;
      }


      // Found solution from continued fraction.
      if (clean.SolutionFoundFromContFraction(isBeven,
                                              BigInt::Abs(V).ToInt().value(),
                                              Alpha, Beta,
                                              A, B, C,
                                              Discr,
                                              UU1, VV1)) {
        return;
      }
    }
  }

  void callbackQuadModHyperbolic(const BigInt &Value) {
    bool isBeven_XXX = ((ValB.limbs[0].x & 1) == 0);
    positiveDenominator = 1;

    const BigInt A = BigIntegerToBigInt(&ValA);
    const BigInt B = BigIntegerToBigInt(&ValB);
    const BigInt C = BigIntegerToBigInt(&ValC);
    const BigInt K = BigIntegerToBigInt(&ValK);

    auto pqro = PerformTransformation(A, B, C, K, Value);
    if (!pqro.has_value()) {
      // No solutions because gcd(P, Q, R) > 1.
      return;
    }

    // P and Q are always overwritten below.
    // R_ is likely dead too?
    const auto &[P_, Q_, R_] = pqro.value();
    BigIntToBigInteger(R_, &ValR);

    BigInteger value;
    BigIntToBigInteger(Value, &value);

    BigInt Discr = BigIntegerToBigInt(&discr);

    // Expected to agree because PerformTransformation doesn't modify B?
    const bool isBeven = B.IsEven();
    CHECK(isBeven == isBeven_XXX);

    // Compute P as floor((2*a*theta + b)/2)
    BigInt P = (((A << 1) * Value) + B);
    // BigIntAdd(&ValA, &ValA, &ValP);
    // (void)BigIntMultiply(&ValP, &value, &ValP);
    // BigIntAdd(&ValP, &ValB, &ValP);
    if (P.IsOdd()) P -= 1;
    P >>= 1;
    // subtractdivide(&ValP, ValP.limbs[0].x & 1, 2);

    // Compute Q = a*abs(K)
    BigInt Q = BigInt::Abs(K) * A;
    // CopyBigInt(&ValQ, &ValK);
    // ValQ.sign = SIGN_POSITIVE;
    // (void)BigIntMultiply(&ValQ, &ValA, &ValQ);

    // Find U, V, L so we can compute the continued fraction
    // expansion of (U+sqrt(L))/V.
    BigInt L = Discr;
    // CopyBigInt(&ValL, &discr);

    BigInt U, V;
    if (isBeven) {
      U = P;
      // CopyBigInt(&ValU, &ValP);       // U <- P
      // Argument of square root is discriminant/4.
      L >>= 2;
      // subtractdivide(&ValL, 0, 4);
      V = Q;
      // CopyBigInt(&ValV, &ValQ);
    } else {
      // U <- 2P+1
      U = (P << 1) + 1;
      // BigIntAdd(&ValP, &ValP, &ValU);
      // addbigint(&ValU, 1);
      // V <- 2Q
      V = (Q << 1);
      // BigIntAdd(&ValQ, &ValQ, &ValV);
    }

    U = -U;
    // BigIntChSign(&ValU);

    // If L-U^2 is not multiple of V, there is no solution, so go out.
    // (void)BigIntMultiply(&ValU, &ValU, &bigTmp);
    // BigIntSubt(&ValL, &bigTmp, &bigTmp);
    // (void)BigIntRemainder(&bigTmp, &ValV, &bigTmp);
    // PERF divisibility check
    if (BigInt::CMod(L - U * U, V) != 0) {
      // No solutions using continued fraction.
      equationNbr += 2;
      return;
    }

    // Set G to floor(sqrt(L))
    BigInt G = BigInt::Sqrt(L);
    // SquareRoot(ValL.limbs, ValG.limbs, ValL.nbrLimbs, &ValG.nbrLimbs);
    // ValG.sign = SIGN_POSITIVE;          // g <- sqrt(discr).
    // Invalidate solutions.
    Xplus.reset();
    Xminus.reset();
    Yplus.reset();
    Yminus.reset();
    // Somewhere below these can get set. Can we return the values instead?

    // BigInt UBak = U;
    // BigInt VBak = V;
    contfracEqNbr = equationNbr + 2;

    // XXX pass args
    BigIntToBigInteger(G, &ValG);
    BigIntToBigInteger(P, &ValP);
    BigIntToBigInteger(Q, &ValQ);
    BigIntToBigInteger(L, &ValL);
    BigIntToBigInteger(U, &ValU);
    BigIntToBigInteger(V, &ValV);

    ContFrac(&value, FIRST_SOLUTION);    // Continued fraction of (U+G)/V

    positiveDenominator = 0;

    // should be no reason to do this, since we copied
    // U = UBak;
    // V = VBak;
    // BigIntToBigInteger(UBak, &ValU);
    // BigIntToBigInteger(VBak, &ValV);

    U = -U;
    V = -V;
    // BigIntChSign(&ValU);
    // BigIntChSign(&ValV);

    contfracEqNbr = equationNbr + 3;
    // XXX why don't we have a check on the number of limbs here?
    BigIntToBigInteger(U, &ValU);
    BigIntToBigInteger(V, &ValV);
    if ((ValU.limbs[0].x == 3) && (ValV.limbs[0].x == 9)) {
      contfracEqNbr++;
    }

    ContFrac(&value, SECOND_SOLUTION);   // Continued fraction of (-U+G)/(-V)
    showSolution = ONE_SOLUTION;

    if (Xplus.has_value()) {
      CHECK(Yplus.has_value());
      // Result box:
      ShowXYOne(ExchXY, Xplus.value(), Yplus.value());
    }

    if (Xminus.has_value()) {
      CHECK(Yminus.has_value());
      // Result box:
      ShowXYOne(ExchXY, Xminus.value(), Yminus.value());
    }
    equationNbr += 4;
  }

  // Copy intentional; we modify them in place (factor out gcd).
  // PS: This is where to understand the meaning of Alpha, Beta, K, Div.
  void SolveQuadEquation(BigInt A, BigInt B, BigInt C,
                         BigInt D, BigInt E, BigInt F) {
    showSolution = ONE_SOLUTION;
    showRecursiveSolution = 0;    // Do not show recursive solution by default.
    divgcd = "<p>Dividing the equation by the greatest common divisor "
      "we obtain:</p>";

    BigInt gcd = BigInt::GCD(BigInt::GCD(A, B),
                             BigInt::GCD(BigInt::GCD(C, D),
                                         E));

    // PERF divisibility check
    if (gcd != 0 && BigInt::CMod(F, gcd) != 0) {
      // F is not multiple of GCD(A, B, C, D, E) so there are no solutions.
      clean.ShowText("<p>There are no solutions.</p>");
      return;
    }

    // Divide all coefficients by GCD(A, B, C, D, E).
    // PERF: Known-divisible operation
    if (gcd != 0) {
      A /= gcd;
      B /= gcd;
      C /= gcd;
      D /= gcd;
      E /= gcd;
      F /= gcd;
    }

    if (VERBOSE)
      printf("After dividing: %s %s %s %s %s %s\n",
             A.ToString().c_str(),
             B.ToString().c_str(),
             C.ToString().c_str(),
             D.ToString().c_str(),
             E.ToString().c_str(),
             F.ToString().c_str());

    // Test whether the equation is linear. A = B = C = 0.
    if (A == 0 && B == 0 && C == 0) {
      LinearSolution sol = LinearEq(D, E, F);
      // Result box:
      CHECK(!ExchXY);
      clean.PrintLinear(sol, "t");
      return;
    }

    // Compute discriminant: b^2 - 4ac.
    const BigInt Discr = B * B - ((A * C) << 2);

    if (Discr == 0) {
      // Discriminant is zero.
      DiscriminantIsZero(A, B, C, D, E, F);
      return;
    }

    // Compute gcd(a,b,c).
    // BigIntGcd(&ValA, &ValB, &bigTmp);
    // BigIntGcd(&bigTmp, &ValC, &U1);

    BigInt UU1 = BigInt::GCD(BigInt::GCD(A, B), C);
    BigInt Div, K, Alpha, Beta;
    // Discriminant is not zero.
    if (D == 0 && E == 0) {
      // Do not translate origin.
      Div = BigInt(1);
      // intToBigInteger(&ValDiv, 1);
      K = -F;
      // CopyBigInt(&ValK, &ValF);
      // BigIntChSign(&ValK);
      Alpha = BigInt(0);
      Beta = BigInt(0);
      // intToBigInteger(&ValAlpha, 0);
      // intToBigInteger(&ValBeta, 0);
    } else {
      Div = Discr;
      // CopyBigInt(&ValDiv, &discr);
      // Translate the origin (x, y) by (alpha, beta).
      // Compute alpha = 2cd - be
      Alpha = ((C * D) << 1) - (B * E);
      // (void)BigIntMultiply(&ValC, &ValD, &ValAlpha);
      // BigIntMultiplyBy2(&ValAlpha);
      // (void)BigIntMultiply(&ValB, &ValE, &bigTmp);
      // BigIntSubt(&ValAlpha, &bigTmp, &ValAlpha);

      // Compute beta = 2ae - bd
      Beta = ((A * E) << 1) - (B * D);
      // (void)BigIntMultiply(&ValA, &ValE, &ValBeta);
      // BigIntMultiplyBy2(&ValBeta);
      // (void)BigIntMultiply(&ValB, &ValD, &bigTmp);
      // BigIntSubt(&ValBeta, &bigTmp, &ValBeta);

      // We get the equation ax^2 + bxy + cy^2 = k
      // where k = -D (ae^2 - bed + cd^2 + fD)

      // BigInt AE = A * E;
      // BigInt AEE = AE * E;
      // (void)BigIntMultiply(&ValA, &ValE, &ValK);     // ae
      // (void)BigIntMultiply(&ValK, &ValE, &ValK);     // ae^2
      K = (-Discr) * ((A * E * E) - (B * E * D) + (C * D * D) + (F * Discr));
      // (void)BigIntMultiply(&ValB, &ValE, &bigTmp);   // be
      // (void)BigIntMultiply(&bigTmp, &ValD, &bigTmp); // bed
      // BigIntSubt(&ValK, &bigTmp, &ValK);        // ae^2 - bed
      // (void)BigIntMultiply(&ValC, &ValD, &bigTmp);   // cd
      // (void)BigIntMultiply(&bigTmp, &ValD, &bigTmp); // cd^2
      // BigIntAdd(&ValK, &bigTmp, &ValK);              // ae^2 - bed + cd^2
      // (void)BigIntMultiply(&ValF, &discr, &bigTmp);  // fD
      // BigIntAdd(&ValK, &bigTmp, &ValK);              // ae^2 - bed + cd^2 + fD
      // (void)BigIntMultiply(&ValK, &discr, &ValK);    // D (ae^2 - bed + cd^2 + fD)
      // BigIntChSign(&ValK);                           // k
    }

    // const BigInt Alpha = BigIntegerToBigInt(&ValAlpha);
    // const BigInt Beta = BigIntegerToBigInt(&ValBeta);
    // const BigInt Div = BigIntegerToBigInt(&ValDiv);

    // A = BigIntegerToBigInt(&ValA);
    // B = BigIntegerToBigInt(&ValB);
    // C = BigIntegerToBigInt(&ValC);
    // BigInt K = BigIntegerToBigInt(&ValK);

    // XXX remove this state
    BigIntToBigInteger(A, &ValA);
    BigIntToBigInteger(B, &ValB);
    BigIntToBigInteger(C, &ValC);
    BigIntToBigInteger(D, &ValD);
    BigIntToBigInteger(E, &ValE);
    BigIntToBigInteger(F, &ValF);
    BigIntToBigInteger(Discr, &discr);
    BigIntToBigInteger(Alpha, &ValAlpha);
    BigIntToBigInteger(Beta, &ValBeta);
    BigIntToBigInteger(Div, &ValDiv);
    BigIntToBigInteger(K, &ValK);

    // const BigInt UU1 = BigIntegerToBigInt(&U1);

    // If k is not multiple of gcd(A, B, C), there are no solutions.
    // (void)BigIntRemainder(&ValK, &U1, &bigTmp);
    if (BigInt::CMod(K, UU1) != 0) {
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
      NegativeDiscriminant(A, B, C, K, Discr, Alpha, Beta, Div);
      return;
    }

    // const BigInt Discr = BigIntegerToBigInt(&discr);
    const BigInt G = BigInt::Sqrt(Discr);

    // SquareRoot(discr.limbs, ValG.limbs, discr.nbrLimbs, &ValG.nbrLimbs);
    // ValG.sign = SIGN_POSITIVE;
    // (void)BigIntMultiply(&ValG, &ValG, &bigTmp);
    if (G * G == Discr) {
      // Discriminant is a perfect square.

      PerfectSquareDiscriminant(
          A, B, C, G, K,
          Alpha, Beta, Div, Discr);

      return;
    } else {
      PositiveDiscriminant(A, B, C, K, Discr, Alpha, Beta, Div);
    }
  }

  void QuadBigInt(const BigInt &A, const BigInt &B, const BigInt &C,
                  const BigInt &D, const BigInt &E, const BigInt &F) {
    clean.ShowText("2<p>");

    clean.ShowText("<h2>");
    clean.ShowEq(A, B, C, D, E, F, "x", "y");
    clean.ShowText(" = 0</h2>");
    SolNbr = 0;

    size_t preamble_size = (clean.output == nullptr) ? 0 : clean.output->size();

    SolveQuadEquation(A, B, C, D, E, F);

    if (clean.output != nullptr && clean.output->size() == preamble_size) {
      clean.ShowText("<p>The equation does not have integer solutions.</p>");
    }
  }

  Quad() {
    MarkUninitialized();
  }

};


void QuadBigInt(const BigInt &a, const BigInt &b, const BigInt &c,
                const BigInt &d, const BigInt &e, const BigInt &f,
                std::string *output) {
  std::unique_ptr<Quad> quad(new Quad);
  quad->clean.output = output;
  quad->QuadBigInt(a, b, c, d, e, f);
}
