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

enum eLinearSolution {
  SOLUTION_FOUND = 0,
  NO_SOLUTIONS,
  INFINITE_SOLUTIONS,
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

struct Quad {
  // XXX can be retired for two_solutions arg
  enum eShowSolution showSolution;
  // TODO: Lots of these could be local; dynamically sized.
  enum eCallbackQuadModType callbackQuadModType;
  char isDescending[400];
  BigInteger Aux0, Aux1, Aux2, Aux3, Aux5;
  BigInteger ValA;
  BigInteger ValB;
  BigInteger ValC;
  BigInteger ValD;
  BigInteger ValE;
  BigInteger ValF;
  BigInteger ValH;
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
  BigInteger ValS;
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
  BigInteger Xind;
  BigInteger Yind;
  BigInteger Xlin;
  BigInteger Ylin;
  int nbrFactors;
  bool solFound;
  bool teach = true;
  char also;
  bool ExchXY;
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

  BigInteger currentFactor;
  BigInteger Xplus;
  BigInteger Xminus;
  BigInteger Yplus;
  BigInteger Yminus;

  // At least use std::optional<BigInt>? But I think
  // it can be done with local state at one call site.
  BigInteger *Xbak;
  BigInteger *Ybak;

  int equationNbr;
  int contfracEqNbr;
  const char *ptrVarNameX;
  const char *ptrVarNameY;
  const char *varX;
  const char *varY;
  const char *varXnoTrans;
  const char *varYnoTrans;
  bool firstSolutionX;
  bool positiveDenominator;

  // Were globals NumberLength and TestNbr
  int modulus_length;
  limb TheModulus[MAX_LEN];

  std::string *output = nullptr;

  QuadModLLResult qmllr;

  void MarkUninitialized() {
    // Port note: There are various interleaved code paths where
    // different state (e.g. callback type) results in these member
    // variables being initialized or not. At least set them to
    // valid state so that we can convert them to BigInt (and discard).
    for (BigInteger *b : {
          &Aux0, &Aux1, &Aux2, &Aux3, &Aux5,
          &ValA, &ValB, &ValC, &ValD, &ValE, &ValF,
          &ValH, &ValI, &ValL, &ValM, &ValN, &ValO, &ValP, &ValQ,
          &ValU, &ValV, &ValG, &ValR, &ValS, &ValK, &ValZ,
          &ValAlpha, &ValBeta, &ValDen, &ValDiv,
          &ValABak, &ValBBak, &ValCBak,
          &ValGcdHomog, &Tmp1, &Tmp2,
          &Xind, &Yind, &Xlin, &Ylin, &discr,
          &U1, &U2, &U3, &V1, &V2, &V3,
          &bigTmp, &startPeriodU, &startPeriodV,
          &modulus, &currentFactor, &Xplus, &Xminus, &Yplus, &Yminus}) {
      intToBigInteger(b, 0xCAFE);
    }
  }

  void showText(const char *text) {
    if (output != nullptr)
      *output += text;
  }

  inline void ShowChar(char c) {
    if (output != nullptr)
      output->push_back(c);
  }

  void showLimbs(const limb *limbs, int num_limbs) {
    if (output != nullptr) {
      BigInt b = LimbsToBigInt(limbs, num_limbs);
      *output += b.ToString();
    }
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
    showText("&sup2;");
  }

  void showAlso() {
    if (also && !teach) {
      showText("and also:<br>");
    } else {
      also = 1;
    }
  }

  void startResultBox(eLinearSolution Ret) {
    if (teach && (Ret != NO_SOLUTIONS)) {
      showText("<div class=\"outerbox\"><div class=\"box\">");
    }
  }

  void endResultBox(eLinearSolution Ret) {
    if (teach && (Ret != NO_SOLUTIONS)) {
      showText("</div></div>");
    }
  }

  void showEqNbr(int nbr) {
    showText(" <span class=\"eq\">(");
    showInt(nbr);
    showText(")</span>");
  }

  void ShowLin(const BigInt &coeffX, const BigInt &coeffY,
               const BigInt &coeffInd,
               const char *x, const char *y) {
    eLinearSolution t;
    t = Show(coeffX, x, SOLUTION_FOUND);
    t = Show(coeffY, y, t);
    Show1(coeffInd, t);
  }

  void ShowLinInd(const BigInt &lin, const BigInt &ind,
                  const char *var) {
    if (ind == 0 && lin == 0) {
      showText("0");
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
      showText(var);
    }
  }

  void PrintLinear(eLinearSolution Ret, const char *var) {
    if (Ret == NO_SOLUTIONS) {
      return;
    }
    if ((var == varT) && !teach) {
      showAlso();
    }
    if (Ret == INFINITE_SOLUTIONS) {
      showText("<p>x, y: any integer</p>");
      return;
    }
    if (ExchXY) {
      // Exchange Xind and Yind
      CopyBigInt(&bigTmp, &Xind);
      CopyBigInt(&Xind, &Yind);
      CopyBigInt(&Yind, &bigTmp);
      // Exchange Xlin and Ylin
      CopyBigInt(&bigTmp, &Xlin);
      CopyBigInt(&Xlin, &Ylin);
      CopyBigInt(&Ylin, &bigTmp);
    }
    showText("<p>x = ");
    ShowLinInd(BigIntegerToBigInt(&Xlin),
               BigIntegerToBigInt(&Xind), var);
    showText("<br>y = ");
    ShowLinInd(BigIntegerToBigInt(&Ylin),
               BigIntegerToBigInt(&Yind), var);
    showText("</p>");
    return;
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
      showText(var1);
      showSquare();
    } else if (T2 != 0) {
      // coeffT2 is not zero.
      ShowBigInt(T2);
      ShowChar(' ');
      showText(var1);
      showSquare();
    } else {
      // Nothing to do.
    }

    if (T < 0) {
      showText(" &minus; ");
    } else if (T != 0 && T2 != 0) {
      showText(" + ");
    } else {
      // Nothing to do.
    }

    if (BigInt::Abs(T) == 1) {
      // abs(coeffT) = 1
      showText(var1);
      showText("&#8290;");
      if (var2 != NULL) {
        showText(var2);
      }
      showText(" ");
    } else if (T != 0) {
      // Port note: original called showlimbs if negative, which I
      // think is just a way of printing the absolute value without
      // any copying.
      ShowBigInt(BigInt::Abs(T));
      showText(" ");
      showText(var1);
      if (var2 != NULL) {
        showText("&#8290;");
        showText(var2);
      }
    } else {
      // Nothing to do.
    }

    if (Ind != 0) {
      if (T != 0 || T2 != 0) {
        if (Ind < 0) {
          showText(" &minus; ");
        } else {
          showText(" + ");
        }
      } else if (Ind < 0) {
        showText(" &minus;");
      } else {
        // Nothing to do.
      }

      if (var2 == NULL) {
        // Same trick for abs value.
        ShowBigInt(BigInt::Abs(Ind));
      } else if (BigInt::Abs(Ind) != 1) {
        ShowBigInt(BigInt::Abs(Ind));
        showText("&nbsp;&#8290;");
        showText(var2);
        showSquare();
      } else {
        showText(var2);
        showSquare();
      }
    }
  }

  void ShowSolutionXY(const BigInt &x, const BigInt &y) {
    showText("<p>x = ");
    ShowBigInt(ExchXY ? y : x);
    showText("<BR>y = ");
    ShowBigInt(ExchXY ? x : y);
    showText("</p>");
  }

  // This weird function either shows the solution or continues
  // to try to find the minimum, storing the state in Xbak, Ybak.
  // XXX need to take some kind of state to accumulate Xbak, Ybak...
  void ShowXY(bool two_solutions, const BigInt &X, const BigInt &Y) {
    if (two_solutions) {
      solFound = true;

      // This is basically nullopt state.
      if (Xbak->nbrLimbs == 0) {
        BigIntToBigInteger(X, Xbak);
        BigIntToBigInteger(Y, Ybak);
        return;
      }

      // Use the lowest of |X| + |Y| and |Xbak| + |Ybak|
      BigInt BX = BigIntegerToBigInt(Xbak);
      BigInt BY = BigIntegerToBigInt(Ybak);

      if (BigInt::Abs(X) + BigInt::Abs(Y) <=
          BigInt::Abs(BX) + BigInt::Abs(BY)) {
        // At this moment |x| + |y| <= |xbak| + |ybak|
        BigIntToBigInteger(X, Xbak);
        BigIntToBigInteger(Y, Ybak);
      }

    } else {
      // ONE_SOLUTION: Show it.
      ShowXYOne(X, Y);
    }
  }

  void ShowXYOne(const BigInt &X, const BigInt &Y) {
    CHECK(showSolution == ONE_SOLUTION);
    showAlso();
    ShowSolutionXY(X, Y);
  }

  eLinearSolution Show(const BigInt &num, const string &str,
                       eLinearSolution t) {
    eLinearSolution tOut = t;
    if (num != 0) {
      // num is not zero.
      if (t == NO_SOLUTIONS && num >= 0) {
        showText(" +");
      }

      if (num < 0) {
        showText(" -");
      }

      if (num != 1 && num != -1) {
        // num is not 1 or -1.
        ShowChar(' ');
        ShowBigInt(BigInt::Abs(num));
        showText("&nbsp;&#8290;");
      } else {
        showText("&nbsp;");
      }

      if (output != nullptr)
        *output += str;

      if (t == SOLUTION_FOUND) {
        tOut = NO_SOLUTIONS;
      }
    }
    return tOut;
  }

  void Show1(const BigInt &num, eLinearSolution t) {
    int u = Show(num, "", t);
    ShowChar(' ');
    if ((u & 1) == 0 || num == 1 || num == -1) {
      // Show absolute value of num.
      ShowBigInt(BigInt::Abs(num));
    }
  }

  void ShowEq(
      const BigInt &coeffA, const BigInt &coeffB,
      const BigInt &coeffC, const BigInt &coeffD,
      const BigInt &coeffE, const BigInt &coeffF,
      const char *x, const char *y) {

    eLinearSolution t;
    string vxx = StringPrintf("%s&sup2;", x);
    t = Show(coeffA, vxx, SOLUTION_FOUND);

    string vxy = StringPrintf("%s&#8290;%s", x, y);
    t = Show(coeffB, vxy, t);

    string vyy = StringPrintf("%s&sup2;", y);
    t = Show(coeffC, vyy, t);

    t = Show(coeffD, x, t);

    t = Show(coeffE, y, t);
    Show1(coeffF, t);
  }

  void showFactors(const BigInteger *value, const Factors &factors) {
    int numFactors = factors.product.size();
    bool factorShown = false;
    ShowNumber(value);
    showText(" = ");
    if (value->sign == SIGN_NEGATIVE) {
      showMinus();
    }
    for (int index = 0; index < numFactors; index++) {
      const sFactorz &fact = factors.product[index];
      // XXX why not setting modulus_length here?
      IntArray2BigInteger(modulus_length, fact.array, &qmllr.prime);
      if (fact.multiplicity == 0) {
        continue;
      }
      if (factorShown) {
        showText(" &times; ");
      }
      ShowNumber(&qmllr.prime);
      if (fact.multiplicity != 1) {
        showText("<sup>");
        showInt(fact.multiplicity);
        showText("</sup>");
      }
      factorShown = true;
    }
    if (!factorShown) {
      showText("1");
    }
  }

  void ShowValue(const BigInt &value) {
    if (teach) {
      if (firstSolutionX) {
        showText("<ol>");
        firstSolutionX = false;
      }
      showText("<li><var>T</var> = ");
      ShowBigInt(value);
      showText("</li>");
    }
  }

  void SolutionX(BigInt value, const BigInt &Modulus) {
    SolNbr++;
    BigInt mm = BigIntegerToBigInt(&modulus);
    CHECK(Modulus == mm) <<
      Modulus.ToString() << " vs " << mm.ToString();


    // If 2*value is greater than modulus, subtract modulus.
    // BigInt Modulus = BigIntegerToBigInt(&modulus);
    if ((value << 1) > Modulus) {
      value -= Modulus;
    }

    ShowValue(value);

    switch (callbackQuadModType) {
    case CBACK_QMOD_PARABOLIC:
      callbackQuadModParabolic(value);
      break;
    case CBACK_QMOD_ELLIPTIC: {
      BigInteger tmp;
      BigIntToBigInteger(value, &tmp);
      callbackQuadModElliptic(&tmp);
      break;
    }
    case CBACK_QMOD_HYPERBOLIC: {
      BigInteger tmp;
      BigIntToBigInteger(value, &tmp);
      callbackQuadModHyperbolic(&tmp);
      break;
    }
    default:
      break;
    }
  }

  void NoSolsModPrime(int expon) {
    if (teach) {
      showText("<p>There are no solutions modulo ");
      ShowNumber(&qmllr.prime);
      if (expon != 1) {
        if (output != nullptr)
          StringAppendF(output, "<sup>%d</sup>", expon);
      }
      showText(", so the modular equation does not have any solution.</p>");
    }
  }

  void ShowSolutionsModPrime(int factorIndex, int expon,
                             const BigInteger *pIncrement,
                             const QuadModLLResult *qmllr) {
    bool last;
    BigInteger primePower;
    if (!teach) {
      return;
    }

    // Get value of prime power.
    (void)BigIntPowerIntExp(&qmllr->prime, expon, &primePower);
    intToBigInteger(&ValH, 0);
    showText("<p>Solutions modulo ");
    ShowNumber(&qmllr->prime);
    if (expon != 1) {
      if (output != nullptr)
        StringAppendF(output, "<sup>%d</sup>", expon);
    }
    showText(": ");
    do {

      fprintf(stderr, "sol1[%d] (%d,%08x) sol2[%d] (%d,%08x)\n",
              factorIndex,
              qmllr->Solution1[factorIndex].nbrLimbs,
              qmllr->Solution1[factorIndex].limbs[0].x,
              factorIndex,
              qmllr->Solution2[factorIndex].nbrLimbs,
              qmllr->Solution2[factorIndex].limbs[0].x);

      bool oneSolution = BigIntEqual(&qmllr->Solution1[factorIndex],
                                     &qmllr->Solution2[factorIndex]);
      BigIntAdd(&ValH, pIncrement, &ValI);   // Next value.
      last = BigIntEqual(&primePower, &ValI);
      if (!BigIntIsZero(&ValH)) {
        if (last && oneSolution) {
          showText(" and ");
        } else {
          showText(", ");
        }
      }
      BigInteger SolJ;
      BigIntAdd(&ValH, &qmllr->Solution1[factorIndex], &SolJ);
      ShowNumber(&SolJ);
      if (!oneSolution) {
        if (last) {
          showText(" and ");
        } else {
          showText(", ");
        }
        BigIntAdd(&ValH, &qmllr->Solution2[factorIndex], &SolJ);
        ShowNumber(&SolJ);
      }
      CopyBigInt(&ValH, &ValI);
    } while (!last);
    showText("</p>");
  }

