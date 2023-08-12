
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
  }
}

int main(int argc, char **argv) {

  Simple();

  printf("OK\n");
  return 0;
}
