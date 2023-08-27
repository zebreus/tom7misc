// Little standalone tool for manipulating some polynomials used
// in orbit.cc.

#include "polynomial.h"

#include <functional>
#include <string>

#include "ansi.h"
#include "base/logging.h"
#include "util.h"
#include "bignum/big.h"
#include "bhaskara-util.h"

using namespace std;

template<class F>
static BigInt BigEval(const Term &t, const F &f) {
  BigInt result(1);
  for (const auto &[x, e] : t.product) {
    CHECK(e >= 1) << "Should use a BigRat version for polynomials "
      "with negative powers.";
    BigInt v = f(x);
    result = BigInt::Times(result, BigInt::Pow(v, e));
  }
  return result;
}

template<class F>
static BigInt BigEval(const Polynomial &p, const F &f) {
  BigInt result(0);
  for (const auto &[t, c] : p.sum) {
    result = BigInt::Plus(result, BigInt::Times(BigEval(t, f), c));
  }
  return result;
}

// static string Id(string s) { return s; }
// static string Up(string s) { return Util::ucase(s); }

static string Id(string s) { return StringPrintf("%s1", s.c_str()); }
static string Up(string s) { return StringPrintf("%s2", s.c_str()); }

// s^2 -> (qr + 1)
static Polynomial SubstS2(const Polynomial &p) {
  return Polynomial::Subst(
      p,
      "s"_t * "s"_t,
      "q"_p * "r"_p + Polynomial{1});
}

// qr -> s^2 - 1
static Polynomial SubstQR(const Polynomial &p) {
  return Polynomial::Subst(p, "q"_t * "r"_t,
                           "s"_p * "s"_p - Polynomial{1});
}


[[maybe_unused]]
static void Minimize() {

  // (((a1 * m1 + b1) / k1  +  a1 * x1)^2 -
  //   ((a2 * m2 + b2) / k2  +  a2 * x2)^2)^2

  // This is the expression we want to minimize.
  // (a1'^2 - a2'^2)^2
  auto APrime = [](std::function<string(string)> Suffix) {
      Polynomial a(Suffix("a"));
      Polynomial m(Suffix("m"));
      Polynomial b(Suffix("b"));
      Polynomial x(Suffix("x"));
      Polynomial kinv(Suffix("k"), -1);

      return (a * m + b) * kinv  +  a * x;
    };

  Polynomial a1p = APrime(Id);
  Polynomial a2p = APrime(Up);

  // a1'^2
  Polynomial a1p_sq = a1p * a1p;
  Polynomial a2p_sq = a2p * a2p;

  Polynomial adiff = a1p_sq - a2p_sq;
  Polynomial adiff_sq = adiff * adiff;

  printf("Diff squared:\n%s\n",
         adiff_sq.ToString().c_str());

  // Now the k terms

  auto KPrime = [](std::function<string(string)> Suffix) {
      Polynomial m(Suffix("m"));
      Polynomial n(Suffix("n"));
      Polynomial x(Suffix("x"));
      Polynomial k(Suffix("k"));
      Polynomial kinv(Suffix("k"), -1);

      Polynomial mplusxk = m + x * k;
      return (mplusxk * mplusxk - n) * kinv;
    };

  auto k1p = KPrime(Id);
  auto k2p = KPrime(Up);

  Polynomial sum_sq = adiff_sq + k1p * k1p + k2p * k2p;

  printf("Full polynomial to minimize:\n%s\n",
         sum_sq.ToString().c_str());

  auto TermHas = [](const Term &term, const std::string &x) {
      for (const auto &tp : term.product) {
        if (tp.first == x) return true;
      }
      return false;
    };

  // Separate into terms that involve x and those that don't...
  std::string x1 = Id("x");
  std::string x2 = Up("x");
  Polynomial nox, yesx;
  for (const auto &[t, c] : sum_sq.sum) {
    if (TermHas(t, x1) || TermHas(t, x2)) {
      yesx = yesx + Polynomial(t, c);
    } else {
      nox = nox + Polynomial(t, c);
    }
  }

  printf("\n" ACYAN("Constant terms") ":\n%s\n"
         "\n" APURPLE("Dependent on x1,x2") ":\n%s\n",
         nox.ToString().c_str(),
         yesx.ToString().c_str());

  Polynomial dyes1 = Polynomial::PartialDerivative(yesx, x1);
  Polynomial dyes2 = Polynomial::PartialDerivative(yesx, x2);
  printf("\n" AYELLOW("d/dx1") ":\n%s\n"
         "\n" AGREEN("d/dx2") ":\n%s\n",
         dyes1.ToString().c_str(),
         dyes2.ToString().c_str());

  string code1 = Polynomial::ToCode("BigInt", dyes1);
  string code2 = Polynomial::ToCode("BigInt", dyes2);
  printf("\n"
         "// d/dx1:\n"
         "%s\n\n"
         "// d/dx2:\n"
         "%s\n\n",
         code1.c_str(),
         code2.c_str());
}