  // Solve congruence an^2 + bn + c = 0 (mod n) where n is different from zero.
  void SolveQuadModEquation(const BigInt &coeffQuadr,
                            const BigInt &coeffLinear,
                            const BigInt &coeffIndep,
                            const BigInt &modulus_in) {

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
        showText("<p>All values of <var>x</var> between 0 and ");

        // XXX Suspicious that this modifies GcdAll in place (I
        // think just to display it?) but uses it again below.
        GcdAll -= 1;
        ShowBigInt(GcdAll);
        showText(" are solutions.</p>");
      } else {
        // must succeed; is < 5 and non-negative
        const int n = GcdAll.ToInt().value();
        for (int ctr = 0; ctr < n; ctr++) {
          fprintf(stderr, "X %d\n", __LINE__);
          SolutionX(BigInt(ctr), modulus);
        }

        if (teach && !firstSolutionX) {
          showText("</ol>");
        }
      }
      return;
    }

    // PERF divisibility check
    if (BigInt::CMod(coeff_quadr, modulus) == 0) {
      // Linear equation.
      if (BigInt::GCD(coeff_linear, modulus) != 1) {
        // ValB and ValN are not coprime. Go out.
        return;
      }

      // Calculate z <- -ValC / ValB (mod ValN)
      /*
        BigInteger big_modulus;
        BigIntToBigInteger(&TheModulus, modulus);
        modulus_length = modulus.nbrLimbs;
        int lenBytes = modulus_length * (int)sizeof(limb);
        (void)memcpy(TheModulus, modulus.limbs, lenBytes);
      */

      modulus_length = BigIntToLimbs(modulus, TheModulus);
      TheModulus[modulus_length].x = 0;

      MontgomeryParams params = GetMontgomeryParams(modulus_length, TheModulus);

      BigInt z;
      {
        // Yuck
        BigInteger ind, lin, modu, zz;
        BigIntToBigInteger(coeff_indep, &ind);
        BigIntToBigInteger(coeff_linear, &lin);
        BigIntToBigInteger(modulus, &modu);

        BigIntModularDivision(params, modulus_length, TheModulus,
                              &ind, &lin, &modu, &zz);
        z = BigIntegerToBigInt(&zz);
      }

      if (z != 0) {
        // not covered by cov.sh :(
        fprintf(stderr, "z != 0\n");
        // XXX is this a typo for ValNn in the original?
        // ValN is only set in CheckSolutionSquareDiscr.
        // Since it was static, it would usually be zero here.
        // z = ValN - z;
        z = 0 - z;
      }
      BigInt Temp0 = ValNn * GcdAll;

      {
        for (;;) {
          // also not covered :(
          fprintf(stderr, "loop zz");
          SolutionX(z, modulus);
          z += modulus;
          if (z < Temp0) break;
        }
      }

      if (teach && !firstSolutionX) {
        showText("</ol>");
      }
      return;
    }

    if (callbackQuadModType == CBACK_QMOD_PARABOLIC) {
      // For elliptic case, the factorization is already done.
      if (teach) {
        BigInteger modu;
        BigIntToBigInteger(modulus, &modu);
        std::unique_ptr<Factors> factors = BigFactor(&modu);

        showText("<p>To solve this quadratic modular equation we have to "
                 "factor the modulus and find the solution modulo the powers "
                 "of the prime factors. Then we combine them by using the "
                 "Chinese Remainder Theorem.</p>");
        showFactors(&modu, *factors);
      }
    }

    {
      BigInteger quad, lin, ind, modu, gcd, valnn;
      BigIntToBigInteger(coeff_quadr, &quad);
      BigIntToBigInteger(coeff_linear, &lin);
      BigIntToBigInteger(coeff_indep, &ind);
      BigIntToBigInteger(modulus, &modu);
      BigIntToBigInteger(GcdAll, &gcd);
      BigIntToBigInteger(ValNn, &valnn);

      // XXX two different moduli here
      // CHECK(modulus == BigIntegerToBigInt(&this->modulus));

      SolveEquation(
          SolutionFn([this](BigInteger *value) {
              this->SolutionX(BigIntegerToBigInt(value),
                              BigIntegerToBigInt(&this->modulus));
            }),
          ShowSolutionsModPrimeFn(
              [this](int factorIndex, int expon,
                     const BigInteger *pIncrement,
                     const QuadModLLResult *qmllr) {
                fprintf(stderr,
                        "ssmp %d %d %s\n",
                        factorIndex, expon,
                        BigIntegerToBigInt(pIncrement).ToString().c_str());
                this->ShowSolutionsModPrime(factorIndex, expon, pIncrement,
                                            qmllr);
              }),
          ShowNoSolsModPrimeFn([this](int expon) {
              this->NoSolsModPrime(expon);
            }),
          &quad, &lin, &ind, &modu,
          &gcd, &valnn,
          &qmllr);
    }

    if (teach && !firstSolutionX) {
      showText("</ol>");
    }
  }

  void ParenA(const BigInteger *num) {
    if (num->sign == SIGN_NEGATIVE) {
      ShowChar('(');
      ShowNumber(num);
      ShowChar(')');
    } else {
      ShowNumber(num);
    }
  }

  void Paren(const BigInt &num) {
    if (num < 0) {
      ShowChar('(');
      ShowBigInt(num);
      ShowChar(')');
    } else {
      ShowBigInt(num);
    }
  }

  eLinearSolution LinearEq(
      BigInt coeffX, BigInt coeffY, BigInt coeffInd) {

    if (teach) {
      showText("<p>This is a linear equation ");
      ShowLin(coeffX, coeffY, coeffInd, "x", "y");
      showText(" = 0</p>");
    }

    if (coeffX == 0) {
      if (coeffY == 0) {
        if (coeffInd != 0) {
          return NO_SOLUTIONS;           // No solutions
        } else {
          return INFINITE_SOLUTIONS;     // Infinite number of solutions
        }
      }

      BigInt Aux0 = BigInt::CMod(coeffInd, coeffY);
      if (Aux0 != 0) {
        return NO_SOLUTIONS;             // No solutions
      } else {
        intToBigInteger(&Xind, 0);
        intToBigInteger(&Xlin, 1);
        // PERF QuotRem
        BigInt yy = coeffInd / coeffY;
        BigIntToBigInteger(yy, &Yind);
        BigIntNegate(&Yind, &Yind);
        intToBigInteger(&Ylin, 0);
        return SOLUTION_FOUND;           // Solution found
      }
    }

    if (coeffY == 0) {

      const auto [qq, rr] = BigInt::QuotRem(coeffInd, coeffX);

      if (rr != 0) {
        return NO_SOLUTIONS;             // No solutions
      } else {
        intToBigInteger(&Yind, 0);
        intToBigInteger(&Ylin, 1);
        BigIntToBigInteger(qq, &Xind);
        BigIntNegate(&Xind, &Xind);
        intToBigInteger(&Xlin, 0);
        return SOLUTION_FOUND;           // Solution found
      }
    }

    BigInt gcdxy = BigInt::GCD(coeffX, coeffY);

    if (gcdxy != 1) {
      // GCD is not 1.
      if (teach) {
        showText(
            "<p>To solve it, we first find the greatest common divisor of the "
            "linear coefficients, that is: gcd(");
        ShowBigInt(coeffX);
        showText(", ");
        ShowBigInt(coeffY);
        showText(") = ");
        ShowBigInt(gcdxy);
        showText(".</p>");
      }

      // PERF divisibility test
      BigInt r = BigInt::CMod(coeffInd, gcdxy);

      if (r != 0) {
        if (teach) {
          showText("<p>The independent coefficient is not multiple of "
                   "gcd, so there are no solutions.</p>");
        }
        return NO_SOLUTIONS;            // No solutions
      }

      // Divide all coefficients by the gcd.
      if (gcdxy != 0) {
        // PERF known divisible
        coeffX /= gcdxy;
        coeffY /= gcdxy;
        coeffInd /= gcdxy;
      }
    }

    if (teach) {
      showText(divgcd);
      ShowLin(coeffX, coeffY, coeffInd, "x", "y");
      showText(" = 0</p>");
      showText("<p>Now we must apply the Generalized Euclidean algorithm:</p>");
    }

    BigInt U1(1);
    BigInt U2(0);
    BigInt U3 = coeffX;
    BigInt V1(0);
    BigInt V2(1);
    BigInt V3 = coeffY;

    // PERF this is just a display heuristic. base this on bounds instead
    bool showSteps = true;
    {
      BigInteger coeff;
      BigIntToBigInteger(coeffX, &coeff);
      if (coeff.nbrLimbs != 1) showSteps = false;
      BigIntToBigInteger(coeffY, &coeff);
      if (coeff.nbrLimbs != 1) showSteps = false;
    }

    int stepNbr = 1;

    if (teach && showSteps) {
      showText("<p>");
    }

    BigInt q;

    while (V3 != 0) {

      if (VERBOSE)
      printf("Step %d: %s %s %s %s %s %s\n",
             stepNbr,
             U1.ToString().c_str(),
             U2.ToString().c_str(),
             U3.ToString().c_str(),
             V1.ToString().c_str(),
             V2.ToString().c_str(),
             V3.ToString().c_str());

      if (teach && showSteps) {
        showText("Step ");
        showInt(stepNbr);
        showText(": ");
        Paren(U1);
        showText(" &times; ");
        Paren(coeffX);
        showText(" + ");
        Paren(U2);    // U2
        showText(" &times; ");
        Paren(coeffY);
        showText(" = ");
        Paren(U3);
        showText("<br>");
      }

      // q <- floor(U3 / V3).
      q = FloorDiv(U3, V3);

      {
        // T <- U1 - q * V1
        BigInt T = U1 - q * V1;
        U1 = V1;
        V1 = T;
      }

      {
        BigInt T = U2 - q * V2;
        U2 = V2;
        V2 = T;
      }

      {
        BigInt T = U3 - q * V3;
        U3 = V3;
        V3 = T;
      }

      stepNbr++;
    }

    CHECK(U3 != 0);
    // Compute q as -coeffInd / U3
    q = -coeffInd / U3;


    // XXX remove from state: Xind, Xlin, Yind, Ylin
    // Can probably just move this to the end!

    // Compute Xind as -U1 * coeffInd / U3
    BigInt xind = U1 * q;
    BigIntToBigInteger(xind, &Xind);
    // Set Xlin to coeffY
    BigInt xlin = coeffY;
    BigIntToBigInteger(xlin, &Xlin);

    // Compute Yind as -U2 * coeffInd / U3
    BigInt yind = U2 * q;
    BigIntToBigInteger(yind, &Yind);
    // Set Ylin to -coeffX
    BigInt ylin = -coeffX;
    BigIntToBigInteger(ylin, &Ylin);

    if (teach) {
      showText("Step ");
      showInt(stepNbr);
      showText(": ");
      Paren(U1);
      showText(" &times; ");
      Paren(coeffX);
      showText(" + ");
      Paren(U2);
      showText(" &times; ");
      Paren(coeffY);
      showText(" = ");
      Paren(U3);    // U3

      if (q != 1) {
        // Multiplier is not 1.
        showText("</p><p>Multiplying the last equation by ");
        Paren(q);
        showText(" we obtain:<br>");
        ParenA(&Xind);
        showText(" &times; ");
        Paren(coeffX);
        showText(" + ");
        ParenA(&Yind);
        showText(" &times; ");
        Paren(coeffY);
        showText(" = ");
        ShowBigInt(-coeffInd);
      }

      showText("</p><p>Adding and subtracting ");
      Paren(coeffX);
      showText(" &times; ");
      Paren(coeffY);
      showText(" t' ");
      showText("we obtain");
      showText(":</p><p>(");
      ShowNumber(&Xind);
      showText(" + ");
      Paren(coeffY);
      showText(" t') &times; ");
      Paren(coeffX);
      showText(" + (");
      ShowNumber(&Yind);
      showText(" - ");
      Paren(coeffX);
      showText(" t') &times; ");
      Paren(coeffY);
      showText(" = ");
      ShowBigInt(-coeffInd);
      showText("</p><p>So, the solution is given by the set:</p>");
      PrintLinear(SOLUTION_FOUND, "t'");
      showText("</p>");
    }

    // HERE!

    if (VERBOSE)
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

    if (VERBOSE)
    printf("Step: %s %s %s %s %s %s\n",
           U1.ToString().c_str(),
           U2.ToString().c_str(),
           U3.ToString().c_str(),
           V1.ToString().c_str(),
           V2.ToString().c_str(),
           V3.ToString().c_str());

    if (teach) {
      showText("<p>By making the substitution ");
      showText("<var>t'</var> = ");
      ShowBigInt(U1);
      if (xlin < 0 && ylin < 0) {
        // If both coefficients are negative, change sign of transformation.
        showText(" &minus;");
      } else {
        showText(" +");
      }
      showText(" <var>t</var> "
               "we finally obtain:</p>");
    }

    // Xind <- Xind + coeffY * delta
    q = U1 * coeffY;
    xind += q;
    BigIntToBigInteger(xind, &Xind);

    // Yind <- Yind - coeffX * delta
    q = U1 * coeffX;
    yind -= q;
    BigIntToBigInteger(yind, &Yind);


    if (xlin < 0 && ylin < 0) {
      // If both coefficients are negative, make them positive.
      BigIntChSign(&Xlin);
      BigIntChSign(&Ylin);
    }

    return SOLUTION_FOUND;
  }

  void ShowTDiscrZero(bool ExchXY,
                      const BigInt &A, const BigInt &B) {
    BigInt H, I;
    if (ExchXY) {
      // Show bx + 2cy
      H = B;
      I = A << 1;
    } else {
      // Show 2ax + by
      I = B;
      H = A << 1;
    }
    // XXX remove state
    // intToBigInteger(&ValJ, 0);
    ShowLin(H, I, BigInt(0), "<var>x</var>", "<var>y</var>");
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


    // XXX remove this state
    BigIntToBigInteger(A, &ValA);
    BigIntToBigInteger(B, &ValB);
    BigIntToBigInteger(C, &ValC);
    BigIntToBigInteger(D, &ValD);
    BigIntToBigInteger(E, &ValE);
    BigIntToBigInteger(F, &ValF);
    // discriminant should be known zero on this code path.
    // BigIntToBigInteger(Discr, &discr);

    // ax^2 + bxy + cx^2 + dx + ey + f = 0 (1)
    // Multiplying by 4a:
    // (2ax + by)^2 + 4adx + 4aey + 4af = 0
    // Let t = 2ax + by. So (1) becomes: (t + d)^2 = uy + v.
    // Compute u <- 2(bd - 2ae)
    BigInteger TeachJ;
    if (teach) {
      showText("<p>Multiplying by");
      showText(ExchXY ? " 4&#8290;<var>c</var>" : " 4&#8290;<var>a</var>");
      showText("</p><p>(");
      ShowTDiscrZero(ExchXY, A, B);
      showText(")");
      showSquare();
      if (ExchXY) {
        (void)BigIntMultiply(&ValA, &ValE, &ValH);
        (void)BigIntMultiply(&ValA, &ValD, &ValI);
      } else {
        (void)BigIntMultiply(&ValA, &ValD, &ValH);
        (void)BigIntMultiply(&ValA, &ValE, &ValI);
      }
      (void)BigIntMultiply(&ValA, &ValF, &TeachJ);
      MultInt(&ValH, &ValH, 4);
      MultInt(&ValI, &ValI, 4);
      MultInt(&TeachJ, &TeachJ, 4);
      if (BigIntIsZero(&ValH)) {
        if (BigIntIsZero(&ValI)) {
          if (TeachJ.sign == SIGN_POSITIVE) {
            showText(" + ");
          }
        } else if (ValI.sign == SIGN_POSITIVE) {
          showText(" + ");
        }
      } else if (ValH.sign == SIGN_POSITIVE) {
        showText(" + ");
      } else {
        // Nothing to do.
      }
      ShowLin(BigIntegerToBigInt(&ValH),
              BigIntegerToBigInt(&ValI),
              BigIntegerToBigInt(&TeachJ), "<var>x</var>", "<var>y</var>");
      showText(" = 0</p><p>");
      showText("Let");
      showText(" <var>t</var> = ");
      ShowTDiscrZero(ExchXY, A, B);
      intToBigInteger(&TeachJ, 0);
      intToBigInteger(&ValH, 1);
      showEqNbr(1);
      showText("</p><p>(<var>t</var> + <var>");
      showText(ExchXY?"e</var>)": "d</var>)");
      showSquare();
      showText(" = (");
      ShowLin(BigIntegerToBigInt(&TeachJ),
              BigIntegerToBigInt(&ValH),
              BigIntegerToBigInt(&ValD), "", "<var>t</var>");
      showText(")");
      showSquare();
      showText(" = ");
    }

    (void)BigIntMultiply(&ValB, &ValD, &Aux0);
    (void)BigIntMultiply(&ValA, &ValE, &Aux1);
    MultInt(&Aux2, &Aux1, 2);
    BigIntSubt(&Aux0, &Aux2, &Aux1);
    MultInt(&ValU, &Aux1, 2);

    // Compute v <- d^2 - 4af
    (void)BigIntMultiply(&ValD, &ValD, &Aux2);
    (void)BigIntMultiply(&ValA, &ValF, &Aux1);
    MultInt(&Aux3, &Aux1, 4);
    BigIntSubt(&Aux2, &Aux3, &ValV);

    if (teach) {
      if (ExchXY) {
        ptrVarNameX = "<var>y</var>";
        ptrVarNameY = "<var>x</var>";
      } else {
        ptrVarNameX = "<var>x</var>";
        ptrVarNameY = "<var>y</var>";
      }
      ShowLin(BigIntegerToBigInt(&TeachJ),
              BigIntegerToBigInt(&ValU),
              BigIntegerToBigInt(&ValV),
              ptrVarNameX, ptrVarNameY);
      if (!BigIntIsZero(&ValU) && !BigIntIsZero(&ValV)) {
        showEqNbr(2);
      }
      showText("</p><p>"
               "where the linear coefficient is ");
      showText(ExchXY ? "2&#8290;(<var>b</var>&#8290;<var>e</var> "
               "&minus; 2&#8290;<var>c</var><var>d</var>)" :
               "2&#8290;(<var>b</var>&#8290;<var>d</var> "
               "&minus; 2&#8290;<var>a</var><var>e</var>)");
      showText(" and the constant coefficient is ");
      showText(ExchXY ?
               "<var>e</var>&sup2; &minus; "
               "4&#8290;<var>c</var><var>f</var>.</p>" :
               "<var>d</var>&sup2; &minus; "
               "4&#8290;<var>a</var><var>f</var>.</p>");
    }

    if (BigIntIsZero(&ValU)) {
      // u equals zero, so (t+d)^2 = v.
      eLinearSolution ret;
      if (ValV.sign == SIGN_NEGATIVE) {
        // There are no solutions when v is negative.
        if (teach) {
          showText("A square cannot be equal to a negative number, "
                   "so there are no solutions.</p>");
        }
        return;
      }

      if (BigIntIsZero(&ValV)) {
        // v equals zero, so (1) becomes 2ax + by + d = 0
        MultInt(&Aux3, &ValA, 2);
        ret = LinearEq(BigIntegerToBigInt(&Aux3),
                       BigIntegerToBigInt(&ValB),
                       BigIntegerToBigInt(&ValD));
        startResultBox(ret);
        PrintLinear(ret, "<var>t</var>");
        endResultBox(ret);
        return;
      }

      // u equals zero but v does not.
      // v must be a perfect square, otherwise there are no solutions.
      SquareRoot(ValV.limbs, ValG.limbs, ValV.nbrLimbs, &ValG.nbrLimbs);
      ValG.sign = SIGN_POSITIVE;          // g <- sqrt(v).
      (void)BigIntMultiply(&ValG, &ValG, &Aux3);
      if (!BigIntEqual(&ValV, &Aux3)) {
        // v is not perfect square.
        if (teach) {
          showText(
              "The right hand side is not a perfect square, "
              "so there are no solutions.</p>");
        }
        return;
      }
      // The original equation is now: 2ax + by + (d +/- g) = 0
      MultInt(&Aux3, &ValA, 2);
      BigInteger Aux4;
      BigIntAdd(&ValD, &ValG, &Aux4);
      CopyBigInt(&Aux5, &ValB);

      if (teach) {
        showText("<p>");
        ShowLin(BigIntegerToBigInt(&TeachJ),
                BigIntegerToBigInt(&ValH),
                BigIntegerToBigInt(&ValD), "", "<var>t</var>");
        showText(" = &pm;");
        ShowNumber(&ValG);
        showText("</p><p>This equation represents two parallel lines. "
                 "The first line is: </p><p>");
        if (ExchXY) {
          ShowLin(BigIntegerToBigInt(&Aux5),
                  BigIntegerToBigInt(&Aux3),
                  BigIntegerToBigInt(&Aux4), "<var>x</var>", "<var>y</var>");
        } else {
          ShowLin(BigIntegerToBigInt(&Aux3),
                  BigIntegerToBigInt(&Aux5),
                  BigIntegerToBigInt(&Aux4), "<var>x</var>", "<var>y</var>");
        }
        showText(" = 0</p>");
      }
      ret = LinearEq(BigIntegerToBigInt(&Aux3),
                     BigIntegerToBigInt(&Aux5),
                     BigIntegerToBigInt(&Aux4));
      startResultBox(ret);
      PrintLinear(ret, "<var>t</var>");
      endResultBox(ret);
      MultInt(&Aux3, &ValA, 2);
      BigIntSubt(&ValD, &ValG, &Aux4);
      CopyBigInt(&Aux5, &ValB);
      if (teach) {
        showText("<p>The second line is:</p><p>");
        if (ExchXY) {
          ShowLin(BigIntegerToBigInt(&Aux5),
                  BigIntegerToBigInt(&Aux3),
                  BigIntegerToBigInt(&Aux4), "<var>x</var>", "<var>y</var>");
        } else {
          ShowLin(BigIntegerToBigInt(&Aux3),
                  BigIntegerToBigInt(&Aux5),
                  BigIntegerToBigInt(&Aux4), "<var>x</var>", "<var>y</var>");
        }
        showText(" = 0</p>");
      }
      ret = LinearEq(BigIntegerToBigInt(&Aux3),
                     BigIntegerToBigInt(&Aux5),
                     BigIntegerToBigInt(&Aux4));
      startResultBox(ret);
      PrintLinear(ret, "<var>t</var>");
      endResultBox(ret);
      return;
    }
    // At this moment u does not equal zero.
    // We have to solve the congruence
    //     T^2 = v (mod u) where T = t+d and t = 2ax+by.
    if (teach) {
      showText("<p>We have to solve");
      showText(" <var>T</var>");
      showSquare();
      showText(" = ");
      ShowNumber(&ValV);
      CopyBigInt(&ValH, &ValU);
      ValH.sign = SIGN_POSITIVE;
      showText(" (mod ");
      ShowNumber(&ValH);
      showText(")</p>");
    }

    BigInt V = BigIntegerToBigInt(&ValV);
    BigInt Modulus = BigIntegerToBigInt(&ValU);
    // XXX eliminate this state
    CopyBigInt(&modulus, &ValU);

    callbackQuadModType = CBACK_QMOD_PARABOLIC;
    equationNbr = 3;

    SolveQuadModEquation(
        // PERF just construct directly above.
        BigInt(1),
        BigInt(0),
        -V,
        Modulus);
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

  void callbackQuadModParabolic(const BigInt &Value) {
    // The argument of this function is T. t = T - d + uk (k arbitrary).
    // Compute ValR <- (T^2 - v)/u
    BigInt A = BigIntegerToBigInt(&ValA);
    BigInt B = BigIntegerToBigInt(&ValB);
    BigInt C = BigIntegerToBigInt(&ValC);
    BigInt D = BigIntegerToBigInt(&ValD);
    BigInt E = BigIntegerToBigInt(&ValE);
    BigInt U = BigIntegerToBigInt(&ValU);

    if (teach) {
      showText("<p><var>t</var> = <var>T</var> &minus; <var>");
      showText(ExchXY ? "e" : "d");
      showText("</var> ");

      if (U >= 0) {
        showText("+ ");
      } else {
        showText("&minus; ");
      }

      if (BigInt::Abs(U) != 1) {
        // Absolute value of U is not 1.
        ShowBigInt(BigInt::Abs(U));
      }

      showText(" &#8290<var>k</var> = ");
      ShowLinInd(U, Value - D, "<var>k</var>");
      showEqNbr(equationNbr);
      showText(" (");
      showText("where <var>k</var> is any integer).</p>");
    }


    BigInt V = BigIntegerToBigInt(&ValV);
    BigInt I = BigIntegerToBigInt(&ValI);

    BigInt R = ((Value * Value) - V) / U;
    // Compute ValS as 2*T
    BigInt S = Value << 1;

    if (teach) {
      showText("<p>Replacing <var>t</var> in equation");
      showEqNbr(2);
      showText(" we can get the value of ");
      showText(ptrVarNameY);
      showText(":</p><p>");
      showText(ptrVarNameY);
      showText(" = ");
      PrintQuad(U, S, R,
                "<var>k</var>", NULL);
      showEqNbr(equationNbr+1);
      showText("</p><p>"
               "From");
      showEqNbr(1);
      showText(" and");
      showEqNbr(equationNbr);
      showText(":</p><p>");
      ShowTDiscrZero(ExchXY, A, B);
      showText(" = ");
      BigInt H = Value - D;
      ShowLinInd(U, H, "<var>k</var>");
      showText("</p>");
      BigInt K = A << 1;

      if (B != 0) {
        showText("<p>Using");
        showEqNbr(equationNbr + 1);
        showText(" to substitute ");
        showText(ptrVarNameY);
        showText(":</p><p>");
        ShowLinInd(K, BigInt(0), ptrVarNameX);
        showText(" = ");
        // Quadratic coeff = -bU.
        BigInt J = -B * U;
        BigInt BigTmp = B * S;
        // Linear coeff = U - bS.
        I = U - BigTmp;
        BigTmp = B * R;
        // Independent coeff = H - bR.
        H -= BigTmp;
        PrintQuad(J, I, H, "<var>k</var>", nullptr);
        showText("</p>");
      }

      // if independent coefficient is not multiple of GCD(I, K) then show that
      // there are no solutions. Note that J is always multiple of K.
      BigInt J = I * K;
      CHECK(J != 0);
      if (H % J != 0) {
        showText("<p>The independent coefficient ");
        ShowBigInt(H);
        showText(" is not multiple of ");
        ShowBigInt(J);
        showText(", which is the greatest common divisor of the other "
                 "three coefficients, so there is no solution.</p>");
      }
    }

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
        ExchXY ?
        ComputeYDiscrZero(U, U2, S, R, Z) :
        ComputeXDiscrZero(A, B, C, D, E, Z, J, K, U2);

      showAlso();
      startResultBox(SOLUTION_FOUND);
      showText("<p><var>x</var> = ");
      PrintQuad(VV3, VV2, VV1,
                "<var>k</var>", NULL);
      showText("<br>");
    }

    {
      const auto &[VV1, VV2, VV3] =
        ExchXY ?
        ComputeXDiscrZero(A, B, C, D, E, Z, J, K, U2) :
        ComputeYDiscrZero(U, U2, S, R, Z);

      showText("<var>y</var> = ");
      PrintQuad(VV3, VV2, VV1,
                "<var>k</var>", NULL);
    }

    if (teach) {
      showText("<br>");
    }
    showText("</p>");
    endResultBox(SOLUTION_FOUND);

    if (teach) {
      showText("<br>");
      equationNbr += 2;
    }
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

    int solution = 0;
    if (teach) {
      showText("<p><var>X</var> = ");
      ShowBigInt(X);
      showText(", <var>Y</var> = ");
      ShowBigInt(Y);
      showText("</p>");
    }

    if (VERBOSE)
    printf("ShowPoint %s %s %s %s %s\n",
           X.ToString().c_str(),
           Y.ToString().c_str(),
           Alpha.ToString().c_str(),
           Beta.ToString().c_str(),
           Div.ToString().c_str());

    // Check first that (X+alpha) and (Y+beta) are multiple of D.
    BigInt tmp1 = X + Alpha;
    BigInt tmp2 = Y + Beta;
    if (teach && !(Alpha == 0 && Beta == 0)) {
      showText("<p><var>X</var> + <var>&alpha;</var> = ");
      ShowBigInt(tmp1);
      showText(", <var>Y</var> + <var>&beta;</var> = ");
      ShowBigInt(tmp2);
      showText("</p>");
    }

    CHECK(Div != 0) << "Might be shenanigans with divisibility by zero";

    // PERF divisibility tests.
    if (tmp1 % Div == 0 &&
        tmp2 % Div == 0) {

      if (teach && Div != 1) {
        showText("<p>Dividing these numbers by");
        showText(" <var>D</var> = ");
        ShowBigInt(Div);
        showText(":</p>");
      }

      // PERF known divisible
      if (Div != 0) {
        tmp1 /= Div;
        tmp2 /= Div;
      }

      // XXX is two_solutions statically known here?
      if (callbackQuadModType == CBACK_QMOD_HYPERBOLIC) {
        if (teach) {
          ShowSolutionXY(tmp1, tmp2);
        }
        ShowXY(two_solutions, tmp1, tmp2);
      } else {
        startResultBox(SOLUTION_FOUND);
        ShowXY(two_solutions, tmp1, tmp2);
        endResultBox(SOLUTION_FOUND);
      }
      solution = 1;
      showRecursiveSolution = 1; // Show recursive solution if it exists.
    }

    if (teach && solution == 0) {
      showText("<p>These numbers are not multiple of"
               " <var>D</var> = ");
      ShowBigInt(Div);
      showText(".</p>");
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
  void NonSquareDiscriminant() {
    // Find GCD(a,b,c)
    BigIntGcd(&ValA, &ValB, &bigTmp);
    BigIntGcd(&ValC, &bigTmp, &ValGcdHomog);
    // Divide A, B, C and K by this GCD.
    (void)BigIntDivide(&ValA, &ValGcdHomog, &ValA);
    (void)BigIntDivide(&ValB, &ValGcdHomog, &ValB);
    (void)BigIntDivide(&ValC, &ValGcdHomog, &ValC);
    (void)BigIntDivide(&ValK, &ValGcdHomog, &ValK);
    // Divide discriminant by the square of GCD.
    (void)BigIntDivide(&discr, &ValGcdHomog, &discr);
    (void)BigIntDivide(&discr, &ValGcdHomog, &discr);
    if (BigIntIsZero(&ValK)) {
      // If k=0, the only solution is (X, Y) = (0, 0)
      ShowPoint(false,
                BigIntegerToBigInt(&ValK),
                BigIntegerToBigInt(&ValK),
                BigIntegerToBigInt(&ValAlpha),
                BigIntegerToBigInt(&ValBeta),
                BigIntegerToBigInt(&ValDiv));
      return;
    }
    CopyBigInt(&ValABak, &ValA);
    CopyBigInt(&ValBBak, &ValB);
    CopyBigInt(&ValCBak, &ValC);
    // Factor independent term.
    eSign ValKSignBak = ValK.sign;
    ValK.sign = SIGN_POSITIVE;

    std::unique_ptr<Factors> factors = BigFactor(&ValK);
    ValK.sign = ValKSignBak;
    // Find all indexes of prime factors with even multiplicity.
    // (XXX parallel. could be pair)
    // Index of prime factors with even multiplicity
    // PORT NOTE: was 1-based in original code; now 0-based
    std::vector<int> indexEvenMultiplicity, originalMultiplicities;
    int numFactors = factors->product.size();
    for (int i = 0; i < numFactors; i++) {
      const sFactorz &fact = factors->product[i];
      if (fact.multiplicity > 1) {
        // At least prime is squared.
        // Port note: The original code stored factorNbr, which was 1-based because
        // of the factor header.
        indexEvenMultiplicity.push_back(i);
        // Convert to even.
        originalMultiplicities.push_back(fact.multiplicity & ~1);
      }
    }
    std::vector<int> counters(400, 0);
    (void)memset(isDescending, 0, sizeof(isDescending));
    intToBigInteger(&ValE, 1);  // Initialize multiplier to 1.
    // Loop that cycles through all square divisors of the independent term.
    equationNbr = 2;
    BigIntGcd(&ValA, &ValK, &bigTmp);
    intToBigInteger(&ValM, 0);
    varXnoTrans = "<var>X</var>";
    varYnoTrans = "<var>Y</var>";
    if ((bigTmp.nbrLimbs != 1) || (bigTmp.limbs[0].x != 1)) {
      // gcd(a, K) is not equal to 1.
      if (teach) {
        showText("<p>The algorithm requires that the coefficient of <var>X</var>");
        showSquare();
        showText(" and the right hand side are coprime. This does not happen, "
                 "so we have to find a value of <var>m</var> such that applying "
                 "one of the unimodular transformations</p> ");
        showText("<ul><li><var>X</var> = <var>m</var>&#8290;<var>U</var> + "
                 "<var>(m&minus;1)</var>&#8290;<var>V</var>, ");
        showText("<var>Y</var> = <var>U</var> + <var>V</var></li>");
        showText("<li><var>X</var> = <var>U</var> + <var>V</var>, ");
        showText("<var>Y</var> = <var>(m&minus;1)</var>&#8290;<var>U</var> + "
                 "<var>m</var>&#8290;<var>V</var></li></ul>");
        showText("<p>the coefficient of <var>U</var>");
        showSquare();
        showText(" and the right hand side are coprime. This coefficient equals ");
        PrintQuad(BigIntegerToBigInt(&ValA),
                  BigIntegerToBigInt(&ValB),
                  BigIntegerToBigInt(&ValC), "<var>m</var>", NULL);
        showText(" in the first case and ");
        PrintQuad(BigIntegerToBigInt(&ValC),
                  BigIntegerToBigInt(&ValB),
                  BigIntegerToBigInt(&ValA),
                  "(<var>m</var> &minus; 1)", NULL);
        showText(" in the second case.</p>");
      }

      intToBigInteger(&ValM, 0);

      do {
        // Compute U1 = cm^2 + bm + a and exit loop if this
        // value is not coprime to K.
        (void)BigIntMultiply(&ValC, &ValM, &U2);
        BigIntAdd(&U2, &ValB, &U1);
        (void)BigIntMultiply(&U1, &ValM, &U1);
        BigIntAdd(&U1, &ValA, &U1);
        BigIntGcd(&U1, &ValK, &bigTmp);
        if ((bigTmp.nbrLimbs == 1) && (bigTmp.limbs[0].x == 1)) {
          addbigint(&ValM, 1);  // Increment M.
          BigIntChSign(&ValM);  // Change sign to indicate type.
          break;
        }
        addbigint(&ValM, 1);    // Increment M.
        // Compute U1 = am^2 + bm + c and loop while this
        // value is not coprime to K.
        (void)BigIntMultiply(&ValA, &ValM, &U2);
        BigIntAdd(&U2, &ValB, &U1);
        (void)BigIntMultiply(&U1, &ValM, &U1);
        BigIntAdd(&U1, &ValC, &U1);
        BigIntGcd(&U1, &ValK, &bigTmp);
      } while ((bigTmp.nbrLimbs != 1) || (bigTmp.limbs[0].x != 1));
      // Compute 2am + b or 2cm + b as required.
      BigIntAdd(&U2, &U2, &U2);
      BigIntAdd(&U2, &ValB, &U2);
      if (ValM.sign == SIGN_POSITIVE) {
        // Compute c.
        BigIntSubt(&U1, &U2, &ValB);
        BigIntAdd(&ValB, &ValA, &ValC);
        // Compute b.
        BigIntAdd(&ValB, &U1, &ValB);
        // Compute a.
        CopyBigInt(&ValA, &U1);
      } else {
        // Compute c.
        BigIntAdd(&U1, &U2, &ValB);
        BigIntAdd(&ValB, &ValC, &ValC);
        // Compute b.
        BigIntAdd(&ValB, &U1, &ValB);
        // Compute a.
        CopyBigInt(&ValA, &U1);
      }

      if (teach) {
        int m = ValM.limbs[0].x;
        showText("<p>We will use the ");
        if (ValM.sign == SIGN_POSITIVE) {
          showText("first");
        } else {
          showText("second");
        }
        showText(" unimodular transformation with <var>m</var> = ");
        showInt(m);
        showText(": <var>X</var> = ");
        if (ValM.sign == SIGN_POSITIVE) {
          if (m == 1) {
            showText("<var>U</var>");
          } else {
            showInt(m);
            showText(" &#8290;<var>U</var> + ");
            if (m > 2) {
              showInt(m - 1);
              showText(" &#8290;");
            }
            showText("<var>V</var>");
          }
          showText(", <var>Y</var> = <var>U</var> + <var>V</var> ");
        } else {
          showText("<var>X</var> = <var>U</var> + <var>V</var>, <var>Y</var> = ");
          if (m > 2) {
            showInt(m - 1);
            showText("&#8290;");
          }
          if (m > 1) {
            showText("<var>U</var> + ");
            showInt(m);
            showText("&#8290;");
          }
          showText("<var>V</var> ");
        }
        showEqNbr(2);
        showText("</p>");
        showText("<p>Using");
        showEqNbr(2);
        showText(", the equation ");
        showEqNbr(1);
        showText(" converts to:</p>");
        PrintQuad(BigIntegerToBigInt(&ValA),
                  BigIntegerToBigInt(&ValB),
                  BigIntegerToBigInt(&ValC),
                  "<var>U</var>", "<var>V</var>");
        showText(" = ");
        ShowNumber(&ValK);
        showText(" ");
        showEqNbr(3);
        equationNbr = 4;
        varXnoTrans = "<var>U</var>";
        varYnoTrans = "<var>V</var>";
      }
    }

    if (teach) {
      enum eSign signK = ValK.sign;
      showText("<p>We will have to solve several quadratic modular "
               "equations. To do this we have to factor the modulus and "
               "find the solution modulo the powers of the prime factors. "
               "Then we combine them by using the Chinese Remainder "
               "Theorem.</p><p>The different moduli are divisors of the "
               "right hand side, so we only have to factor it once.</p>");
      showText("<p>");
      ValK.sign = SIGN_POSITIVE;   // Make the number to factor positive.
      showFactors(&ValK, *factors);
      ValK.sign = signK;
    }

    for (;;) {
      if (teach) {
        showText("<p>");
        if ((ValE.nbrLimbs > 1) || (ValE.limbs[0].x > 1)) {
          if (BigIntIsZero(&ValM)) {
            // No unimodular transforation.
            varX = "<var>X'</var>";
            varY = "<var>Y'</var>";
          } else {
            // Unimodular transforation.
            varX = "<var>U'</var>";
            varY = "<var>V'</var>";
          }
          showText("Let ");
          ShowNumber(&ValE);
          showText("&#8290;");
          showText(varX);
          showText(" = ");
          showText(varXnoTrans);
          showText(" and ");
          ShowNumber(&ValE);
          showText("&#8290;");
          showText(varY);
          showText(" = ");
          showText(varYnoTrans);
          showText(". ");
        } else {
          if (BigIntIsZero(&ValM)) {
            // No unimodular transforation.
            varX = "<var>X</var>";
            varY = "<var>Y</var>";
          } else {
            // Unimodular transforation.
            varX = "<var>U</var>";
            varY = "<var>V</var>";
          }
        }
        showText("Searching for solutions ");
        showText(varX);
        showText(" and ");
        showText(varY);
        showText(" coprime.</p>");
        if ((ValE.nbrLimbs > 1) || (ValE.limbs[0].x > 1)) {
          showText("<p>From equation ");
          showEqNbr(BigIntIsZero(&ValM) ? 1: 3);
          showText(" we obtain ");
          PrintQuad(BigIntegerToBigInt(&ValA),
                    BigIntegerToBigInt(&ValB),
                    BigIntegerToBigInt(&ValC), varX, varY);
          showText(" = ");
          (void)BigIntMultiply(&ValK, &ValE, &ValH);
          (void)BigIntMultiply(&ValH, &ValE, &ValH);
          ShowNumber(&ValH);
          showText(" / ");
          ShowNumber(&ValE);
          showSquare();
          showText(" = ");
          ShowNumber(&ValK);
        }
      }

      CopyBigInt(&modulus, &ValK);
      modulus.sign = SIGN_POSITIVE;

      // XXX We modified the modulus, but
      // CopyBigInt(&LastModulus, &modulus);
      if (teach) {
        showText("<p>We have to solve:");
        PrintQuad(BigIntegerToBigInt(&ValA),
                  BigIntegerToBigInt(&ValB),
                  BigIntegerToBigInt(&ValC), "<var>T</var>", NULL);
        showText(" &equiv; 0 (mod ");
        showFactors(&modulus, *factors);
        showText(")<p>");
      }

      SolveQuadModEquation(
          // PERF just construct directly above.
          BigIntegerToBigInt(&ValA),
          BigIntegerToBigInt(&ValB),
          BigIntegerToBigInt(&ValC),
          BigIntegerToBigInt(&modulus));

      // Adjust counters.
      int index;
      CHECK(indexEvenMultiplicity.size() ==
            originalMultiplicities.size());
      for (index = 0; index < (int)indexEvenMultiplicity.size(); index++) {
        // Loop that increments counters.
        if (isDescending[index] == 0) {
          // Ascending.
          sFactorz *fact = &factors->product[indexEvenMultiplicity[index]];
          if (counters[index] == originalMultiplicities[index]) {
            // Next time it will be descending.
            isDescending[index] = 1;
            continue;
          } else {
            const int number_length = *fact->array;
            modulus_length = number_length;
            IntArray2BigInteger(number_length, fact->array, &bigTmp);
            (void)BigIntMultiply(&bigTmp, &bigTmp, &U3);
            fact->multiplicity -= 2;
            // Divide by square of prime.
            (void)BigIntDivide(&ValK, &U3, &ValK);
            // Multiply multiplier by prime.counters[index]++
            (void)BigIntMultiply(&ValE, &bigTmp, &ValE);
            counters[index] += 2;
            break;
          }
        } else {
          // Descending.
          sFactorz *fact = &factors->product[indexEvenMultiplicity[index]];
          if (counters[index] <= 1) {
            // Next time it will be ascending.
            isDescending[index] = 0;
            continue;
          } else {
            const int number_length = *fact->array;
            modulus_length = number_length;
            IntArray2BigInteger(number_length, fact->array, &bigTmp);
            (void)BigIntMultiply(&bigTmp, &bigTmp, &U3);
            fact->multiplicity += 2;
            // Multiply by square of prime.
            (void)BigIntMultiply(&ValK, &U3, &ValK);
            // Divide multiplier by prime.counters[index]++
            (void)BigIntDivide(&ValE, &bigTmp, &ValE);
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

    BigInt A = BigIntegerToBigInt(&ValA);
    BigInt B = BigIntegerToBigInt(&ValB);
    BigInt C = BigIntegerToBigInt(&ValC);

    // Note: G not necessarily initialized by here if
    // the condition below isn't true.
    BigInt G = BigIntegerToBigInt(&ValG);
    BigInt L = BigIntegerToBigInt(&ValL);

    BigInt Alpha = BigIntegerToBigInt(&ValAlpha);
    BigInt Beta = BigIntegerToBigInt(&ValBeta);
    BigInt GcdHomog = BigIntegerToBigInt(&ValGcdHomog);
    BigInt Discr = BigIntegerToBigInt(&discr);

    if (showRecursiveSolution &&
        callbackQuadModType == CBACK_QMOD_HYPERBOLIC) {

      // Show recursive solution.
      RecursiveSolution(A, B, C, G, L,
                        Alpha, Beta, GcdHomog, Discr);
    }
  }

  void NegativeDiscriminant() {
    callbackQuadModType = CBACK_QMOD_ELLIPTIC;
    NonSquareDiscriminant();
  }

  void showBeforeUnimodularSubstitution(
      const BigInt &Z, const BigInt &O) {
    if (teach) {
      showText("<p>");
      showText(varXnoTrans);
      showText(" = ");
      ShowBigInt(Z);
      showText(", ");
      showText(varYnoTrans);
      showText(" = ");
      ShowBigInt(O);
      showText("</p>");
      showText("<p>From ");
      showEqNbr(2);
      showText(": ");
    }
  }

  // Returns Temp0, Temp1
  std::pair<BigInt, BigInt>
  UnimodularSubstitution(const BigInt &M,
                         const BigInt &Z,
                         const BigInt &O) {
    BigInt Temp0, Temp1;
    if (M < 0) {
      // Perform the substitution: x = X + Y, y = (|m|-1)X + |m|Y
      showBeforeUnimodularSubstitution(Z, O);
      Temp0 = (Z + O);
      Temp1 = Temp0 * -M - Z;
    } else if (M == 0) {
      Temp0 = Z;
      Temp1 = O;
    } else {
      // Perform the substitution: x = mX + (m-1)Y, y = X + Y
      showBeforeUnimodularSubstitution(Z, O);
      Temp1 = Z + O;
      Temp0 = Temp1 * M - O;
    }
    return std::make_pair(Temp0, Temp1);
  }

  // On input: ValH: value of u, ValI: value of v.
  // Output: ((tu - nv)*E, u*E) and ((-tu + nv)*E, -u*E)
  // If m is greater than zero, perform the substitution: x = mX + (m-1)Y, y = X + Y
  // If m is less than zero, perform the substitution: x = X + Y, y = (|m|-1)X + |m|Y
  // Do not substitute if m equals zero.
  void NonSquareDiscrSolution(bool two_solutions, BigInteger *value) {
    if (two_solutions) {
      CHECK(showSolution == TWO_SOLUTIONS);
    } else {
      CHECK(showSolution == ONE_SOLUTION);
    }

    // Back up value.
    BigInt Tmp12 = BigIntegerToBigInt(value);
    // Get value of tu - Kv
    (void)BigIntMultiply(value, &ValH, &ValZ);    // tu
    CopyBigInt(&bigTmp, &ValK);
    if (callbackQuadModType == CBACK_QMOD_HYPERBOLIC) {
      BigIntChSign(&bigTmp);                // Get K
      bigTmp.sign = SIGN_NEGATIVE;
    }

    (void)BigIntMultiply(&bigTmp, &ValI, &bigTmp);// Kv
    BigIntSubt(&ValZ, &bigTmp, &ValZ);      // U = tu - Kv
    if (teach && ((ValE.nbrLimbs > 1) || (ValE.limbs[0].x > 1))) {
      // E > 1
      showText("<p>From ");
      showEqNbr(equationNbr);
      showText(": ");
      showText(varX);
      showText(" = ");
      ShowNumber(&ValZ);
      showText(", ");
      showText(varY);
      showText(" = ");
      ShowNumber(&ValH);
    }
    (void)BigIntMultiply(&ValZ, &ValE, &ValZ);    // X = (tu - Kv)*E
    (void)BigIntMultiply(&ValH, &ValE, &ValO);    // Y = u*E

    BigInt M = BigIntegerToBigInt(&ValM);
    BigInt Z = BigIntegerToBigInt(&ValZ);
    BigInt O = BigIntegerToBigInt(&ValO);

    const BigInt Alpha = BigIntegerToBigInt(&ValAlpha);
    const BigInt Beta = BigIntegerToBigInt(&ValBeta);
    const BigInt Div = BigIntegerToBigInt(&ValDiv);

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
    BigIntToBigInteger(Tmp12, value);
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
    U = V;
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

    BigInt U3 = U2;
    U2 = U1;
    U1 = Tmp3;
    // CopyBigInt(&U3, &U2);
    // CopyBigInt(&U2, &U1);
    // CopyBigInt(&U1, &V3);

    Tmp *= V1;
    Tmp += V2;
    // (void)BigIntMultiply(&bigTmp, &V1, &bigTmp);
    // BigIntAdd(&bigTmp, &V2, &bigTmp);
    BigInt V3 = V2;
    V2 = V1;
    V1 = Tmp;
    // CopyBigInt(&V3, &V2);
    // CopyBigInt(&V2, &V1);
    // CopyBigInt(&V1, &bigTmp);
    return std::make_tuple(U, U1, U2, U3,
                           V, V1, V2, V3);
  }

  // XXX a few more globals
  void ShowSolutionFromConvergent(const BigInt &H,
                                  const BigInt &I) {
    if (teach) {
      showText("Solution of ");
      showEqNbr(equationNbr + 1);
      showText(" found using the convergent ");
      showText(varY);
      showText(" / ");
      if (callbackQuadModType == CBACK_QMOD_HYPERBOLIC) {
        showText("(&minus;<var>k</var>) = ");
      } else {
        showText("<var>k</var> = ");
      }
      ShowBigInt(H);
      showText(" / ");
      ShowBigInt(I);
      showText(" of ");
      showEqNbr(contfracEqNbr);
      showText("</p>");
    }
  }

  void showFirstSolution(const char *discrim, const char *valueP) {
    showText("<p>When the discriminant equals ");
    showText(discrim);
    showText(" and");
    showText(" <var>P</var> = ");
    showText(valueP);
    showText(", a solution is");
    showText(" (");
    showText(varY);
    showText(", <var>k</var>) = (");
  }

  void showOtherSolution(const char *ordinal) {
    showText("<p>The ");
    showText(ordinal);
    showText(" solution is");
    showText(" (");
    showText(varY);
    showText(", <var>k</var>) = (");
  }

  // Output:
  // false = There are no solutions because gcd(P, Q, R) > 1
  // true = gcd(P, Q, R) = 1.
  bool PerformTransformation(const BigInteger *value) {
    // Compute P as (at^2+bt+c)/K
    (void)BigIntMultiply(&ValA, value, &ValQ);
    BigIntAdd(&ValQ, &ValB, &ValP);
    (void)BigIntMultiply(&ValP, value, &ValP);
    BigIntAdd(&ValP, &ValC, &ValP);
    (void)BigIntDivide(&ValP, &ValK, &ValP);
    // Compute Q <- -(2at + b).
    BigIntAdd(&ValQ, &ValQ, &ValQ);
    BigIntAdd(&ValQ, &ValB, &ValQ);
    BigIntChSign(&ValQ);
    // Compute R <- aK
    (void)BigIntMultiply(&ValA, &ValK, &ValR);
    if (teach)
      {
        showText("<p>The transformation ");
        showText(varX);
        showText(" = ");
        CopyBigInt(&ValH, value);
        CopyBigInt(&ValI, &ValK);
        BigIntChSign(&ValI);
        BigInteger TmpJ;
        intToBigInteger(&TmpJ, 0);
        ShowLin(BigIntegerToBigInt(&ValH),
                BigIntegerToBigInt(&ValI),
                BigIntegerToBigInt(&TmpJ),
                varY, "<var>k</var>");
        showText(" ");
        showEqNbr(equationNbr);
        showText(" converts ");
        PrintQuad(BigIntegerToBigInt(&ValA),
                  BigIntegerToBigInt(&ValB),
                  BigIntegerToBigInt(&ValC), varX, varY);
        showText(" = ");
        ShowNumber(&ValK);
        showText(" to ");
        showText("<var>P</var>&#8290;");
        showText(varY);
        showSquare();
        showText(" + <var>Q</var>&#8290;");
        showText(varY);
        showText("<var>k</var> + <var>R</var>&#8290; <var>k</var>");
        showSquare();
        showText(" = 1 ");
        showEqNbr(equationNbr + 1);
        showText("</p>");
        showText("where: ");
        showText("<var>P</var> = (<var>a</var>&#8290;<var>T</var>");
        showSquare();
        showText(" + <var>b</var>&#8290;<var>T</var> + <var>c</var>) / n = ");
        ShowNumber(&ValP);
        showText(", <var>Q</var> = &minus;(2&#8290;<var>a</var>&#8290;<var>T</var> + <var>b</var>) = ");
        ShowNumber(&ValQ);
        showText(", <var>R</var> = <var>a</var>&#8290;<var>n</var> = ");
        ShowNumber(&ValR);
        showText("</p>");
      }
    // Compute gcd of P, Q and R.
    BigIntGcd(&ValP, &ValQ, &ValH);   // Use ValH and ValI as temporary variables.
    BigIntGcd(&ValH, &ValR, &ValI);
    if ((ValI.nbrLimbs == 1) && (ValI.limbs[0].x == 1))
      { // Gcd equals 1.
        return 1;
      }
    if (teach)
      {
        showText("<p>There are no solutions because "
                 "gcd(<var>P</var>, <var>Q</var>, <var>R</var>) "
                 "is greater than 1.</p>");
        equationNbr += 2;
      }
    return 0;
  }

  void callbackQuadModElliptic(BigInteger *value) {
    if (!PerformTransformation(value))
      {      // No solutions because gcd(P, Q, R) > 1.
        return;
      }
    if ((ValP.sign == SIGN_POSITIVE) && (ValP.nbrLimbs == 1))
      {
        int Plow = ValP.limbs[0].x;
        if (((discr.nbrLimbs > 1) || (discr.limbs[0].x > 4)) && (Plow == 1))
          {      // Discriminant is less than -4 and P equals 1.
            intToBigInteger(&ValH, 1);
            intToBigInteger(&ValI, 0);
            NonSquareDiscrSolution(false, value);   // (1, 0)
            equationNbr += 2;
            return;
          }
        if ((discr.nbrLimbs == 1) && (discr.limbs[0].x == 4))
          {      // Discriminant is equal to -4.
            CopyBigInt(&ValG, &ValQ);
            BigIntDivideBy2(&ValG);
            if (Plow == 1)
              {
                intToBigInteger(&ValH, 1);
                intToBigInteger(&ValI, 0);
                if (teach)
                  {
                    showFirstSolution("-4", "1");
                    showText("1, 0)</p>");
                  }
                NonSquareDiscrSolution(false, value);   // (1, 0)
                CopyBigInt(&ValH, &ValG);
                intToBigInteger(&ValI, -1);
                if (teach)
                  {
                    showOtherSolution("second");
                    showText("<var>Q</var>/2 = ");
                    ShowNumber(&ValH);
                    showText(", -1)</p>");
                  }
                NonSquareDiscrSolution(false, value);   // (Q/2, -1)
                equationNbr += 2;
                return;
              }
            if (Plow == 2)
              {
                intToBigInteger(&ValI, -1);
                CopyBigInt(&ValH, &ValG);
                subtractdivide(&ValH, 1, 2);
                if (teach)
                  {
                    showFirstSolution("-4", "2");
                    showText("(<var>Q</var>/2 &minus; 1) / 2 = ");
                    ShowNumber(&ValH);
                    showText(", -1)</p>");
                  }
                NonSquareDiscrSolution(false, value);   // ((Q/2-1)/2, -1)
                intToBigInteger(&ValI, -1);
                CopyBigInt(&ValH, &ValG);
                subtractdivide(&ValH, -1, 2);
                if (teach)
                  {
                    showOtherSolution("second");
                    showText("(<var>Q</var>/2 + 1) / 2 = ");
                    ShowNumber(&ValH);
                    showText(", -1)</p>");
                  }
                NonSquareDiscrSolution(false, value);   // ((Q/2+1)/2, -1)
                equationNbr += 2;
                return;
              }
          }
        if ((discr.nbrLimbs == 1) && (discr.limbs[0].x == 3)) {
          // Discriminant is equal to -3.
          if (Plow == 1) {
            intToBigInteger(&ValH, 1);
            intToBigInteger(&ValI, 0);
            if (teach) {
              showFirstSolution("-3", "1");
              showText("1, 0)</p>");
            }
            NonSquareDiscrSolution(false, value);   // (1, 0)
            CopyBigInt(&ValG, &ValQ);
            subtractdivide(&ValG, 1, 2);
            CopyBigInt(&ValH, &ValG);
            intToBigInteger(&ValI, -1);
            if (teach) {
              showOtherSolution("second");
              showText("(<var>Q</var> &#8209; 1)/2 = ");
              ShowNumber(&ValH);
              showText(", -1)</p>");
            }
            NonSquareDiscrSolution(false, value);   // ((Q-1)/2, -1)
            intToBigInteger(&ValI, -1);
            BigIntSubt(&ValG, &ValI, &ValH);
            if (teach) {
              showOtherSolution("third");
              showText("(<var>Q</var> + 1)/2 = ");
              ShowNumber(&ValH);
              showText(", -1)</p>");
            }
            NonSquareDiscrSolution(false, value);   // ((Q+1)/2, -1)
            equationNbr += 2;
            return;
          }

          if (Plow == 3) {
            intToBigInteger(&ValI, -1);
            CopyBigInt(&ValH, &ValQ);
            subtractdivide(&ValH, -3, 6);
            if (teach) {
              showFirstSolution("-3", "3");
              showText("(<var>Q</var> + 3)/6 = ");
              ShowNumber(&ValH);
              showText(", -1)</p>");
            }
            NonSquareDiscrSolution(false, value);   // ((Q+3)/6, -1)
            intToBigInteger(&ValI, -2);
            CopyBigInt(&ValH, &ValQ);
            subtractdivide(&ValH, 0, 3);
            if (teach) {
              showOtherSolution("second");
              showText("<var>Q</var>/3 = ");
              ShowNumber(&ValH);
              showText(", -2)</p>");
            }
            NonSquareDiscrSolution(false, value);   // (Q/3, -2)
            intToBigInteger(&ValI, -1);
            CopyBigInt(&ValH, &ValQ);
            subtractdivide(&ValH, 3, 6);
            if (teach) {
              showOtherSolution("third");
              showText("(<var>Q</var> &minus; 3)/6 = ");
              ShowNumber(&ValH);
              showText(", -1)</p>");
            }
            NonSquareDiscrSolution(false, value);   // ((Q-3)/6, -1)
            equationNbr += 2;
            return;
          }
        }
      }
    if (teach)
      {
        int coeffNbr = 0;
        contfracEqNbr = equationNbr + 2;
        showText("<p>To obtain solutions to the equation ");
        showEqNbr(equationNbr + 1);
        showText(" we have to compute the convergents of the continued fraction of "
                 "&minus;<var>Q</var> / 2&#8290;<var>P</var> = ");
        CopyBigInt(&ValU, &ValQ);
        BigIntChSign(&ValU);
        BigIntAdd(&ValP, &ValP, &ValV);
        if (ValV.sign == SIGN_NEGATIVE)
          {    // If denominator is negative, change sign of numerator and denominator.
            BigIntChSign(&ValU);
            BigIntChSign(&ValV);
          }
        ShowNumber(&ValU);
        showText(" / ");
        ShowNumber(&ValV);
        showText("</p>"
                 "The continued fraction is: ");
        for (;;) {
          floordiv(&ValU, &ValV, &bigTmp);
          ShowNumber(&bigTmp);
          // Values of U3 and V3 are not used, so they can be overwritten now.
          // Compute new value of ValU and ValV.
          (void)BigIntMultiply(&bigTmp, &ValV, &U3);
          BigIntSubt(&ValU, &U3, &U3);
          CopyBigInt(&ValU, &ValV);
          CopyBigInt(&ValV, &U3);
          if (BigIntIsZero(&ValV))
            {
              break;
            }
          if (coeffNbr == 0)
            {
              showText("+ //");
            }
          else
            {
              showText(", ");
            }
          coeffNbr++;
        }
        showText("// ");
        showEqNbr(equationNbr+2);
        showText("</p>");
      }
    // Compute bound L = sqrt(4P/(-D))
    MultInt(&U1, &ValP, 4);
    (void)BigIntDivide(&U1, &discr, &U1);
    BigIntChSign(&U1);               // 4P/(-D)
    SquareRoot(U1.limbs, ValL.limbs, U1.nbrLimbs, &ValL.nbrLimbs);  // sqrt(4P/(-D))

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
          GetNextConvergent(BigIntegerToBigInt(&ValU),
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

      BigIntSubt(&ValL, &V1, &bigTmp);    // Check whether the denominator of convergent exceeds bound.
      if (bigTmp.sign == SIGN_NEGATIVE)
        {
          if (teach)
            {
              showText("<p>There are no convergents for which ");
              showEqNbr(equationNbr + 1);
              showText(" holds.</p>");
            }
          break;                            // Bound exceeded, so go out.
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
          int D;
          CopyBigInt(&ValH, &U1);
          CopyBigInt(&ValI, &V1);
          ShowSolutionFromConvergent(BigIntegerToBigInt(&ValH),
                                     BigIntegerToBigInt(&ValI));
          NonSquareDiscrSolution(false, value);        // (U1, V1)
          D = discr.limbs[0].x;
          if ((discr.nbrLimbs > 1) || (D > 4)) {
              // Discriminant is less than -4, go out.
              break;
            }
          if ((D == 3) || (D == 4)) {
              // Discriminant is equal to -3 or -4.
              {
                const auto &[U, UU1, UU2, UU3,
                             V, VV1, VV2, VV3] =
                  GetNextConvergent(BigIntegerToBigInt(&ValU),
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

              CopyBigInt(&ValH, &U1);
              CopyBigInt(&ValI, &V1);
              ShowSolutionFromConvergent(BigIntegerToBigInt(&ValH),
                                         BigIntegerToBigInt(&ValI));
              NonSquareDiscrSolution(false, value);      // (U1, V1)
              if (D == 3) {

                  {
                    const auto &[U, UU1, UU2, UU3,
                                 V, VV1, VV2, VV3] =
                      GetNextConvergent(BigIntegerToBigInt(&ValU),
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

                  CopyBigInt(&ValH, &U1);
                  CopyBigInt(&ValI, &V1);
                  ShowSolutionFromConvergent(BigIntegerToBigInt(&ValH),
                                             BigIntegerToBigInt(&ValI));
                  NonSquareDiscrSolution(false, value);    // (U1, V1)
                }
              break;
            }
        }
    }
    equationNbr += 3;
  }

  void CheckSolutionSquareDiscr() {
    // XXX pass args
    BigInt CurrentFactor = BigIntegerToBigInt(&currentFactor);
    BigInt H = BigIntegerToBigInt(&ValH);
    BigInt I = BigIntegerToBigInt(&ValI);
    BigInt L = BigIntegerToBigInt(&ValL);
    BigInt M = BigIntegerToBigInt(&ValM);
    BigInt Z = BigIntegerToBigInt(&ValZ);

    BigInt Alpha = BigIntegerToBigInt(&ValAlpha);
    BigInt Beta = BigIntegerToBigInt(&ValBeta);
    BigInt Div = BigIntegerToBigInt(&ValDiv);

    CHECK(CurrentFactor != 0);
    BigInt N = Z / CurrentFactor;

    if (teach) {
      BigInt J(0);
      showText("<li><p>");
      ShowLin(H, I, J, "X", "Y");
      showText(" = ");
      ShowBigInt(CurrentFactor);
      showText(",");
      ShowLin(L, M, J, "X", "Y");
      showText(" = ");
      ShowBigInt(N);
      showText("</p>");
    }

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

    // No solution found.
    if (teach) {
      showText("<p>The system of two equations does not have "
               "integer solutions <var>X</var>, <var>Y</var>.</p>");
    }
  }

  void PerfectSquareDiscriminant() {
    enum eSign signTemp;

    if (BigIntIsZero(&ValA)) {
      // Let R = gcd(b, c)
      // (bX + cY) Y = k
      BigIntGcd(&ValB, &ValC, &ValR);

      if (teach) {
        intToBigInteger(&ValS, 1);
        CopyBigInt(&V1, &ValB);
        CopyBigInt(&V2, &ValC);
        intToBigInteger(&ValH, 0);
        intToBigInteger(&ValI, 1);
        CopyBigInt(&ValL, &ValK);
      }

    } else {
      // Multiplying by 4a we get (2aX + (b+g)Y)(2aX + (b-g)Y) = 4ak
      // Let R = gcd(2a, b+g)
      BigIntAdd(&ValA, &ValA, &V1);
      BigIntAdd(&ValB, &ValG, &V2);
      BigIntGcd(&V1, &V2, &ValR);
      // Let S = gcd(2a, b-g)
      CopyBigInt(&ValH, &V1);
      BigIntSubt(&ValB, &ValG, &ValI);
      BigIntGcd(&ValH, &ValI, &ValS);
      // Let L = 4ak
      (void)BigIntMultiply(&ValA, &ValK, &ValL);
      MultInt(&ValL, &ValL, 4);

      if (teach) {
        showText("<p>Multiplying by"
                 " 4&#8290;<var>a</var>:</p>");
      }
    }

    if (teach) {
      BigInteger TeachJ;
      intToBigInteger(&TeachJ, 0);
      showText("<p>(");
      ShowLin(BigIntegerToBigInt(&V1),
              BigIntegerToBigInt(&V2),
              BigIntegerToBigInt(&TeachJ), "X", "Y");
      showText(") &#8290;(");
      ShowLin(BigIntegerToBigInt(&ValH),
              BigIntegerToBigInt(&ValI),
              BigIntegerToBigInt(&TeachJ), "X", "Y");
      showText(") = ");
      ShowNumber(&ValL);
      showText("</p><p>");
      (void)BigIntMultiply(&ValR, &ValS, &V3);

      if ((V3.nbrLimbs > 1) || (V3.limbs[0].x > 1)) {
        (void)BigIntDivide(&V1, &ValR, &V1);
        (void)BigIntDivide(&V2, &ValR, &V2);
        (void)BigIntDivide(&ValH, &ValS, &ValH);
        (void)BigIntDivide(&ValI, &ValS, &ValI);
        (void)BigIntRemainder(&ValL, &V3, &bigTmp);

        if (!BigIntIsZero(&bigTmp)) {
          ShowNumber(&V3);
          showText(" &#8290;");
        }

        showText("(");
        ShowLin(BigIntegerToBigInt(&V1),
                BigIntegerToBigInt(&V2),
                BigIntegerToBigInt(&TeachJ), "X", "Y");
        showText(") &#8290;(");
        ShowLin(BigIntegerToBigInt(&ValH),
                BigIntegerToBigInt(&ValI),
                BigIntegerToBigInt(&TeachJ), "X", "Y");
        showText(") = ");

        if (BigIntIsZero(&bigTmp)) {
          (void)BigIntDivide(&ValL, &V3, &ValL);
        }

        ShowNumber(&ValL);
        showText("</p><p>");
        if (!BigIntIsZero(&bigTmp)) {
          showText("The right hand side is not multiple of the number "
                   "located at the left of the parentheses, so there "
                   "are no solutions.</p>");
        }
      }
    }

    if (BigIntIsZero(&ValK)) {
      // k equals zero.
      eLinearSolution ret;
      if (BigIntIsZero(&ValA)) {
        // Coefficient a does equals zero.
        // Solve Dy + beta = 0
        intToBigInteger(&Aux0, 0);
        ret = LinearEq(BigIntegerToBigInt(&Aux0),
                       BigIntegerToBigInt(&discr),
                       BigIntegerToBigInt(&ValBeta));
        startResultBox(ret);
        PrintLinear(ret, "t");
        endResultBox(ret);
        // Solve bDx + cDy + b*alpha + c*beta = 0
        (void)BigIntMultiply(&ValB, &discr, &Aux0);
        (void)BigIntMultiply(&ValC, &discr, &Aux1);
        (void)BigIntMultiply(&ValB, &ValAlpha, &Aux2);
        (void)BigIntMultiply(&ValC, &ValBeta, &bigTmp);
        BigIntAdd(&Aux2, &bigTmp, &Aux2);
      } else {
        // Coefficient a does not equal zero.
        // Solve 2aD x + (b+g)D y = 2a*alpha + (b+g)*beta
        (void)BigIntMultiply(&ValA, &discr, &Aux0);
        BigIntAdd(&Aux0, &Aux0, &Aux0);
        BigIntAdd(&ValB, &ValG, &Aux1);
        (void)BigIntMultiply(&Aux1, &discr, &Aux1);
        (void)BigIntMultiply(&ValA, &ValAlpha, &Aux2);
        BigIntAdd(&Aux2, &Aux2, &Aux2);
        BigIntAdd(&ValB, &ValG, &bigTmp);
        (void)BigIntMultiply(&bigTmp, &ValBeta, &bigTmp);
        BigIntAdd(&Aux2, &bigTmp, &Aux2);
        BigIntChSign(&Aux2);
        ret = LinearEq(BigIntegerToBigInt(&Aux0),
                       BigIntegerToBigInt(&Aux1),
                       BigIntegerToBigInt(&Aux2));
        startResultBox(ret);
        PrintLinear(ret, "t");
        endResultBox(ret);
        // Solve the equation 2aD x + (b-g)D y = 2a*alpha + (b-g)*beta
        (void)BigIntMultiply(&ValA, &discr, &Aux0);
        BigIntAdd(&Aux0, &Aux0, &Aux0);
        BigIntSubt(&ValB, &ValG, &Aux1);
        (void)BigIntMultiply(&Aux1, &discr, &Aux1);
        (void)BigIntMultiply(&ValA, &ValAlpha, &Aux2);
        BigIntAdd(&Aux2, &Aux2, &Aux2);
        BigIntSubt(&ValB, &ValG, &bigTmp);
        (void)BigIntMultiply(&bigTmp, &ValBeta, &bigTmp);
        BigIntAdd(&Aux2, &bigTmp, &Aux2);
        BigIntChSign(&Aux2);
      }

      ret = LinearEq(BigIntegerToBigInt(&Aux0),
                     BigIntegerToBigInt(&Aux1),
                     BigIntegerToBigInt(&Aux2));
      startResultBox(ret);
      PrintLinear(ret, "t");
      endResultBox(ret);
      return;
    }

    // k does not equal zero.
    if (BigIntIsZero(&ValA)) {
      // If R does not divide k, there is no solution.
      CopyBigInt(&U3, &ValK);
      CopyBigInt(&U1, &ValR);
    } else {
      // If R*S does not divide 4ak, there is no solution.
      (void)BigIntMultiply(&ValR, &ValS, &U1);
      (void)BigIntMultiply(&ValA, &ValK, &U2);
      multadd(&U3, 4, &U2, 0);
    }

    (void)BigIntRemainder(&U3, &U1, &U2);

    if (!BigIntIsZero(&U2)) {
      return;
    }

    (void)BigIntDivide(&U3, &U1, &ValZ);

    if (teach) {
      showText("We have to find all factors of the right hand side.</p>");
    }

    // Compute all factors of Z = 4ak/RS
    signTemp = ValZ.sign;
    ValZ.sign = SIGN_POSITIVE;  // Factor positive number.

    std::unique_ptr<Factors> factors = BigFactor(&ValZ);

    // Do not factor again same modulus.
    // CopyBigInt(&LastModulus, &ValZ);
    ValZ.sign = signTemp;       // Restore sign of Z = 4ak/RS.
    // x = (NI - JM) / D(IL - MH) and y = (JL - NH) / D(IL - MH)
    // The denominator cannot be zero here.
    // H = 2a/R, I = (b+g)/R, J = F + H * alpha + I * beta
    // L = 2a/S, M = (b-g)/S, N = Z/F + L * alpha + M * beta
    // F is any factor of Z (positive or negative).
    nbrFactors = factors->product.size();
    if (teach) {
      showText("<p>");
      showFactors(&ValZ, *factors);
      showText("<ol>");
    }

    if (BigIntIsZero(&ValA)) {
      intToBigInteger(&ValH, 0);                    // H <- 0
      intToBigInteger(&ValI, 1);                    // I <- 1
      (void)BigIntDivide(&ValB, &ValR, &ValL);      // L <- b/R
      (void)BigIntDivide(&ValC, &ValR, &ValM);      // M <- c/R
    } else {
      BigIntAdd(&ValA, &ValA, &U3);                 // 2a
      (void)BigIntDivide(&U3, &ValR, &ValH);        // H <- 2a/R
      (void)BigIntDivide(&U3, &ValS, &ValL);        // L <- 2a/S
      BigIntAdd(&ValB, &ValG, &ValI);
      (void)BigIntDivide(&ValI, &ValR, &ValI);      // I <- (b+g)/R
      BigIntSubt(&ValB, &ValG, &ValM);
      (void)BigIntDivide(&ValM, &ValS, &ValM);      // M <- (b-g)/S
    }

    (void)BigIntMultiply(&ValH, &ValAlpha, &ValK);  // H * alpha
    (void)BigIntMultiply(&ValI, &ValBeta, &bigTmp); // I * beta
    BigIntAdd(&ValK, &bigTmp, &ValK);         // K <- H * alpha + I * beta
    (void)BigIntMultiply(&ValL, &ValAlpha, &ValO);  // L * alpha
    (void)BigIntMultiply(&ValM, &ValBeta, &bigTmp); // M * beta
    BigIntAdd(&ValO, &bigTmp, &ValO);         // O <- L * alpha + M * beta
    // Compute denominator: D(IL - MH)
    (void)BigIntMultiply(&ValI, &ValL, &ValDen);    // IL
    (void)BigIntMultiply(&ValM, &ValH, &bigTmp);    // MH
    BigIntSubt(&ValDen, &bigTmp, &ValDen);    // IL - MH
    (void)BigIntMultiply(&ValDen, &discr, &ValDen); // D(IL - MH)

    // Loop that finds all factors of Z.
    // Use Gray code to use only one big number.
    // Gray code: 0->000, 1->001, 2->011, 3->010, 4->110, 5->111, 6->101, 7->100.
    // Change from zero to one means multiply, otherwise divide.
    std::vector<int> counters(400, 0);
    (void)memset(isDescending, 0, sizeof(isDescending));
    intToBigInteger(&currentFactor, 1);
    for (;;) {
      CheckSolutionSquareDiscr();       // Process positive divisor.
      BigIntChSign(&currentFactor);
      CheckSolutionSquareDiscr();       // Process negative divisor.
      BigIntChSign(&currentFactor);
      sFactorz *pstFactor = &factors->product[0];
      int index;
      for (index = 0; index < nbrFactors; index++) {
        // Loop that increments counters.
        if (isDescending[index] == 0) {
          // Ascending.
          if (counters[index] == pstFactor->multiplicity) {
            isDescending[index] = 1;    // Next time it will be descending.
            pstFactor++;
            continue;
          }
          IntArray2BigInteger(modulus_length, pstFactor->array, &qmllr.prime);
          (void)BigIntMultiply(&currentFactor, &qmllr.prime, &currentFactor);
          counters[index]++;
          break;
        }

        if (counters[index] == 0) {
          // Descending.
          isDescending[index] = 0;    // Next time it will be ascending.
          pstFactor++;
          continue;
        }

        IntArray2BigInteger(modulus_length, pstFactor->array, &qmllr.prime);
        (void)BigIntDivide(&currentFactor, &qmllr.prime, &currentFactor);
        counters[index]--;
        break;
      }

      if (index == nbrFactors) {
        // All factors have been found. Exit loop.
        if (teach) {
          showText("</ol>");
        }
        break;
      }
    }
  }

  void PositiveDiscriminant() {
    callbackQuadModType = CBACK_QMOD_HYPERBOLIC;
    NonSquareDiscriminant();
  }

  // First check: |u| < g.
  // Second check: |u+g| > |v|
  // Third check: |u-g| < |v|
  // On input ValG = floor(g), g > 0.
  // g is not an integer number.
  void CheckStartOfContinuedFractionPeriod() {
    CopyBigInt(&bigTmp, &ValU);
    // Set bigTmp to |u|
    bigTmp.sign = SIGN_POSITIVE;
    // Compute bigTmp as floor(g) - |u|
    BigIntSubt(&ValG, &bigTmp, &bigTmp);

    if (bigTmp.sign == SIGN_POSITIVE) {
      // First check |u| < g passed.
      CopyBigInt(&Tmp1, &ValV);
      // Set Tmp1 to |v|
      Tmp1.sign = SIGN_POSITIVE;
      // Compute Tmp2 as u + floor(g) which equals floor(u+g)
      BigIntAdd(&ValU, &ValG, &Tmp2);

      if (Tmp2.sign == SIGN_NEGATIVE) {
        // Round to number nearer to zero.
        addbigint(&Tmp2, 1);
      }

      // Compute Tmp2 as floor(|u+g|)
      Tmp2.sign = SIGN_POSITIVE;
      // Compute bigTmp as floor(|u+g|) - |v|
      BigIntSubt(&Tmp2, &Tmp1, &bigTmp);

      if (bigTmp.sign == SIGN_POSITIVE) {
        // Second check |u+g| > |v| passed.
        // Compute Tmp2 as u - floor(g)
        BigIntSubt(&ValU, &ValG, &Tmp2);

        if ((Tmp2.sign == SIGN_NEGATIVE) || (BigIntIsZero(&Tmp2))) {
          // Round down number to integer.
          addbigint(&Tmp2, -1);
        }

        // Compute Tmp2 as floor(|u-g|)
        Tmp2.sign = SIGN_POSITIVE;
        // Compute Tmp2 as |v| - floor(|u-g|)
        BigIntSubt(&Tmp1, &Tmp2, &bigTmp);

        if (bigTmp.sign == SIGN_POSITIVE) {
          // Third check |u-g| < |v| passed.
          // Save U and V to check period end.
          CopyBigInt(&startPeriodU, &ValU);
          CopyBigInt(&startPeriodV, &ValV);
        }
      }
    }
  }

  void ShowArgumentContinuedFraction(bool positive_denominator,
                                     const BigInt &B,
                                     const BigInt &L,
                                     const BigInt &U,
                                     const BigInt &V) {
    showText(" (");
    if (!positive_denominator) {
      showText("&minus;");
    }
    showText("<var>Q</var> + "
             "<span class = \"sqrtout\"><span class=\"sqrtin\"><var>D</var>");
    if (B.IsEven()) {
      showText(" / 4");
    }
    showText("</span></span>) / ");
    if (B.IsOdd()) {
      showText(positive_denominator ? "(" : "(&minus;");
      showText("2<var>R</var>) = ");
    } else {
      showText(positive_denominator ?
               "<var>R</var> = " : "(&minus;<var>R</var>) = ");
    }
    if (U != 0) {
      showText("(");
      ShowBigInt(U);
      showText(" + ");
    }
    showText("<span class=\"sqrtout\"><span class=\"sqrtin\">");
    ShowBigInt(L);
    showText("</span></span>");
    if (U != 0) {
      showText(")");
    }
    showText(" / ");
    if (V < 0) {
      showText("(");
    }
    ShowBigInt(V);
    if (V < 0) {
      showText(")");
    }
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
    int index = 0;
    int periodIndex = 0;
    char isIntegerPart;
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
    if (teach) {
      CopyBigInt(&U1, &ValU);      // Back up numerator and denominator.
      CopyBigInt(&V1, &ValV);
      showText("<p>The continued fraction expansion of ");
      ShowArgumentContinuedFraction(
          !!positiveDenominator,
          BigIntegerToBigInt(&ValB),
          BigIntegerToBigInt(&ValL),
          BigIntegerToBigInt(&ValU),
          BigIntegerToBigInt(&ValV));

      showText(" is:</p>");
      intToBigInteger(&startPeriodU, -1);      // Less than zero means outside period.
      index = 0;
      for (;;) {
        if (startPeriodU.sign == SIGN_POSITIVE) {
          // Already inside period.
          periodIndex++;
          if (BigIntEqual(&ValU, &startPeriodU) &&
              BigIntEqual(&ValV, &startPeriodV)) {
            // New period started.
            break;      // Go out in this case.
          }
        }
        else if (index > 0) {
          // Check if periodic part of continued fraction has started.
          CheckStartOfContinuedFractionPeriod();
          if (startPeriodU.sign == SIGN_POSITIVE) {
            showText("<span class=\"bold\">");
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
        if (index == 1) {
          showText("+ // ");
        } else if (index > 1) {
          showText(", ");
        }
        index++;
        ShowNumber(&Tmp1);
        // Update numerator and denominator.
        (void)BigIntMultiply(&Tmp1, &ValV, &bigTmp); // U <- a*V - U
        BigIntSubt(&bigTmp, &ValU, &ValU);
        (void)BigIntMultiply(&ValU, &ValU, &bigTmp); // V <- (D - U^2)/V
        BigIntSubt(&ValL, &bigTmp, &bigTmp);
        (void)BigIntDivide(&bigTmp, &ValV, &Tmp1);
        CopyBigInt(&ValV, &Tmp1);
      }
      showText("</span>// ");
      showEqNbr(contfracEqNbr);
      showText("</p>");
      CopyBigInt(&ValU, &U1);      // Restore numerator and denominator.
      CopyBigInt(&ValV, &V1);
    }

    // Initialize variables.
    intToBigInteger(&U1, 1);
    intToBigInteger(&U2, 0);
    intToBigInteger(&V1, 0);
    intToBigInteger(&V2, 1);
    // Less than zero means outside period.
    intToBigInteger(&startPeriodU, -1);
    index = 0;

    if (solutionNbr == SECOND_SOLUTION) {
      index++;
    }

    isIntegerPart = 1;

    for (;;) {
      if ((ValV.nbrLimbs == 1) && (ValV.limbs[0].x == (isBeven ? 1 : 2)) &&
          ((index & 1) == ((ValK.sign == ValV.sign)? 0 : 1))) {

        // Found solution.
        if ((discr.nbrLimbs == 1) && (discr.limbs[0].x == 5) && (ValA.sign != ValK.sign) &&
            (solutionNbr == FIRST_SOLUTION)) {
          // Determinant is 5 and aK < 0. Use exceptional solution (U1-U2)/(V1-V2).
          BigIntSubt(&V1, &V2, &ValH);
          BigIntSubt(&U1, &U2, &ValI);
        } else {
          // Determinant is not 5 or aK > 0. Use convergent U1/V1 as solution.
          CopyBigInt(&ValH, &V1);
          CopyBigInt(&ValI, &U1);
          ShowSolutionFromConvergent(BigIntegerToBigInt(&ValH),
                                     BigIntegerToBigInt(&ValI));
        }
        // XXX replaced with two_solutions
        showSolution = TWO_SOLUTIONS;
        solFound = false;
        NonSquareDiscrSolution(true, value);
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
        CheckStartOfContinuedFractionPeriod();
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
      isIntegerPart = 0;
    }

    // Restore value.
    BigIntToBigInteger(Tmp11, value);
  }

  void ShowRecSol(char variable,
                  const BigInt &cx,
                  const BigInt &cy,
                  const BigInt &ci) {
    eLinearSolution t;
    ShowChar(variable);
    showText("<sub>n+1</sub> = ");
    t = Show(cx, "x<sub>n</sub>", SOLUTION_FOUND);
    t = Show(cy, "y<sub>n</sub>", t);
    Show1(ci, t);
  }

  void ShowResult(const char *text, const BigInt &value) {
    showText(text);
    showText(" = ");
    ShowBigInt(value);
    showText("<br>");
  }

  // TODO: Test this heuristic without converting.
  bool IsBig(const BigInt &bg, int num_limbs) {
    BigInteger tmp;
    BigIntToBigInteger(bg, &tmp);
    return tmp.nbrLimbs > num_limbs;
  }

  void ShowAllRecSols(
      // XXX original code modifies these; was that just a bug?
      BigInt P, BigInt Q,
      BigInt R, BigInt S,
      BigInt K, BigInt L,
      const BigInt &Alpha, const BigInt &Beta) {

    if (IsBig(P, 2) || IsBig(Q, 2)) {
      if (Alpha == 0 && Beta == 0) {
        showText("x<sub>n+1</sub> = P&nbsp;&#8290;x<sub>n</sub> + "
                 "Q&nbsp;&#8290;y<sub>n</sub><br>"
                 "y<sub>n+1</sub> = R&nbsp;&#8290;x<sub>n</sub> + "
                 "S&nbsp;&#8290;y<sub>n</sub></p><p>");
      } else {
        showText("x<sub>n+1</sub> = P&nbsp;&#8290;x<sub>n</sub> + "
                 "Q&nbsp;&#8290;y<sub>n</sub> + K<br>"
                 "y<sub>n+1</sub> = R&nbsp;&#8290;x<sub>n</sub> + "
                 "S&nbsp;&#8290;y<sub>n</sub> + L</p><p>");
      }
      showText("where:</p><p>");
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
      showText("<br>");
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

    showText("<p>and also:</p>");
    if (IsBig(P, 2) || IsBig(Q, 2)) {
      showText("<p>");
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
      showText("<br>");
      ShowRecSol('y', R, S, L);
    }
    showText("</p>");
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

    // printf("SFF CF\n");

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

    if (VERBOSE)
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

    showText("<p>Recursive solutions:</p><p>");

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

  void callbackQuadModHyperbolic(BigInteger *value) {
    bool isBeven = ((ValB.limbs[0].x & 1) == 0);
    positiveDenominator = 1;
    if (!PerformTransformation(value)) {
      // No solutions because gcd(P, Q, R) > 1.
      return;
    }
    // Compute P as floor((2*a*theta + b)/2)
    BigIntAdd(&ValA, &ValA, &ValP);
    (void)BigIntMultiply(&ValP, value, &ValP);
    BigIntAdd(&ValP, &ValB, &ValP);
    subtractdivide(&ValP, ValP.limbs[0].x & 1, 2);
    // Compute Q = a*abs(K)
    CopyBigInt(&ValQ, &ValK);
    ValQ.sign = SIGN_POSITIVE;
    (void)BigIntMultiply(&ValQ, &ValA, &ValQ);
    // Find U, V, L so we can compute the continued fraction
    // expansion of (U+sqrt(L))/V.
    CopyBigInt(&ValL, &discr);

    if (isBeven) {
      CopyBigInt(&ValU, &ValP);       // U <- P
      subtractdivide(&ValL, 0, 4);    // Argument of square root is discriminant/4.
      CopyBigInt(&ValV, &ValQ);
    } else {
      BigIntAdd(&ValP, &ValP, &ValU);
      addbigint(&ValU, 1);            // U <- 2P+1
      BigIntAdd(&ValQ, &ValQ, &ValV); // V <- 2Q
    }
    BigIntChSign(&ValU);
    // If L-U^2 is not multiple of V, there is no solution, so go out.
    (void)BigIntMultiply(&ValU, &ValU, &bigTmp);
    BigIntSubt(&ValL, &bigTmp, &bigTmp);
    (void)BigIntRemainder(&bigTmp, &ValV, &bigTmp);

    if (!BigIntIsZero(&bigTmp)) {
      if (teach) {
        showText("<p>There are no solutions of ");
        showEqNbr(equationNbr + 1);
        showText(" using the continued fraction of ");
        ShowArgumentContinuedFraction(
            !!positiveDenominator,
            BigIntegerToBigInt(&ValB),
            BigIntegerToBigInt(&ValL),
            BigIntegerToBigInt(&ValU),
            BigIntegerToBigInt(&ValV));

        showText(" because "
                 "<var>D</var> &minus; <var>Q</var>");
        showSquare();
        showText(" is not multiple of ");
        if ((ValB.limbs[0].x & 1) != 0) {
          // Odd discriminant.
          showText("2");
        }
        showText("<var>R</var></p>");
      }
      equationNbr += 2;
      return;
    }

    // Set G to floor(sqrt(L))
    SquareRoot(ValL.limbs, ValG.limbs, ValL.nbrLimbs, &ValG.nbrLimbs);
    ValG.sign = SIGN_POSITIVE;          // g <- sqrt(discr).
    Xplus.nbrLimbs = 0;                 // Invalidate solutions.
    Xminus.nbrLimbs = 0;
    Yplus.nbrLimbs = 0;
    Yminus.nbrLimbs = 0;

    BigInt UBak = BigIntegerToBigInt(&ValU);
    BigInt VBak = BigIntegerToBigInt(&ValV);
    contfracEqNbr = equationNbr + 2;
    ContFrac(value, FIRST_SOLUTION);    // Continued fraction of (U+G)/V
    positiveDenominator = 0;
    BigIntToBigInteger(UBak, &ValU);
    BigIntToBigInteger(VBak, &ValV);
    BigIntChSign(&ValU);
    BigIntChSign(&ValV);
    contfracEqNbr = equationNbr + 3;
    if ((ValU.limbs[0].x == 3) && (ValV.limbs[0].x == 9)) {
      contfracEqNbr++;
    }
    ContFrac(value, SECOND_SOLUTION);   // Continued fraction of (-U+G)/(-V)
    showSolution = ONE_SOLUTION;

    if (Xplus.nbrLimbs != 0) {
      startResultBox(SOLUTION_FOUND);
      ShowXYOne(BigIntegerToBigInt(&Xplus),
             BigIntegerToBigInt(&Yplus));
      endResultBox(SOLUTION_FOUND);
    }

    if (Xminus.nbrLimbs != 0) {
      startResultBox(SOLUTION_FOUND);
      ShowXYOne(BigIntegerToBigInt(&Xminus),
                BigIntegerToBigInt(&Yminus));
      endResultBox(SOLUTION_FOUND);
    }
    equationNbr += 4;
  }

  void PrintQuadEqConst(
      const BigInt &A, const BigInt &B, const BigInt &C,
      const BigInt &K,
      bool showEquationNbr) {
    showText("<p>");
    PrintQuad(A, B, C, "<var>X</var>", "<var>Y</var>");
    showText(" = ");
    ShowBigInt(K);
    showText(" ");
    if (showEquationNbr && K >= 0) {
      showEqNbr(1);
    }
    showText("</p>");
  }

  // Copy intentional; we modify them in place (factor out gcd).
  void SolveQuadEquation(BigInt a, BigInt b, BigInt c,
                         BigInt d, BigInt e, BigInt f) {
    also = 0;
    showSolution = ONE_SOLUTION;
    showRecursiveSolution = 0;    // Do not show recursive solution by default.
    divgcd = "<p>Dividing the equation by the greatest common divisor "
      "we obtain:</p>";

    BigInt gcd = BigInt::GCD(BigInt::GCD(a, b),
                             BigInt::GCD(BigInt::GCD(c, d),
                                         e));

    // PERF divisibility check
    if (gcd != 0 && BigInt::CMod(f, gcd) != 0) {
      // F is not multiple of GCD(A, B, C, D, E) so there are no solutions.
      if (teach) {
        showText("<p>The constant coefficient is not multiple of ");
        ShowBigInt(gcd);
        showText(", which is the greatest common divisor of the other "
                 "coefficients, so there are no solutions.</p>");
      } else {
        showText("<p>There are no solutions.</p>");
      }
      return;
    }

    // Divide all coefficients by GCD(A, B, C, D, E).
    // PERF: Known-divisible operation
    if (gcd != 0) {
      a /= gcd;
      b /= gcd;
      c /= gcd;
      d /= gcd;
      e /= gcd;
      f /= gcd;
    }

    if (VERBOSE)
      printf("After dividing: %s %s %s %s %s %s\n",
             a.ToString().c_str(),
             b.ToString().c_str(),
             c.ToString().c_str(),
             d.ToString().c_str(),
             e.ToString().c_str(),
             f.ToString().c_str());

    // Test whether the equation is linear. A = B = C = 0.
    if (a == 0 && b == 0 && c == 0) {
      eLinearSolution ret = LinearEq(d, e, f);
      startResultBox(ret);
      PrintLinear(ret, "t");
      endResultBox(ret);
      return;
    }

    // Compute discriminant: b^2 - 4ac.
    BigInt Discr = b * b - ((a * c) << 2);

    if (teach) {
      showText("<p>The discriminant is"
               " <var>b</var>");
      showSquare();
      showText("&nbsp;&minus;&nbsp;4&#8290;<var>a</var>&#8290;<var>c</var> = ");
      ShowBigInt(Discr);
      showText("</p>");
    }

    if (Discr == 0) {
      // Discriminant is zero.
      DiscriminantIsZero(a, b, c, d, e, f);
      return;
    }

    // XXX remove this state
    BigIntToBigInteger(a, &ValA);
    BigIntToBigInteger(b, &ValB);
    BigIntToBigInteger(c, &ValC);
    BigIntToBigInteger(d, &ValD);
    BigIntToBigInteger(e, &ValE);
    BigIntToBigInteger(f, &ValF);
    BigIntToBigInteger(Discr, &discr);

    // Compute gcd(a,b,c).
    BigIntGcd(&ValA, &ValB, &bigTmp);
    BigIntGcd(&bigTmp, &ValC, &U1);
    // Discriminant is not zero.
    if (BigIntIsZero(&ValD) && BigIntIsZero(&ValE)) {
      // Do not translate origin.
      intToBigInteger(&ValDiv, 1);
      CopyBigInt(&ValK, &ValF);
      BigIntChSign(&ValK);
      intToBigInteger(&ValAlpha, 0);
      intToBigInteger(&ValBeta, 0);
      if (teach) {
        PrintQuadEqConst(
            BigIntegerToBigInt(&ValA),
            BigIntegerToBigInt(&ValB),
            BigIntegerToBigInt(&ValC),
            BigIntegerToBigInt(&ValK),
            true);
      }
    } else {
      CopyBigInt(&ValDiv, &discr);
      // Translate the origin (x, y) by (alpha, beta).
      // Compute alpha = 2cd - be
      (void)BigIntMultiply(&ValC, &ValD, &ValAlpha);
      BigIntMultiplyBy2(&ValAlpha);
      (void)BigIntMultiply(&ValB, &ValE, &bigTmp);
      BigIntSubt(&ValAlpha, &bigTmp, &ValAlpha);
      // Compute beta = 2ae - bd
      (void)BigIntMultiply(&ValA, &ValE, &ValBeta);
      BigIntMultiplyBy2(&ValBeta);
      (void)BigIntMultiply(&ValB, &ValD, &bigTmp);
      BigIntSubt(&ValBeta, &bigTmp, &ValBeta);
      // We get the equation ax^2 + bxy + cy^2 = k
      // where k = -D (ae^2 - bed + cd^2 + fD)
      (void)BigIntMultiply(&ValA, &ValE, &ValK);     // ae
      (void)BigIntMultiply(&ValK, &ValE, &ValK);     // ae^2
      (void)BigIntMultiply(&ValB, &ValE, &bigTmp);   // be
      (void)BigIntMultiply(&bigTmp, &ValD, &bigTmp); // bed
      BigIntSubt(&ValK, &bigTmp, &ValK);        // ae^2 - bed
      (void)BigIntMultiply(&ValC, &ValD, &bigTmp);   // cd
      (void)BigIntMultiply(&bigTmp, &ValD, &bigTmp); // cd^2
      BigIntAdd(&ValK, &bigTmp, &ValK);              // ae^2 - bed + cd^2
      (void)BigIntMultiply(&ValF, &discr, &bigTmp);  // fD
      BigIntAdd(&ValK, &bigTmp, &ValK);              // ae^2 - bed + cd^2 + fD
      (void)BigIntMultiply(&ValK, &discr, &ValK);    // D (ae^2 - bed + cd^2 + fD)
      BigIntChSign(&ValK);                           // k
      if (teach) {
        showText("<p>Let <var>D</var> be the discriminant. "
                 "We apply the transformation of Legendre "
                 "<var>D</var><var>x</var> = <var>X</var> + <var>&alpha;</var>, "
                 "<var>D</var><var>y</var> = <var>Y</var> + <var>&beta;</var>, "
                 "and we obtain"
                 ":</p><p><var>&alpha;</var> = "
                 "2&#8290;<var>c</var>&#8290;<var>d</var> - "
                 "<var>b</var>&#8290;<var>e</var> = ");
        ShowNumber(&ValAlpha);
        showText("</p><p><var>&beta;</var> = "
                 "2&#8290;<var>a</var>&#8290;<var>e</var> - "
                 "<var>b</var>&#8290;<var>d</var> = ");
        ShowNumber(&ValBeta);
        showText("</p>");
        PrintQuadEqConst(
            BigIntegerToBigInt(&ValA),
            BigIntegerToBigInt(&ValB),
            BigIntegerToBigInt(&ValC),
            BigIntegerToBigInt(&ValK),
            BigIntIsOne(&U1));
        showText("<p>"
                 "where the right hand side equals "
                 "&minus;<var>D</var> (<var>a</var>&#8290;<var>e</var>");
        showSquare();
        showText(" &minus; <var>b</var>&#8290;<var>e</var>&#8290;<var>d</var> + "
                 "<var>c</var>&#8290;<var>d</var>");
        showSquare();
        showText(" + <var>f</var>&#8290;<var>D</var>)</p>");
        if (!BigIntIsOne(&U1)) {
          CopyBigInt(&ValABak, &ValA);
          CopyBigInt(&ValBBak, &ValB);
          CopyBigInt(&ValCBak, &ValC);
          CopyBigInt(&V1, &ValK);
          (void)BigIntDivide(&ValA, &U1, &ValA);
          (void)BigIntDivide(&ValB, &U1, &ValB);
          (void)BigIntDivide(&ValC, &U1, &ValC);
          (void)BigIntDivide(&ValK, &U1, &ValK);
          showText("<p>Dividing both sides by ");
          showLimbs(U1.limbs, U1.nbrLimbs);
          showText(":</p>");
          PrintQuadEqConst(
              BigIntegerToBigInt(&ValA),
              BigIntegerToBigInt(&ValB),
              BigIntegerToBigInt(&ValC),
              BigIntegerToBigInt(&ValK),
              true);
          CopyBigInt(&ValA, &ValABak);
          CopyBigInt(&ValB, &ValBBak);
          CopyBigInt(&ValC, &ValCBak);
          CopyBigInt(&ValK, &V1);
        }
      }
    }

    // If k is not multiple of gcd(A, B, C), there are no solutions.
    (void)BigIntRemainder(&ValK, &U1, &bigTmp);
    if (!BigIntIsZero(&bigTmp)) {
      if (teach) {
        showText("<p>The right hand side is not multiple of ");
        ShowNumber(&U1);
        showText(", which is the greatest common divisor of all three "
                 "coefficients, so there are no solutions");
      }
      return;     // There are no solutions.
    }

    if (ValK.sign == SIGN_NEGATIVE) {
      BigIntChSign(&ValA);
      BigIntChSign(&ValB);
      BigIntChSign(&ValC);
      BigIntChSign(&ValK);
      if (teach) {
        showText("<p>The algorithm requires the constant coefficient "
                 "to be positive, so we multiply both RHS and LHS by &minus;1.</p>");
        PrintQuadEqConst(
            BigIntegerToBigInt(&ValA),
            BigIntegerToBigInt(&ValB),
            BigIntegerToBigInt(&ValC),
            BigIntegerToBigInt(&ValK),
            true);
      }
    }

    if (discr.sign == SIGN_NEGATIVE) {
      NegativeDiscriminant();
      return;
    }

    SquareRoot(discr.limbs, ValG.limbs, discr.nbrLimbs, &ValG.nbrLimbs);
    ValG.sign = SIGN_POSITIVE;
    (void)BigIntMultiply(&ValG, &ValG, &bigTmp);
    if (BigIntEqual(&bigTmp, &discr)) {
      // Discriminant is a perfect square.
      PerfectSquareDiscriminant();
      return;
    }
    PositiveDiscriminant();
  }

  void QuadBigInt(const BigInt &a, const BigInt &b, const BigInt &c,
                  const BigInt &d, const BigInt &e, const BigInt &f) {
    showText("2<p>");

    showText("<h2>");
    ShowEq(a, b, c, d, e, f, "x", "y");
    showText(" = 0</h2>");
    SolNbr = 0;

    size_t preamble_size = (output == nullptr) ? 0 : output->size();

    SolveQuadEquation(a, b, c, d, e, f);

    if (output != nullptr && output->size() == preamble_size) {
      showText("<p>The equation does not have integer solutions.</p>");
    }
  }

  Quad() {
    MarkUninitialized();
  }

};


void QuadBigInt(bool t,
                const BigInt &a, const BigInt &b, const BigInt &c,
                const BigInt &d, const BigInt &e, const BigInt &f,
                std::string *output) {
  std::unique_ptr<Quad> quad(new Quad);
  quad->teach = t;
  quad->output = output;
  quad->QuadBigInt(a, b, c, d, e, f);
}
