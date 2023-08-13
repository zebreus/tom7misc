
#include "polynomial.h"

#include <string>

#include "base/logging.h"

#define CHECK_SEQ(a, b) do {                          \
  const std::string aa = (a), bb = (b);             \
  CHECK(aa == bb) << "Comparing " #a " and " #b "\n"  \
  << aa << "\nvs\n" << bb;                            \
  } while (false)

static void Simple() {
  Polynomial seven = 7_p;
  Polynomial x = "x"_p;

  {
    Polynomial p = x + seven;
    CHECK_SEQ(p.ToString(), "7 + x");
    p = p + p;
    CHECK_SEQ(p.ToString(), "14 + 2x");
  }

  {
    Polynomial p = x * x + seven * x;
    CHECK_SEQ(p.ToString(), "7x + x^2");

    Polynomial pp = Polynomial::PartialDerivative(p, "x");
    CHECK_SEQ(pp.ToString(), "7 + 2x");
  }

  {
    Polynomial p = x * x * x + seven;
    Polynomial pp = Polynomial::PartialDerivative(p, "x");
    CHECK_SEQ(pp.ToString(), "3x^2");

    // invariant in y, so the derivative is zero
    Polynomial ppy = Polynomial::PartialDerivative(p, "y");
    CHECK_SEQ(ppy.ToString(), "0");
  }
}

static void Canceling() {
  {
    Polynomial k("k", 3);
    Polynomial invk("k", -3);

    Polynomial one = k * invk;
    CHECK_SEQ(one.ToString(), "1");
  }

  {
    Polynomial m("m", 2);
    Polynomial negm = -m;

    Polynomial zero = m + negm;
    CHECK_SEQ(zero.ToString(), "0");
  }
}

int main(int argc, char **argv) {

  Simple();
  Canceling();

  printf("OK\n");
  return 0;
}
