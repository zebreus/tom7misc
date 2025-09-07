
#include "symmetric-matrix.h"

#include <cstddef>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"

static void Test() {
  SymmetricMatrix<int> m(5, 7);
  for (size_t r = 0; r < m.Height(); r++) {
    for (size_t c = 0; c < m.Width(); c++) {
      CHECK(m.At(r, c) == 7);
    }
  }

  m.At(2, 1) = 8;
  CHECK(m.At(2, 1) == 8);
  CHECK(m.At(1, 2) == 8);

  for (size_t r = 0; r < m.Height(); r++) {
    for (size_t c = 0; c < m.Width(); c++) {
      if ((r == 2 && c == 1) || (r == 1 && c ==2))
        continue;
      CHECK(m.At(r, c) == 7);
    }
  }

}

int main(int argc, char **argv) {
  ANSI::Init();

  Test();

  Print("OK\n");
  return 0;
}
