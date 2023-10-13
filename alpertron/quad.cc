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

enum class SolutionNumber {
  FIRST,
  SECOND,
};

enum class QmodCallbackType {
  PARABOLIC = 0,
  ELLIPTIC,
  HYPERBOLIC,
};

[[maybe_unused]]
static std::string CallbackString(QmodCallbackType t) {
  switch (t) {
  case QmodCallbackType::PARABOLIC: return "PARABOLIC";
  case QmodCallbackType::ELLIPTIC: return "ELLIPTIC";
  case QmodCallbackType::HYPERBOLIC: return "HYPERBOLIC";
  default: return "???";
  }
}

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
      // PERF QuotRem
      sol.Yind = -(coeffInd / coeffY);
      sol.Ylin = BigInt(0);
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
// On input G = floor(g), g > 0.
// g is not an integer number.
static bool CheckStartOfContinuedFractionPeriod(const BigInt &U,
                                                const BigInt &V,
                                                const BigInt &G) {
  if (G >= BigInt::Abs(U)) {
    // First check |u| < g passed.
    // Set Tmp1 to |v|
    BigInt Tmp1 = BigInt::Abs(V);
    // Compute Tmp2 as u + floor(g) which equals floor(u+g)
    BigInt Tmp2 = U + G;

    if (Tmp2 < 0) {
      // Round to number nearer to zero.
      Tmp2 += 1;
      // addbigint(&Tmp2, 1);
    }

    Tmp2 = BigInt::Abs(Tmp2);

    // Compute Tmp2 as floor(|u+g|)
    // Compute bigTmp as floor(|u+g|) - |v|
    if (Tmp2 >= Tmp1) {
      // Second check |u+g| > |v| passed.
      // Compute Tmp2 as u - floor(g)
      Tmp2 = U - G;

      if (Tmp2 <= 0) {
        // Round down number to integer.
        Tmp2 -= 1;
      }

      Tmp2 = BigInt::Abs(Tmp2);

      // Compute Tmp2 as floor(|u-g|)
      // Compute Tmp2 as |v| - floor(|u-g|)
      if (Tmp1 >= Tmp2) {
        // Third check |u-g| < |v| passed.
        // Save U and V to check period end.
        return true;
      }
    }
  }
  return false;
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
  // TODO: Lots of these could be local; dynamically sized.
  BigInteger ValA;
  BigInteger ValB;
  BigInteger ValC;
  BigInteger ValD;
  BigInteger ValE;
  BigInteger ValI;
  BigInteger ValM;
  BigInteger ValU;
  BigInteger ValV;
  BigInteger ValR;
  BigInteger ValK;
  BigInteger ValAlpha;
  BigInteger ValBeta;
  BigInteger ValDiv;
  BigInteger discr;
  int showRecursiveSolution = 0;
  // BigInt Xind, Yind, Xlin, Ylin;
  bool solFound = false;

  bool ExchXY = false;

  const char *varT = "t";


  std::optional<BigInt> Xplus;
  std::optional<BigInt> Xminus;
  std::optional<BigInt> Yplus;
  std::optional<BigInt> Yminus;

  // These will point to xplus/minus etc. above, and those
  // get set through the pointers. Gross!
  std::optional<BigInt> *Xbak = nullptr, *Ybak = nullptr;

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
    std::tuple<BigInt, BigInt, BigInt,
               BigInt, BigInt, BigInt> GetNextConvergent(
                   BigInt U, BigInt U1, BigInt U2,
                   BigInt V, BigInt V1, BigInt V2) {
      BigInt Tmp = FloorDiv(U, V);

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

    void ShowXYOne(bool swap_xy, const BigInt &X, const BigInt &Y) {
      showAlso();
      if (swap_xy)
        ShowSolutionXY(Y, X);
      else
        ShowSolutionXY(X, Y);
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
        const BigInt &ABack, const BigInt &BBack, const BigInt &CBack,
        const BigInt &Alpha, const BigInt &Beta,
        const BigInt &GcdHomog, BigInt Discr) {

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
        if (SolutionFoundFromContFraction(isBeven, 4,
                                          Alpha, Beta,
                                          A, B, C,
                                          Discr,
                                          BigInt(3),
                                          BigInt(1))) {
          return;
        }

        // 9,4 is first solution to U1^2 - 5*V1^2 = 1
        (void)SolutionFoundFromContFraction(isBeven, 1,
                                            Alpha, Beta,
                                            A, B, C,
                                            Discr,
                                            BigInt(9),
                                            BigInt(4));
        return;
      }

      // g <- sqrt(discr).
      BigInt G = BigInt::Sqrt(H);
      // Port note: Was explicit SIGN_POSITIVE here in original, but I think that
      // was just because it was manipulating the limbs directly? Sqrt
      // is always non-negative...
      CHECK(G >= 0);

      int periodLength = 1;

      BigInt U(0);
      BigInt V(1);

      BigInt UU2(0);
      BigInt UU1(1);

      BigInt VV2(1);
      BigInt VV1(0);

      BigInt UBak, VBak;

      if (gcd_homog != 1) {
        periodLength = -1;
        do {
          // fprintf(stderr, "rec_gcdhomognotone coverage H=%s\n",
          // H.ToString().c_str());
          BigInt BigTmp = U + G;
          if (V < 0) {
            // If denominator is negative, round square root upwards.
            BigTmp += 1;
          }

          // Tmp1 = Term of continued fraction.
          BigInt Tmp1 = FloorDiv(BigTmp, V);

          // U <- a*V - U
          U = Tmp1 * V - U;

          // Port note: In alpertron, this uses L, which is used as
          // the discriminant (or discriminant/4) in ContFrac and may
          // have the same value here. But I think this was a bug; H
          // is discr (or discr/4) in this one. Passing around L
          // rather than setting a global results it in being
          // uninitialized here. Unfortunately, not good coverage of
          // this loop.

          // V <- (D - U^2)/V
          V = (H - U * U) / V;

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
      /*
      printf("U=%s V=%s period=%d\n", U.ToString().c_str(), V.ToString().c_str(),
             periodLength);
      */

      if (periodLength > 1) {
        // quad.exe 6301 1575 2 7199 -1 -114995928
        printf("nonzeroperiod coverage (%d)\n", periodLength);
      }

      ShowText("<p>Recursive solutions:</p><p>");

      // XXX Rather than overwrite, substitute in below?
      A = ABack;
      B = BBack;
      C = CBack;


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
        BigInt UU3 = UU2;
        UU2 = UU1;
        UU1 = Tmp1 * UU2 + UU3;

        // V3 <- V2, V2 <- V1, V1 <- a*V2 + V3
        BigInt VV3 = VV2;
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
        if (((periodNbr * periodLength) % gcd_homog) != 0) {
          continue;
        }


        // Found solution from continued fraction.
        if (SolutionFoundFromContFraction(isBeven,
                                          BigInt::Abs(V).ToInt().value(),
                                          Alpha, Beta,
                                          A, B, C,
                                          Discr,
                                          UU1, VV1)) {
          return;
        }
      }
    }


  };  // Clean


  Clean clean;

  void MarkUninitialized() {
    // Port note: There are various interleaved code paths where
    // different state (e.g. callback type) results in these member
    // variables being initialized or not. At least set them to
    // valid state so that we can convert them to BigInt (and discard).
    for (BigInteger *b : {
          &ValA, &ValB, &ValC, &ValD, &ValE,
          &ValI, &ValM,
          &ValU, &ValV, &ValR, &ValK,
          &ValAlpha, &ValBeta, &ValDiv,
          &discr
            }) {
      intToBigInteger(b, 0xCAFE);
    }
  }

  struct Solution {

  };

  // This weird function either shows the solution or continues
  // to try to find the minimum, storing the state in Xbak, Ybak.
  // XXX need to take some kind of state to accumulate Xbak, Ybak...
  void ShowXYTwo(bool swap_xy, const BigInt &X, const BigInt &Y) {
    solFound = true;

    CHECK(Xbak != nullptr);
    CHECK(Ybak != nullptr);

    if (!Xbak->has_value()) {
      CHECK(!Ybak->has_value());
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
  }

  // TODO: Try to make this dispatch (callbackQuadModType) static.
  template<QmodCallbackType QMOD_CALLBACK>
  void SolutionX(BigInt Value, const BigInt &Modulus,
                 const BigInt &A, const BigInt &B, const BigInt &C,
                 const BigInt &D, const BigInt &E,
                 const BigInt &M, const BigInt &K, const BigInt &I,
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
      printf("  with %s %s %s %s %s | %s %s %s | %s %s\n",
             A.ToString().c_str(),
             B.ToString().c_str(),
             C.ToString().c_str(),
             D.ToString().c_str(),
             E.ToString().c_str(),
             M.ToString().c_str(),
             K.ToString().c_str(),
             I.ToString().c_str(),
             U.ToString().c_str(),
             V.ToString().c_str());
    }

    switch (QMOD_CALLBACK) {
    case QmodCallbackType::PARABOLIC:
      clean.CallbackQuadModParabolic(ExchXY,
                                     A, B, C, D, E,
                                     U, V, I, Value);
      break;

    case QmodCallbackType::ELLIPTIC:
      CallbackQuadModElliptic(A, B, C, E, M, K,
                              Alpha, Beta, Div, Discr,
                              Value);
      break;

    case QmodCallbackType::HYPERBOLIC:
      CallbackQuadModHyperbolic(A, B, C, K, Discr, Value);
      break;

    default:
      break;
    }
  }

  // Solve congruence an^2 + bn + c = 0 (mod n) where n is different from zero.
  template<QmodCallbackType QMOD_CALLBACK>
  void SolveQuadModEquation(
      const BigInt &coeffQuadr,
      const BigInt &coeffLinear,
      const BigInt &coeffIndep,
      BigInt Modulus,
      const BigInt &A, const BigInt &B, const BigInt &C, const BigInt &D, const BigInt &E,
      const BigInt &M, const BigInt &K, const BigInt &I, const BigInt &U, const BigInt &V,
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

    if (GcdAll != 0 && BigInt::CMod(coeff_indep, GcdAll) != 0) {
      // ValC must be multiple of gcd(ValA, ValB).
      // Otherwise go out because there are no solutions.
      return;
    }

    GcdAll = BigInt::GCD(Modulus, GcdAll);

    // PERF: version of division where we know it's divisible.
    // Divide all coefficients by gcd(ValA, ValB).
    if (GcdAll != 0) {
      coeff_quadr /= GcdAll;
      coeff_linear /= GcdAll;
      coeff_indep /= GcdAll;
      Modulus /= GcdAll;
    }

    BigInt ValNn = Modulus;

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
          SolutionX<QMOD_CALLBACK>(BigInt(ctr), Modulus,
                                   A, B, C, D, E,
                                   M, K, I,
                                   U, V,
                                   Alpha, Beta, Div, Discr);
        }
      }
      return;
    }

    // PERF divisibility check
    if (BigInt::CMod(coeff_quadr, Modulus) == 0) {
      // Linear equation.
      printf("linear-eq coverage\n");

      if (BigInt::GCD(coeff_linear, Modulus) != 1) {
        // ValB and ValN are not coprime. Go out.
        return;
      }

      // Calculate z <- -ValC / ValB (mod ValN)

      // We only use this right here, so we could have a version of MGParams
      // that just took a BigInt modulus, at least for this code.
      limb TheModulus[MAX_LEN];
      const int modulus_length = BigIntToLimbs(Modulus, TheModulus);
      TheModulus[modulus_length].x = 0;

      // Is it worth it to convert to montgomery form for one division??
      const MontgomeryParams params = GetMontgomeryParams(modulus_length, TheModulus);

      BigInt z = BigIntModularDivision(params, coeff_indep, coeff_linear, Modulus);

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
        SolutionX<QMOD_CALLBACK>(z, Modulus,
                                 A, B, C, D, E,
                                 M, K, I,
                                 U, V,
                                 Alpha, Beta, Div, Discr);
        z += Modulus;
        if (z < Temp0) break;
      }

      return;
    }

    if (QMOD_CALLBACK == QmodCallbackType::PARABOLIC) {
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
      // BigIntToBigInteger(Modulus, &this->modulus);

      if (VERBOSE) {
        printf("[Call SolveEq] %s %s %s %s %s %s\n",
               coeff_quadr.ToString().c_str(),
               coeff_linear.ToString().c_str(),
               coeff_indep.ToString().c_str(),
               Modulus.ToString().c_str(),
               GcdAll.ToString().c_str(),
               ValNn.ToString().c_str());
      }

      SolveEquation(
          SolutionFn([&](const BigInt &Value) {
              this->SolutionX<QMOD_CALLBACK>(
                  Value,
                  Modulus,
                  A, B, C, D, E,
                  M, K, I,
                  U, V,
                  Alpha, Beta, Div, Discr);
            }),
          coeff_quadr, coeff_linear, coeff_indep,
          Modulus, GcdAll, ValNn);
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

    CHECK(BigIntegerToBigInt(&ValU) == U);

    // BigInt Modulus = BigIntegerToBigInt(&ValU);

    const BigInt M = BigIntegerToBigInt(&ValM);
    const BigInt K = BigIntegerToBigInt(&ValK);
    const BigInt I = BigIntegerToBigInt(&ValI);
    const BigInt Alpha = BigIntegerToBigInt(&ValAlpha);
    const BigInt Beta = BigIntegerToBigInt(&ValBeta);
    const BigInt Div = BigIntegerToBigInt(&ValDiv);
    const BigInt Discr = BigIntegerToBigInt(&discr);

    SolveQuadModEquation<QmodCallbackType::PARABOLIC>(
        // Coefficients and modulus
        BigInt(1), BigInt(0), -V, BigInt::Abs(U),
        // Problem state
        A, B, C, D, E,
        M, K, I, U, V,
        Alpha, Beta, Div, Discr);
  }

  void ShowPoint(bool two_solutions,
                 const BigInt &X, const BigInt &Y,
                 const BigInt &Alpha, const BigInt &Beta,
                 const BigInt &Div) {

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

    // (I think this should actually be impossible because Div comes from
    // the GCD of the coefficients.)
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
      if (two_solutions) {
        // CHECK(callbackQuadModType == QmodCallbackType::HYPERBOLIC);
        ShowXYTwo(ExchXY, tmp1, tmp2);
      } else {
        // CHECK(callbackQuadModType != QmodCallbackType::HYPERBOLIC)
        // fprintf(stderr, "Non-Hyperbolic: %s\n", two_solutions ? "two" : "one");
        // Result box:
        clean.ShowXYOne(ExchXY, tmp1, tmp2);
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
  template<QmodCallbackType QMOD_CALLBACK>
  void NonSquareDiscriminant(BigInt A, BigInt B, BigInt C,
                             BigInt K, BigInt Discr,
                             BigInt Alpha, BigInt Beta, const BigInt &Div) {
    // Find GCD(a,b,c)
    BigInt GcdHomog = BigInt::GCD(BigInt::GCD(A, B), C);
    // Divide A, B, C and K by this GCD.
    if (GcdHomog != 0) {
      A /= GcdHomog;
      B /= GcdHomog;
      C /= GcdHomog;
      K /= GcdHomog;
      // Divide discriminant by the square of GCD.
      Discr /= GcdHomog;
      Discr /= GcdHomog;
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

    if (VERBOSE) {
      printf("start NSD %s %s %s | %s %s | %s %s %s\n",
             A.ToString().c_str(), B.ToString().c_str(), C.ToString().c_str(),
             K.ToString().c_str(), Discr.ToString().c_str(),
             Alpha.ToString().c_str(), Beta.ToString().c_str(), Div.ToString().c_str());
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

    BigIntToBigInteger(A, &ValA);
    BigIntToBigInteger(B, &ValB);
    BigIntToBigInteger(C, &ValC);
    BigIntToBigInteger(M, &ValM);
    const BigInt D = BigIntegerToBigInt(&ValD);
    const BigInt I = BigIntegerToBigInt(&ValI);
    const BigInt U = BigIntegerToBigInt(&ValU);
    const BigInt V = BigIntegerToBigInt(&ValV);


    for (;;) {
      // Ugh, SQME depends on additional state (SolutionX).
      // These two are modified in this loop.
      BigIntToBigInteger(E, &ValE);
      BigIntToBigInteger(K, &ValK);


      SolveQuadModEquation<QMOD_CALLBACK>(
          // Coefficients and modulus
          A, B, C, BigInt::Abs(K),
          // Problem state
          A, B, C, D, E,
          M, K, I, U, V,
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
          // const auto &[fact, multiplicity] = factors[fidx];
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
          // auto &[fact, multiplicity] = factors[indexEvenMultiplicity[index]];
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
      // CopyBigInt(&LastModulus, &ValK);

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

    if (showRecursiveSolution &&
        QMOD_CALLBACK == QmodCallbackType::HYPERBOLIC) {

      // Show recursive solution.
      clean.RecursiveSolution(A, B, C,
                              ABack, BBack, CBack,
                              Alpha, Beta, GcdHomog, Discr);
    }
  }

  void NegativeDiscriminant(const BigInt &A, const BigInt &B, const BigInt &C,
                            const BigInt &K, const BigInt &Discr,
                            const BigInt &Alpha, const BigInt &Beta, const BigInt &Div) {
    NonSquareDiscriminant<QmodCallbackType::ELLIPTIC>(
        A, B, C, K, Discr, Alpha, Beta, Div);
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

    // Port note: This used to modify the value of K based on the callback
    // type, but now we do that at the call site. (Also there was something
    // suspicious in here where it flipped the sign and then set it negative.)

    // X = (tu - Kv)*E
    const BigInt Z = (Value * H - K * I) * E;
    // Y = u*E
    const BigInt O = H * E;

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
    BigIntToBigInteger(R, &ValR);

    CHECK(Discr <= 0);

    std::optional<int64_t> plow_opt = P.ToInt();
    if (plow_opt.has_value() && plow_opt.value() >= 0) {
      int64_t plow = plow_opt.value();
      if (Discr < -4 && plow == 1) {
        // Discriminant is less than -4 and P equals 1.

        NonSquareDiscrSolution(
            false,
            M, E, K,
            Alpha, Beta, Div,
            BigInt(1), BigInt(0),
            Value);   // (1, 0)

        return;
      }

      if (Discr == -4) {
        // Discriminant is equal to -4.
        BigInt G = Q >> 1;

        if (plow == 1) {
          NonSquareDiscrSolution(
              false,
              M, E, K,
              Alpha, Beta, Div,
              BigInt(1), BigInt(0),
              Value);

          intToBigInteger(&ValI, -1);

          NonSquareDiscrSolution(
              false,
              M, E, K,
              Alpha, Beta, Div,
              G, BigInt(-1),
              Value);   // (Q/2, -1)

          return;
        } if (plow == 2) {

          NonSquareDiscrSolution(
              false,
              M, E, K,
              Alpha, Beta, Div,
              (G - 1) >> 1, BigInt(-1),
              Value);   // ((Q/2-1)/2, -1)

          NonSquareDiscrSolution(
              false,
              M, E, K,
              Alpha, Beta, Div,
              (G + 1) >> 1, BigInt(-1),
              Value);   // ((Q/2+1)/2, -1)

          return;
        }
      }

      if (Discr == -3) {
        // Discriminant is equal to -3.
        if (plow == 1) {

          // printf("plow1 coverage\n");
          NonSquareDiscrSolution(
              false,
              M, E, K,
              Alpha, Beta, Div,
              BigInt(1), BigInt(0),
              Value);   // (1, 0)

          NonSquareDiscrSolution(
              false,
              M, E, K,
              Alpha, Beta, Div,
              (Q - 1) >> 1, BigInt(-1),
              Value);   // ((Q-1)/2, -1)

          NonSquareDiscrSolution(
              false,
              M, E, K,
              Alpha, Beta, Div,
              (Q + 1) >> 1, BigInt(-1),
              Value);   // ((Q+1)/2, -1)

          return;
        } else if (plow == 3) {

          // printf("plow3 coverage\n");

          NonSquareDiscrSolution(
              false,
              M, E, K,
              Alpha, Beta, Div,
              (Q + 3) / 6, BigInt(-1),
              Value);   // ((Q+3)/6, -1)

          NonSquareDiscrSolution(
              false,
              M, E, K,
              Alpha, Beta, Div,
              Q / 3, BigInt(-2),
              Value);   // (Q/3, -2)

          NonSquareDiscrSolution(
              false,
              M, E, K,
              Alpha, Beta, Div,
              (Q - 3) / 6, BigInt(-1),
              Value);   // ((Q-3)/6, -1)

          return;
        }
      }
    }

    // Compute bound L = sqrt(4P/(-D))
    const BigInt L = BigInt::Sqrt((P << 2) / -Discr);

    // HERE!

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
        clean.GetNextConvergent(U, U1, U2,
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
        NonSquareDiscrSolution(
            false,
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
            clean.GetNextConvergent(U, U1, U2,
                                    V, V1, V2);

          // XXX dead?
          BigInt I = V1;
          BigIntToBigInteger(I, &ValI);
          // CopyBigInt(&ValI, &V1);

          NonSquareDiscrSolution(
              false,
              M, E, K,
              Alpha, Beta, Div,
              U1, V1,
              Value);

          if (d == -3) {
            std::tie(U, U1, U2, V, V1, V2) =
                clean.GetNextConvergent(U, U1, U2, V, V1, V2);

            NonSquareDiscrSolution(
                false,
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

        {
          // Solve bDx + cDy + b*alpha + c*beta = 0
          const BigInt Aux0 = B * Discr;
          const BigInt Aux1 = C * Discr;
          const BigInt Aux2 = B * Alpha + C * Beta;

          LinearSolution sol = LinearEq(Aux0, Aux1, Aux2);
          CHECK(!ExchXY);
          // Result box:
          clean.PrintLinear(sol, "t");
        }

      } else {
        // printf("kzeroanzero coverage\n");
        // Coefficient a does not equal zero.

        const BigInt AAlpha2 = (A * Alpha) << 1;

        {
          // Solve 2aD x + (b+g)D y = 2a*alpha + (b+g)*beta
          const BigInt Aux0 = (A * Discr) << 1;
          const BigInt Aux1 = (B + G) * Discr;
          const BigInt Aux2 = -(AAlpha2 + (B + G) * Beta);

          LinearSolution sol = LinearEq(Aux0, Aux1, Aux2);
          // Result box:
          CHECK(!ExchXY);
          clean.PrintLinear(sol, "t");
        }

        {
          // Solve 2aD x + (b-g)D y = 2a*alpha + (b-g)*beta
          const BigInt Aux0 = A << 1;
          // Port note: At some point this was erroneously
          // multiplied by the value of aux1 above.
          const BigInt Aux1 = (B - G) * Discr;
          const BigInt Aux2 = -(AAlpha2 + (B - G) * Beta);

          LinearSolution sol = LinearEq(Aux0, Aux1, Aux2);
          /*
          if (sol.type == LinearSolutionType::SOLUTION_FOUND) {
            printf("bminusg coverage\n");
          }
          */

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

    // We have to find all factors of the right hand side.

    // Compute all factors of Z = 4ak/RS

    // Factor positive number.
    std::vector<std::pair<BigInt, int>> factors =
      BigIntFactor(BigInt::Abs(Z));

    // Do not factor again same modulus.

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
    BigIntToBigInteger(NewK, &ValK);

    // Loop that finds all factors of Z.
    // Use Gray code to use only one big number.
    // Gray code: 0->000, 1->001, 2->011, 3->010, 4->110, 5->111, 6->101, 7->100.
    // Change from zero to one means multiply, otherwise divide.
    std::vector<int> counters(400, 0);
    std::vector<bool> is_descending(400, false);

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
        if (!is_descending[index]) {
          // Ascending.
          if (counters[index] == factors[fidx].second) {
            // Next time it will be descending.
            is_descending[index] = true;
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
          // Next time it will be ascending.
          is_descending[index] = false;
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
    NonSquareDiscriminant<QmodCallbackType::HYPERBOLIC>(
        A, B, C, K, Discr, Alpha, Beta, Div);
  }

  // Used for hyperbolic curve.
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
  void ContFrac(const BigInt &Value, enum SolutionNumber solutionNbr,
                const BigInt &A, const BigInt &B, const BigInt &E,
                const BigInt &K, const BigInt &L, const BigInt &M,
                BigInt U, BigInt V, BigInt G,
                const BigInt &Alpha, const BigInt &Beta,
                const BigInt &Div, const BigInt &Discr) {

    const bool isBeven = B.IsEven();
    // If (D-U^2) is not multiple of V, exit routine.
    if (((L - U * U) % V) != 0) {
      return;
    }

    BigInt U1(1);
    BigInt U2(0);
    BigInt V1(0);
    BigInt V2(1);

    // Initialize variables.
    // Less than zero means outside period.
    // Port note: Original code left startperiodv uninitialized, though
    // it was probably only accessed when startperiodu is non-negative.
    BigInt StartPeriodU(-1);
    BigInt StartPeriodV(-1);
    int index = 0;

    if (solutionNbr == SolutionNumber::SECOND) {
      index++;
    }

    bool isIntegerPart = true;

    const bool k_neg = K < 0;
    const bool a_neg = A < 0;

    int periodIndex = 0;
    for (;;) {

      const bool v_neg = V < 0;

      if (V == (isBeven ? 1 : 2) &&
          ((index & 1) == (k_neg == v_neg ? 0 : 1))) {
        // Two solutions
        solFound = false;

        // Found solution.
        if (BigInt::Abs(Discr) == 5 && (a_neg != k_neg) &&
            (solutionNbr == SolutionNumber::FIRST)) {
          // Determinant is 5 and aK < 0. Use exceptional solution (U1-U2)/(V1-V2).

          // printf("aaaaaaa coverage\n");

          NonSquareDiscrSolution(
              true,
              M, E, -BigInt::Abs(K),
              Alpha, Beta, Div,
              V1 - V2,
              U1 - U2,
              Value);

        } else {
          // Determinant is not 5 or aK > 0. Use convergent U1/V1 as solution.

          NonSquareDiscrSolution(
              true,
              M, E, -BigInt::Abs(K),
              Alpha, Beta, Div,
              V1, U1, Value);

        }

        if (solFound) {
          break;                             // Solution found. Exit loop.
        }
      }

      if (StartPeriodU >= 0) {
        // Already inside period.
        periodIndex++;
        if (U == StartPeriodU &&
            V == StartPeriodV &&
            // New period started.
            (periodIndex & 1) == 0) {
          // Two periods of period length is odd, one period if even.
          break;  // Go out in this case.
        }

      } else if (!isIntegerPart) {
        // Check if periodic part of continued fraction has started.

        if (CheckStartOfContinuedFractionPeriod(U, V, G)) {
          StartPeriodU = U;
          StartPeriodV = V;
        }
      }

      // Get continued fraction coefficient.
      BigInt BigTmp = U + G;
      if (V < 0) {
        // If denominator is negative, round square root upwards.
        BigTmp += 1;
      }

      // Tmp1 = Term of continued fraction.
      BigInt Tmp1 = FloorDiv(BigTmp, V);
      // Update convergents.
      // U3 <- U2, U2 <- U1, U1 <- a*U2 + U3
      BigInt U3 = U2;
      U2 = U1;
      U1 = Tmp1 * U2 + U3;

      // V3 <- V2, V2 <- V1, V1 <- a*V2 + V3
      BigInt V3 = V2;
      V2 = V1;
      V1 = Tmp1 * V2 + V3;

      // Update numerator and denominator.
      U = Tmp1 * V - U;
      V = (L - U * U) / V;

      index++;
      isIntegerPart = false;
    }
  }

  void CallbackQuadModHyperbolic(const BigInt &A,
                                 const BigInt &B,
                                 const BigInt &C,
                                 const BigInt &K,
                                 const BigInt &Discr,
                                 const BigInt &Value) {
    auto pqro = PerformTransformation(A, B, C, K, Value);
    if (!pqro.has_value()) {
      // No solutions because gcd(P, Q, R) > 1.
      return;
    }

    // P and Q are always overwritten below.
    // R_ is likely dead too?
    const auto &[P_, Q_, R_] = pqro.value();
    BigIntToBigInteger(R_, &ValR);

    // Expected to agree because PerformTransformation doesn't modify B?
    const bool isBeven = B.IsEven();

    // Compute P as floor((2*a*theta + b)/2)
    BigInt P = (((A << 1) * Value) + B);

    if (P.IsOdd()) P -= 1;
    P >>= 1;

    // Compute Q = a*abs(K)
    BigInt Q = BigInt::Abs(K) * A;

    // Find U, V, L so we can compute the continued fraction
    // expansion of (U+sqrt(L))/V.
    BigInt L = Discr;

    BigInt U, V;
    if (isBeven) {
      U = P;
      // Argument of square root is discriminant/4.
      L >>= 2;
      V = Q;
    } else {
      // U <- 2P+1
      U = (P << 1) + 1;
      // V <- 2Q
      V = (Q << 1);
    }

    U = -U;

    // If L-U^2 is not multiple of V, there is no solution, so go out.
    // PERF divisibility check
    if (BigInt::CMod(L - U * U, V) != 0) {
      // No solutions using continued fraction.
      return;
    }

    // Set G to floor(sqrt(L))
    BigInt G = BigInt::Sqrt(L);
    // Invalidate solutions.
    Xplus.reset();
    Xminus.reset();
    Yplus.reset();
    Yminus.reset();
    // Somewhere below these can get set. Can we return the values instead?

    // XXX pass args
    BigIntToBigInteger(U, &ValU);
    BigIntToBigInteger(V, &ValV);

    const BigInt E = BigIntegerToBigInt(&ValE);
    const BigInt M = BigIntegerToBigInt(&ValM);

    const BigInt Alpha = BigIntegerToBigInt(&ValAlpha);
    const BigInt Beta = BigIntegerToBigInt(&ValBeta);
    const BigInt Div = BigIntegerToBigInt(&ValDiv);

    // Continued fraction of (U+G)/V
    ContFrac(Value, SolutionNumber::FIRST,
             A, B, E,
             K, L, M,
             U, V, G,
             Alpha, Beta, Div, Discr);

    U = -U;
    V = -V;

    BigIntToBigInteger(U, &ValU);
    BigIntToBigInteger(V, &ValV);

    // Continued fraction of (-U+G)/(-V)
    ContFrac(Value, SolutionNumber::SECOND,
             A, B, E,
             K, L, M,
             U, V, G,
             Alpha, Beta, Div, Discr);


    if (Xplus.has_value()) {
      CHECK(Yplus.has_value());
      // Result box:
      clean.ShowXYOne(ExchXY, Xplus.value(), Yplus.value());
    }

    if (Xminus.has_value()) {
      CHECK(Yminus.has_value());
      // Result box:
      clean.ShowXYOne(ExchXY, Xminus.value(), Yminus.value());
    }
  }

  // Copy intentional; we modify them in place (factor out gcd).
  // PS: This is where to understand the meaning of Alpha, Beta, K, Div.
  void SolveQuadEquation(BigInt A, BigInt B, BigInt C,
                         BigInt D, BigInt E, BigInt F) {
    // Do not show recursive solution by default.
    showRecursiveSolution = 0;

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
      Div = Discr;
      // Translate the origin (x, y) by (alpha, beta).
      // Compute alpha = 2cd - be
      Alpha = ((C * D) << 1) - (B * E);

      // Compute beta = 2ae - bd
      Beta = ((A * E) << 1) - (B * D);

      // We get the equation ax^2 + bxy + cy^2 = k
      // where k = -D (ae^2 - bed + cd^2 + fD)

      K = (-Discr) * ((A * E * E) - (B * E * D) + (C * D * D) + (F * Discr));
    }

    // XXX remove this state
    BigIntToBigInteger(A, &ValA);
    BigIntToBigInteger(B, &ValB);
    BigIntToBigInteger(C, &ValC);
    BigIntToBigInteger(D, &ValD);
    BigIntToBigInteger(E, &ValE);
    BigIntToBigInteger(Discr, &discr);
    BigIntToBigInteger(Alpha, &ValAlpha);
    BigIntToBigInteger(Beta, &ValBeta);
    BigIntToBigInteger(Div, &ValDiv);
    BigIntToBigInteger(K, &ValK);

    // If k is not multiple of gcd(A, B, C), there are no solutions.
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
