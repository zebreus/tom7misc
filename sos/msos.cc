#include <cmath>
#include <memory>
#include <vector>
#include <functional>
#include <string>
#include <bit>

#include "clutil.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "arcfour.h"
#include "randutil.h"
#include "threadutil.h"
#include "util.h"
#include "periodically.h"
#include "timer.h"
#include "ansi.h"

using namespace std;

static CL *cl = nullptr;

using int64 = int64_t;

// https://www.nuprl.org/MathLibrary/integer_sqrt/
static uint64_t Sqrt64(uint64_t xx) {
  if (xx <= 1) return xx;
  // z = xx / 4
  uint64_t z = xx >> 2;
  uint64_t r2 = 2 * Sqrt64(z);
  uint64_t r3 = r2 + 1;
  return (xx < r3 * r3) ? r2 : r3;
}

// Maybe a simpler sieve would find numbers that can be written
// as a sum of two squares in more than one way. After we pick
// the top row A, B, C, we know the sum, and only sums where
// SUM - A, SUM - B, and SUM - C are in this set are even workable.
// (Or put another way, A + B, A + C, and B + C must be in the set.)
//
// The reason why they need to be able to be written in two ways
// is that since A + B + C = SUM = A + D + G, it must be that
// B + C = D + G, and these four numbers must be distinct.
// (We actually need THREE ways due to the diagonal!)

// XXX start with sum = a^2 + b^2, with a < b.
// wlog we can also say that a is the smallest of a,b,c,d. So
// Then loop from c to the square root of the sum, and check
// whether sum - c^2 is a square. since a < c, we also know that
// all four are distinct. So if this is true for any c,d then we
// know sum has the target property. This has to do like
// 2^32 * 2^32 * 2^32 operations, though!!

// Some useful facts:
//  OEIS https://oeis.org/A004431
//    distinct nonzero squares is exactly this.

//  https://proofwiki.org/wiki/Sum_of_2_Squares_in_2_Distinct_Ways
//    if m and n are in the set, then m*n is in the set (except
//    possibly for some equivalences like a = b).

static void DoubleSums(uint32_t high_word) {
  string defines = StringPrintf("#define SUM_HI 0x%x\n", high_word);
  string kernel_src = defines + Util::ReadFile("doublesums.cl");

  auto [program, kernel] = cl->BuildOneKernel(kernel_src, "SieveG");
  CHECK(kernel != 0);

  // Allocate output bitmask.
  static constexpr int BYTES = 1 << (32 - 3);
  printf("%d bytes = %d Mb\n", BYTES, BYTES / (1024 * 1024));
  cl_mem sieved_gpu = CreateUninitializedGPUMemory<uint8_t>(cl->context, BYTES);

  CHECK_SUCCESS(clSetKernelArg(kernel, 0, sizeof (cl_mem),
                               (void *)&sieved_gpu));
}

static void SumOfSquaresTwice(uint32_t a, uint32_t b, uint32_t c) {
  uint64_t aa = (uint64_t)a * (uint64_t)a;
  uint64_t bb = (uint64_t)b * (uint64_t)b;
  uint64_t cc = (uint64_t)c * (uint64_t)c;
  uint64_t sum = aa + bb + cc;
  string defines = StringPrintf("#define A 0x%x\n"
                                "#define B 0x%x\n"
                                "#define C 0x%x\n"
                                "\n",
                                a, b, c);
  string kernel_src = defines + Util::ReadFile("sos.cl");
  auto [program, kernel] = cl->BuildOneKernel(kernel_src, "SieveG");
  CHECK(kernel != 0);

  // Allocate output bitmask.
  static constexpr int BYTES = 1 << (32 - 3);
  // static constexpr int BYTES = 10000000;
  printf("%d bytes = %d Mb\n", BYTES, BYTES / (1024 * 1024));
  cl_mem sieved_gpu = CreateUninitializedGPUMemory<uint8_t>(cl->context, BYTES);

  CHECK_SUCCESS(clSetKernelArg(kernel, 0, sizeof (cl_mem),
                               (void *)&sieved_gpu));

  static constexpr int BATCH_SIZE = BYTES >> 6;
  static constexpr int NUM_BATCHES = 1 << 6;
  for (int batch = 0; batch < NUM_BATCHES; batch++) {
    size_t global_work_offset[] = { (size_t)(batch * BATCH_SIZE), };
    size_t global_work_size[] = { BATCH_SIZE, };
    CHECK_SUCCESS(
        clEnqueueNDRangeKernel(cl->queue, kernel,
                               // 1D
                               1,
                               // It does its own indexing
                               global_work_offset,
                               global_work_size,
                               // No local work
                               nullptr,
                               // No wait list
                               0, nullptr,
                               // no event
                               nullptr));
    clFinish(cl->queue);
    printf(".");
  }

  printf("Kernel done.\n");
  std::vector<uint8_t> sieved =
    CopyBufferFromGPU<uint8_t>(cl->queue, sieved_gpu, BYTES);
  printf("Copy done.\n");

  CHECK_SUCCESS(clReleaseMemObject(sieved_gpu));

  int64_t num_ones = 0, num_nonzero = 0;
  for (int dhi = 0; dhi < sieved.size(); dhi++) {
    uint8_t byte = sieved[dhi];
    if (byte) {
      num_nonzero++;
      num_ones += std::popcount<uint8_t>(byte);
      for (int bit = 0; bit < 8; bit++) {
        int dlo = (7 - bit);
        if (byte & (1 << bit)) {
          uint32_t d = (dhi << 3) | dlo;
          // printf("0x%x\n", d);
          uint64_t dd = (uint64_t)d * (uint64_t)d;
          // Does there exist g such that aa + dd + gg = SUM?
          uint64_t gg = sum - aa - dd;
          uint64_t g = Sqrt64(gg);
          CHECK(g * g == gg) << g << " squared should be " << gg
                             << " but it is " << (g * g);
          // plus a each time
          if (num_ones < 32) {
            CHECK(aa + bb + cc == aa + dd + gg);
            printf("%u^2 + %u^2 = %u^2 + %u^2 = %llu\n",
                   b, c, d, (uint32_t)g, sum - aa);
          }
        }
      }
    }
  }
  printf("There are %lld valid entries; %lld nonzero bytes\n",
         num_ones, num_nonzero);
}