// With p,q,r,s unconstrained.
[[maybe_unused]]
static void Recur() {
  // BigInt b2 = pb + qc;
  // BigInt c2 = sc + rb;

  Polynomial p = "p"_p;
  Polynomial q = "q"_p;
  Polynomial r = "r"_p;
  Polynomial s = "s"_p;

  Polynomial b1 = p * "b0"_p + q * "c0"_p;
  Polynomial c1 = s * "c0"_p + r * "b0"_p;

  // BigInt b3 = pb - qc;
  // BigInt c3 = sc - rb;

  Polynomial b2 = p * b1 - q * c1;
  Polynomial c2 = s * c1 - r * b1;

  printf("Unconstrained pqrs.\n");
  printf("b2: %s\n"
         "c2: %s\n",
         b2.ToString().c_str(),
         c2.ToString().c_str());
}


// With p == s.
static void Recur2() {
  // BigInt b2 = pb + qc;
  // BigInt c2 = sc + rb;

  Polynomial p = "s"_p;
  Polynomial q = "q"_p;
  Polynomial r = "r"_p;
  Polynomial s = "s"_p;

  {
    Polynomial b1 = p * "b0"_p + q * "c0"_p;
    Polynomial c1 = s * "c0"_p + r * "b0"_p;

    // BigInt b3 = pb - qc;
    // BigInt c3 = sc - rb;

    Polynomial b2 = p * b1 - q * c1;
    Polynomial c2 = s * c1 - r * b1;

    printf("With p=s.\n");
    printf("b2: %s\n"
           "c2: %s\n",
           b2.ToString().c_str(),
           c2.ToString().c_str());
  }

  {
    Polynomial b1 = p * "b0"_p - q * "c0"_p;
    Polynomial c1 = s * "c0"_p - r * "b0"_p;

    Polynomial b2 = p * b1 + q * c1;
    Polynomial c2 = s * c1 + r * b1;

    printf("And backwards:\n");
    printf("b2: %s\n"
           "c2: %s\n",
           b2.ToString().c_str(),
           c2.ToString().c_str());

  }
}

static void PrintIters() {
  Polynomial p = "s"_p;
  Polynomial q = "q"_p;
  Polynomial r = "r"_p;
  Polynomial s = "s"_p;

  Polynomial b = "b"_p;
  Polynomial c = "c"_p;

  static constexpr int MAX_ITERS = 32;
  for (int i = 0; i < MAX_ITERS; i++) {
    Polynomial b1 = p * b + q * c;
    Polynomial c1 = s * c + r * b;

    /*
    b1 = Polynomial::Subst(b1, "s"_t * "s"_t,
                           q * r + Polynomial{1});
    c1 = Polynomial::Subst(c1, "s"_t * "s"_t,
                           q * r + Polynomial{1});
    */

    b1 = Polynomial::Subst(b1, "q"_t * "r"_t,
                           s * s - Polynomial{1});
    c1 = Polynomial::Subst(c1, "q"_t * "r"_t,
                           s * s - Polynomial{1});

    printf("f^%d(b, c) =\n"
           "b': %s\n"
           "c': %s\n",
           i + 1,
           b1.ToString().c_str(),
           c1.ToString().c_str());

    b = std::move(b1);
    c = std::move(c1);
  }
}

