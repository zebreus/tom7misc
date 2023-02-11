
#include "timer.h"
#include "fluint8.h"

static void BenchEq() {
  Timer timer;

  int64 comparisons = 0;
  int out = 0;
  for (int iters = 0; iters < 1000; iters++) {
    for (int i = 0; i < 256; i++) {
      Fluint8 ii(i);
      for (int j = 0; j < 256; j++) {
        Fluint8 jj(j);

        Fluint8 c = Fluint8::Eq(ii, jj);
        comparisons++;
        out += c.ToInt();
      }
    }
    if (iters % 100 == 0) printf(".");
  }
  printf("\n");

  double sec = timer.Seconds();
  printf("Eq: %lld comparisons in %.3fs = %.2f/sec\n",
         comparisons, sec, comparisons / sec);
}

int main(int argc, char **argv) {

  BenchEq();

  return 0;
}
