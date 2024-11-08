
#include "arcfour.h"
#include "randutil.h"
#include "image.h"

#include "auto-histo.h"

static void Plot() {
  // The proposition is that the following two functions produce
  // the same distribution:
  //   Sqrt(Uniform())
  //   Max(Uniform(), Uniform())

  // Sqrt(Uniform()) = Max(Uniform(), Uniform())
  // Uniform() = Square(Max(Uniform(), Uniform()))

  ArcFour rc("rand");

  AutoHisto histo;

  for (int i = 0; i < 1'000'000'000; i++) {
    const double a = RandDoubleNot1(&rc);
    const double b = RandDoubleNot1(&rc);

    const double m = std::max(a, b);

    const double sample = m * m;
    CHECK(sample < 1.0);
    CHECK(sample >= 0.0);

    histo.Observe(sample);

    if (i % 1'000'000 == 0) {
      printf("%d\n", i);
    }
  }

  printf("Result:\n%s\n", histo.SimpleHorizANSI(30).c_str());
  printf("Result:\n%s\n", histo.SimpleAsciiString(50).c_str());
}

int main(int argc, char **argv) {
  Plot();
  return 0;
}
