
#ifndef _MOD_GPU_H
#define _MOD_GPU_H

#include <cstdint>
#include <vector>
#include <string>
#include <mutex>
#include <utility>

#include "base/logging.h"
#include "base/stringprintf.h"

#include "clutil.h"
#include "util.h"
#include "map-util.h"


// For mod.exe: For a batch of primes and pairs (m, n), does a quick
// test to see if the equation has a solution mod prime. Most triples
// are rejected easily (by finding a solution) so the goal is to get
// those out of the way so that we can do a slower full pass for the
// rare cases that remain.
struct ModQuickPassGPU {
  // Can only run primes larger than this.
  static constexpr int MIN_PRIME = 360749;

  // Output is a task to do as a full run.
  struct FullRun {
    FullRun(int m, int n, uint64_t prime) : m(m), n(n), prime(prime) {}
    int m = 0, n = 0;
    uint64_t prime = 0;
  };

  // XXX: Should depend on height.
  static constexpr int MAX_FULL_RUNS = 64;

  // Number of quick tests to do.
  // Note: primes must be larger than this.
  // XXX: Lowered for test. But this should be like 64?
  static constexpr int QUICK_PASS_SIZE = 64;

  // The width is the number of primes.
  // The height is the (maximum) number of (m,n) pairs.
  ModQuickPassGPU(CL *cl, size_t width, size_t height) : cl(cl),
                                                         width(width),
                                                         height(height) {
    std::string defines =
      StringPrintf(
          "#define MAX_FULL_RUNS %d\n"
          "#define QUICK_PASS_SIZE %d\n"
          "#define WIDTH %d\n"
          "#define HEIGHT %d\n",
          MAX_FULL_RUNS,
          QUICK_PASS_SIZE,
          width, height);

    std::string kernel_src = defines + Util::ReadFile("mod.cl");
    const auto &[prog, kernels] =
      cl->BuildKernels(kernel_src, {"InitializeAtomic", "QuickPass"}, false);

    CHECK(prog != 0);
    program = prog;
    kernel_init = FindOrDefault(kernels, "InitializeAtomic", 0);
    CHECK(kernel_init != 0);
    kernel_quick = FindOrDefault(kernels, "QuickPass", 0);
    CHECK(kernel_quick != 0);

    // As input, we take an array of primes and an array of m,n pairs.

    primes_gpu = CreateUninitializedGPUMemory<uint64_t>(
        cl->context,
        width);

    // These range from -333 to +333 so they can be int16s.
    mns_gpu = CreateUninitializedGPUMemory<int16_t>(
        cl->context,
        // m,n adjacent
        height * 2);

    // For each mn, we have a list of primes that passed the
    // filter and need a full run. We assume this is rare, so
    // the table is undersized.
    out_gpu = CreateUninitializedGPUMemory<uint64_t>(
        cl->context,
        height * MAX_FULL_RUNS);

    // Number of prime entries in the row of out_gpu.
    // We could use uint8 here, but uint32 is the smallest atomic type.
    out_sizes_gpu = CreateUninitializedGPUMemory<uint32_t>(
        cl->context,
        height);
  }

  std::vector<FullRun>
  Run(// must be 'width'
      const std::vector<uint64_t> &primes,
      // must be 'height'
      const std::vector<std::pair<int, int>> &mn);

  ~ModQuickPassGPU() {
    CHECK_SUCCESS(clReleaseKernel(kernel_init));
    CHECK_SUCCESS(clReleaseKernel(kernel_quick));
    CHECK_SUCCESS(clReleaseProgram(program));

    CHECK_SUCCESS(clReleaseMemObject(out_sizes_gpu));
    CHECK_SUCCESS(clReleaseMemObject(out_gpu));
    CHECK_SUCCESS(clReleaseMemObject(mns_gpu));
    CHECK_SUCCESS(clReleaseMemObject(primes_gpu));
  }

private:
  // Synchronized access.
  std::mutex m;
  cl_program program = 0;
  cl_kernel kernel_init = 0, kernel_quick = 0;
  cl_mem primes_gpu = nullptr, mns_gpu = nullptr, out_gpu = nullptr,
    out_sizes_gpu = nullptr;

  CL *cl = nullptr;
  size_t width = 0, height = 0;
};

#endif