template<int SUBST>
static std::pair<Polynomial, Polynomial> GetIter(int n) {
  Polynomial p = "s"_p;
  Polynomial q = "q"_p;
  Polynomial r = "r"_p;
  Polynomial s = "s"_p;

  Polynomial b = "b"_p;
  Polynomial c = "c"_p;

  for (int i = 0; i < n; i++) {
    Polynomial b1 = p * b + q * c;
    Polynomial c1 = s * c + r * b;


    if constexpr (SUBST == 1) {
      b1 = Polynomial::Subst(b1, "s"_t * "s"_t,
                             q * r + Polynomial{1});
      c1 = Polynomial::Subst(c1, "s"_t * "s"_t,
                             q * r + Polynomial{1});
    } else if (SUBST == 2) {
      b1 = Polynomial::Subst(b1, "q"_t * "r"_t,
                             s * s - Polynomial{1});
      c1 = Polynomial::Subst(c1, "q"_t * "r"_t,
                             s * s - Polynomial{1});
    }

    b = std::move(b1);
    c = std::move(c1);
  }

  return make_pair(b, c);
}

static Polynomial ManualC(int n) {
  // note s^2 = (qr + 1)
  // c_n+2 = 2sc_n+1 - s^2c_n + qrc_n
  // c_n = 2sc_n-1 - s^2c_n-2 + qrc_n-2
  // c_n = 2sc_n-1 - (s^2c_n-2 - qrc_n-2)
  // c_n = 2sc_n-1 - (s^2 - qr)c_n-2
  // c_n = 2sc_n-1 - ((qr+1) - qr)c_n-2     (because s^2 = qr+1)
  // c_n = 2sc_n-1 - (1)c_n-2     (because s^2 = qr+1)
  // c_n = 2sc_n-1 - c_n-2
  Polynomial s = "s"_p;
  if (n == 0) return "c"_p;
  if (n == 1) return "s"_p * "c"_p + "r"_p * "b"_p;
  // PERF iterate or memoize!
  Polynomial nm1 = ManualC(n - 1);
  Polynomial nm2 = ManualC(n - 2);
  // return 2 * s * nm1 - (s * s * nm2) + ("q"_p * "r"_p * nm2);
  return 2 * s * nm1 - nm2;
}

static Polynomial ManualB(int n) {
  // b_n+2 = 2sb_n+1 - s^2b_n + qrb_n
  // b_n = 2sb_n-1 - s^2b_n-2 + qrb_n-2
  Polynomial s = "s"_p;
  if (n == 0) return "b"_p;
  if (n == 1) return "s"_p * "b"_p + "q"_p * "c"_p;
  // PERF as above!
  Polynomial nm1 = ManualB(n - 1);
  Polynomial nm2 = ManualB(n - 2);
  // return 2 * s * nm1 - (s * s * nm2) + ("q"_p * "r"_p * nm2);
  // (as above)
  return 2 * s * nm1 - nm2;
}

static std::pair<Polynomial, Polynomial> Manual(int n) {
  return std::make_pair(ManualB(n),
                        ManualC(n));
}

static void Compare() {
  const int n = 5;

  const auto [bi, ci] = GetIter<1>(n);
  const auto [bc, cc] = Manual(n);

  Polynomial bs = SubstS2(bc);
  Polynomial cs = SubstS2(cc);

  Polynomial bq = SubstQR(bc);
  Polynomial cq = SubstQR(cc);

  // reference values
  string sbi = bi.ToString();
  string sci = ci.ToString();

  auto GreenB = [&sbi](string s) {
      if (s == sbi) return StringPrintf(AGREEN("%s"), s.c_str());
      else return s;
    };

  auto GreenC = [&sci](string s) {
      if (s == sci) return StringPrintf(AGREEN("%s"), s.c_str());
      else return s;
    };

  string sbc = GreenB(bc.ToString());
  string scc = GreenC(cc.ToString());
  string sbs = GreenB(bs.ToString());
  string scs = GreenC(cs.ToString());
  string sbq = GreenB(bq.ToString());
  string scq = GreenC(cq.ToString());


  printf("Iter:\n"
         "b(%d): " AGREEN("%s") "\n"
         "c(%d): " AGREEN("%s") "\n"
         "Closed:\n"
         "b(%d): %s\n"
         "c(%d): %s\n"
         "Closed-subst (for s^2):\n"
         "b(%d): %s\n"
         "c(%d): %s\n"
         "Closed-subst (for qr):\n"
         "b(%d): %s\n"
         "c(%d): %s\n" ,
         n, sbi.c_str(),
         n, sci.c_str(),
         n, sbc.c_str(),
         n, scc.c_str(),
         n, sbs.c_str(),
         n, scs.c_str(),
         n, sbq.c_str(),
         n, scq.c_str());

}

