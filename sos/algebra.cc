// Little standalone tool for manipulating some polynomials used
// in orbit.cc.

#include "polynomial.h"

#include <functional>
#include <string>

#include "ansi.h"
#include "base/logging.h"
#include "util.h"

using namespace std;

// static string Id(string s) { return s; }
// static string Up(string s) { return Util::ucase(s); }

static string Id(string s) { return StringPrintf("%s1", s.c_str()); }
static string Up(string s) { return StringPrintf("%s2", s.c_str()); }

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


int main(int argc, char **argv) {
  ANSI::Init();

  Minimize();

  return 0;
}