static void DoSearch(uint32_t a, uint32_t b, uint32_t c) {
  uint64_t aa = (uint64_t)a * (uint64_t)a;
  uint64_t bb = (uint64_t)b * (uint64_t)b;
  uint64_t cc = (uint64_t)c * (uint64_t)c;
  uint64_t sum = aa + bb + cc;
  string defines = StringPrintf("#define A 0x%x\n"
                                "#define B 0x%x\n"
                                "#define C 0x%x\n"
                                "\n",
                                a, b, c);
  string kernel_src = defines + Util::ReadFile("sos.cl");
  auto [program, kernel] = cl->BuildOneKernel(kernel_src, "SieveG");
  CHECK(kernel != 0);

  // Allocate output bitmask.
  static constexpr int BYTES = 1 << (32 - 3);
  // static constexpr int BYTES = 10000000;
  printf("%d bytes = %d Mb\n", BYTES, BYTES / (1024 * 1024));
  cl_mem sieved_gpu = CreateUninitializedGPUMemory<uint8_t>(cl->context, BYTES);

  CHECK_SUCCESS(clSetKernelArg(kernel, 0, sizeof (cl_mem),
                               (void *)&sieved_gpu));

  static constexpr int BATCH_SIZE = BYTES >> 6;
  static constexpr int NUM_BATCHES = 1 << 6;
  for (int batch = 0; batch < NUM_BATCHES; batch++) {
    size_t global_work_offset[] = { (size_t)(batch * BATCH_SIZE), };
    size_t global_work_size[] = { BATCH_SIZE, };
    CHECK_SUCCESS(
        clEnqueueNDRangeKernel(cl->queue, kernel,
                               // 1D
                               1,
                               // It does its own indexing
                               global_work_offset,
                               global_work_size,
                               // No local work
                               nullptr,
                               // No wait list
                               0, nullptr,
                               // no event
                               nullptr));
    clFinish(cl->queue);
    printf(".");
  }

  printf("Kernel done.\n");
  std::vector<uint8_t> sieved =
    CopyBufferFromGPU<uint8_t>(cl->queue, sieved_gpu, BYTES);
  printf("Copy done.\n");

  CHECK_SUCCESS(clReleaseMemObject(sieved_gpu));

  int64_t num_ones = 0, num_nonzero = 0;
  for (int dhi = 0; dhi < sieved.size(); dhi++) {
    uint8_t byte = sieved[dhi];
    if (byte) {
      num_nonzero++;
      num_ones += std::popcount<uint8_t>(byte);
      for (int bit = 0; bit < 8; bit++) {
        int dlo = (7 - bit);
        if (byte & (1 << bit)) {
          uint32_t d = (dhi << 3) | dlo;
          // printf("0x%x\n", d);
          uint64_t dd = (uint64_t)d * (uint64_t)d;
          // Does there exist g such that aa + dd + gg = SUM?
          uint64_t gg = sum - aa - dd;
          uint64_t g = Sqrt64(gg);
          CHECK(g * g == gg) << g << " squared should be " << gg
                             << " but it is " << (g * g);
          // plus a each time
          if (num_ones < 32) {
            CHECK(aa + bb + cc == aa + dd + gg);
            printf("%u^2 + %u^2 = %u^2 + %u^2 = %llu\n",
                   b, c, d, (uint32_t)g, sum - aa);
          }
        }
      }
    }
  }
  printf("There are %lld valid entries; %lld nonzero bytes\n",
         num_ones, num_nonzero);
}

int main(int argc, char **argv) {
  AnsiInit();
  cl = new CL;
  // DoSearch(0x0BADBEEF, 0x0123456, 0x0777777);
  DoSearch(29, 47, 1);
  printf(AGREEN("OK") "\n");
  return 0;
}