void Eval() {
  //
  auto F = [](const std::string &x) {
      if (x == "c") return BigInt{40};
      if (x == "r") return BigInt{22};
      if (x == "q") return BigInt{24};
      if (x == "s") return BigInt{23};
      if (x == "b") return BigInt{28};
      if (x == "p") return BigInt{23};
      CHECK(false) << "unbound " << x;
    };

  CHECK(BigInt::Eq(F("p"), F("s")));
  CHECK(BigInt::Eq(BigInt::Times(F("s"), F("s")),
                   BigInt::Plus(BigInt{1},
                                BigInt::Times(F("q"), F("r")))));

  for (int n = 0; n < 10; n++) {
    const auto [bi, ci] = GetIter<0>(n);
    const auto [bc, cc] = Manual(n);

    BigInt x0 = BigEval(bi, F);
    BigInt y0 = BigEval(ci, F);

    BigInt x1 = BigEval(bc, F);
    BigInt y1 = BigEval(cc, F);

    printf("--- %d ---\n", n);
    printf("%s vs %s\n", LongNum(x0).c_str(), LongNum(x1).c_str());
    printf("%s vs %s\n", LongNum(y0).c_str(), LongNum(y1).c_str());

    CHECK(BigInt::Eq(x0, x1));
    CHECK(BigInt::Eq(y0, y1));
  }
}

static void Ellipse() {
  BigInt a{3};
  BigInt b{7};

  BigInt aa = a * a;
  BigInt ab = a * b;
  BigInt bb = b * b;

  BigInt x = -2 * ab;
  BigInt y = 138600 * aa - bb;
  BigInt z = 138600 * aa + bb;

  // x = -2ab
  // y = 138600 a^2 - b^2
  // z = 138600 a^2 + b^2


  printf("x: %s\n"
         "y: %s\n"
         "z: %s\n",
         x.ToString().c_str(),
         y.ToString().c_str(),
         z.ToString().c_str());

  BigInt xx = x * x;
  BigInt yy = y * y;
  BigInt zz = z * z;

  printf("x^2: %s\n"
         "138600 * x^2: %s\n"
         "y^2: %s\n"
         "z^2: %s\n"
         "138600x^2 + y^2: %s\n",
         xx.ToString().c_str(),
         (138600 * xx).ToString().c_str(),
         yy.ToString().c_str(),
         zz.ToString().c_str(),
         (138600 * xx + yy).ToString().c_str());

  CHECK(138600 * xx + yy == zz) <<
    ((138600 * xx + yy) - zz).ToString();
}

static void EllipseP() {
  Polynomial x = -2 * "a"_p * "b"_p;
  Polynomial y = 138600 * Polynomial("a", 2) - Polynomial("b", 2);
  Polynomial z = 138600 * Polynomial("a", 2) + Polynomial("b", 2);

  Polynomial lhs = 138600 * x * x + y * y;
  Polynomial rhs = z * z;

  printf("%s =\n"
         "%s\n", lhs.ToString().c_str(), rhs.ToString().c_str());
}

static void FormulaK() {
  Polynomial x = -2 * "a"_p * "b"_p;
  Polynomial y = 138600 * Polynomial("a", 2) - Polynomial("b", 2);
  Polynomial z = 138600 * Polynomial("a", 2) + Polynomial("b", 2);

  Polynomial xx = x * x;

  // k(a, b) = 222121 (-2ab)^2 - (138600 a^2 - b^2)^2
  // k(a, b) = 360721 (-2ab)^2 - (138600 a^2 + b^2)^2

  Polynomial k1 = 222121 * xx - y * y;
  Polynomial k2 = 360721 * xx - z * z;

  printf("k1: %s\n"
         "k2: %s\n",
         k1.ToString().c_str(),
         k2.ToString().c_str());
}

int main(int argc, char **argv) {
  ANSI::Init();

  EllipseP();
  Ellipse();

  FormulaK();

  // (void)Minimize();

  // Recur();
  // Recur2();

  printf("----\n");
  // Iter();

  // Closed();
  // Compare();

  // Eval();

  return 0;
}
