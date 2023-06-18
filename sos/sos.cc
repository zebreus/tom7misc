#include <cmath>
#include <memory>
#include <vector>
#include <functional>
#include <string>
#include <bit>
#include <tuple>
#include <unordered_map>

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

static optional<tuple<uint64_t,uint64_t,uint64_t,uint64_t>>
ReferenceValidate2(uint64_t sum) {
  // Easy to see that there's no need to search beyond this.
  uint64_t limit = Sqrt64(sum);
  while (limit * limit < sum) limit++;

  // with x^2 + y^2 == sum and x <= y, get y.
  // (Or return zero)
  auto GetOther = [limit, sum](uint64_t x) -> uint64_t {
      uint64_t xx = x * x;
      for (uint64_t y = x; y <= limit; y++) {
        const uint64_t yy = y * y;
        if (xx + yy == sum) {
          return yy;
        } else if (xx + yy > sum) {
          return 0;
        }
      }
      return 0;
    };

  for (uint64_t a = 0; a <= limit; a++) {
    if (uint64_t b = GetOther(a)) {
      for (uint64_t c = a + 1; c <= limit; c++) {
        if (c != b) {
          if (uint64_t d = GetOther(c)) {
            return make_tuple(a, b, c, d);
          }
        }
      }
    }
  }
  return nullopt;
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
//  Must be in OEIS https://oeis.org/A004431, sum of two distinct squares.
//  OEIS https://oeis.org/A000161 gives the count of ways, so if
//  A000161[n] >= 2, it is in this set.
//
//
//
//  https://proofwiki.org/wiki/Sum_of_2_Squares_in_2_Distinct_Ways
//    if m and n are in the set, then m*n is in the set (except
//    possibly for some equivalences like a = b).
// https://users.rowan.edu/~hassen/Papers/SUM%20OF%20TWO%20SQUARES%20IN%20MORE%20THAN%20ONE%20WAY.pdf
//    If it's in the set, then it is the product of sums of squares.
#if 0
static void TestFormula() {
  for (int sum = 0; sum < 200; sum++) {
    std::vector<int> factors = Util::Factorize(sum);

    auto Delta = [](uint64_t n) {
        uint64_t nsqrt = Sqrt64(n);
        return nsqrt * nsqrt == n ? 1 : 0;
      };

    int deltas = Delta(sum) + (sum & 1) ? 0 : Delta(sum >> 1);

    // f(n) = the number of divisors of n that are congruent to 1 modulo 4 minus the number of its divisors that are congruent to 3 modulo 4
    int residue1 = 0, residue3 = 0;
    for (int factor : factors) {
      switch (factor % 4) {
      default:
      case 0:
      case 2:
        break;
      case 1: residue1++; break;
      case 3: residue3++; break;
      }
    };
    int f_n = residue1 - residue3;
    /*
    define delta(n) to be 1 if n is a perfect square and 0 otherwise. Then a(n)=1/2 (f(n)+delta(n)+delta(1/2 n))
    */

    // bit_length is the number of bits to represent the value.
    // can be done with clz

    // int(not any(e&1 for e in f.values())) + m +

    // p^e are the prime factors.
    //  m = the product
    //     1  if p==2
    //     p mod 4 == 1 ? e + 1 else (e+1) & 1
    //  if
    //
    // (~n & n-1).bit_length()
    // ((((~n & n-1).bit_length()&1)<<1)-1 if m&1 else 0)

    // )>>1) if n else 1 # Chai Wah Wu, Sep 08 2022

}
}
#endif

static int ChaiWahWu(uint64_t sum) {
  if (sum == 0) return 1;
  std::vector<int> factors = Util::Factorize(sum);
  std::unordered_map<int, int> collated;
  for (int f : factors) collated[f]++;

  auto HasAnyOddPowers = [&collated]() {
      for (const auto &[p, e] : collated) {
        if (e & 1) return true;
      }
      return false;
    };

  int first = HasAnyOddPowers() ? 0 : 1;

  int m = 1;
  for (const auto &[p, e] : collated) {
    if (p != 2) {
      m *= (p % 4 == 1) ? e + 1 : ((e + 1) & 1);
    }
  }

  // ((((~n & n-1).bit_length()&1)<<1)-1 if m&1 else 0)
  int b = 0;
  if (m & 1) {
    int bits = std::bit_width<uint64_t>(~sum & (sum - 1));
    b = ((bits & 1) << 1) - 1;
  }

  return first + ((m + b) >> 1);

  /*
    return int(not any(e&1 for e in f.values())) + (((m:=prod(1 if p==2 else (e+1 if p&3==1 else (e+1)&1) for p, e in f.items()))+((((~n & n-1).bit_length()&1)<<1)-1 if m&1 else 0))>>1) if n else 1
  */
}

static void TestCWW() {
  for (int i = 0; i < 1000; i++) {
    int num = ChaiWahWu(i);
    auto fo = ReferenceValidate2(i);

    bool correct = fo.has_value() ? num >= 2 : num < 2;
    bool print = !correct || fo.has_value();
    if (print) {
      printf("%s%d: %d%s", correct ? "" : ANSI_RED, i, num,
             correct ? "" : ANSI_RESET);
    }

    if (fo.has_value()) {
      auto [a, b, c, d] = fo.value();
      printf(" = %llu^2 + %llu^2 = %llu^2 + %llu^2", a, b, c, d);
    }

    if (print) printf("\n");

  }
}

#if 0
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
#endif

#if 0
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
#endif

int main(int argc, char **argv) {
  AnsiInit();
  cl = new CL;
  // DoSearch(0x0BADBEEF, 0x0123456, 0x0777777);
  // DoSearch(29, 47, 1);
  TestCWW();
  printf(AGREEN("OK") "\n");
  return 0;
}

