
#include <cstddef>
#include <cstdio>

#include "base/print.h"
#include "nd-solutions.h"

static constexpr int TARGET_SAMPLES = 1'000'000;

int main(int argc, char **argv) {
  NDSolutions<6> dst("456b29ba-45202bba.nds");
  NDSolutions<6> src("456b29ba-45202bba.nds.1");

  Print("dst: {}\n", dst.Size());
  Print("src: {}\n", src.Size());

  size_t idx = 0;
  while (idx < src.Size() && dst.Size() < TARGET_SAMPLES) {
    const auto &[key, score, outer, inner] = src[idx];
    dst.Add(key, score, outer, inner);
  }

  Print("OK, now have size {}\n", dst.Size());

  dst.Save();

  return 0;
}
