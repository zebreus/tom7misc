
#include <cstdint>
#include <cstdio>

#include "ansi.h"
#include "timer.h"

#include "base/do-not-optimize.h"
#include "byteset.h"

static constexpr int OUTER = 10000;
static constexpr int INNER = 1'000'000;

static void BenchSizeSIMD() {
  double total_sec = 0.0;
  for (int i = 0; i < OUTER; i++) {
    ByteSet s;
    s.Add(i);
    s.Add(i * 3);
    s.Add(i * 129 + 11);

    Timer timer;
    for (int j = 0; j < INNER; j++) {
      DoNotOptimize(s);
      int size = s.Size();
      DoNotOptimize(size);
    }
    total_sec += timer.Seconds();
  }

  printf("Total time (SIMD): %s\n",
         ANSI::Time(total_sec).c_str());
}

static void BenchSizeASM1() {
  double total_sec = 0.0;
  for (int i = 0; i < OUTER; i++) {
    ByteSet s;
    s.Add(i);
    s.Add(i * 3);
    s.Add(i * 129 + 11);

    Timer timer;
    for (int j = 0; j < INNER; j++) {
      DoNotOptimize(s);
      int size = s.SizeASM1();
      DoNotOptimize(size);
    }
    total_sec += timer.Seconds();
  }

  printf("Total time (ASM): %s\n",
         ANSI::Time(total_sec).c_str());
}

static void BenchSizeASM2() {
  double total_sec = 0.0;
  for (int i = 0; i < OUTER; i++) {
    ByteSet s;
    s.Add(i);
    s.Add(i * 3);
    s.Add(i * 129 + 11);

    Timer timer;
    for (int j = 0; j < INNER; j++) {
      DoNotOptimize(s);
      int size = s.SizeASM2();
      DoNotOptimize(size);
    }
    total_sec += timer.Seconds();
  }

  printf("Total time (ASM): %s\n",
         ANSI::Time(total_sec).c_str());
}

int main(int argc, char **argv) {
  ANSI::Init();

  printf("\nBenchmarking %lld Size calls:\n",
         int64_t(OUTER) * int64_t(INNER));
  BenchSizeSIMD();
  BenchSizeASM1();
  BenchSizeASM2();

  printf("OK\n");
  return 0;
}
