
#include <cstdio>
#include <bit>

#include "base/logging.h"

int main(int argc, char **argv) {
  CHECK(argc == 2);

  int size = atoi(argv[1]);
  CHECK(size > 0);

  CHECK(std::has_single_bit<uint32_t>(size)) <<
    "size must be a power of two";
  const int power = std::countr_zero<uint32_t>(size);
  CHECK((1 << power) == size);

  // There is a constant for each position in the permutation.

  for (int i = 0; i < size; i++) {
    printf("(declare-const p%d (_ BitVec %d))\n", i, power);
  }

  // Because these are bit vectors of the appropriate size, they
  // are in range by definition. But they must all be different
  // for this to be a proper permutation. Generate this constraint
  // when i < j:

  #if 0
  for (int i = 0; i < size; i++) {
    for (int j = 0; j < size; j++) {
      if (i < j) {
        printf("(assert (not (= p%d p%d)))\n", i, j);
      }
    }
  }
  #endif

  // There is no builtin popcount, so we define one.
  printf("(define-fun HasCorrectPopcnt ((x (_ BitVec %d))) Bool\n"
         "  (or\n", power);
  for (int x = 0; x < size; x++) {
    if (std::popcount<uint32_t>(x) == power / 2)
      printf("    (= x (_ bv%d %d))\n", x, power);
  }
  printf("  ))\n");


  printf("\n");
  // Now the constraints for the strict avalanche criteria.
  for (int idx = 0; idx < size; idx++) {
    for (int bit = 0; bit < power; bit++) {
      // Flip the one bit.
      const int oidx = idx ^ (1 << bit);

      printf("(assert (HasCorrectPopcnt (bvxor p%d p%d)))\n",
             idx, oidx);
    }
  }

  printf("\n"
         "(check-sat)\n"
         "(get-model)\n");

  return 0;
}
