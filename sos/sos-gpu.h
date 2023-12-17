
#ifndef _SOS_SOS_GPU_H
#define _SOS_SOS_GPU_H

#include <utility>
#include <mutex>
#include <vector>
#include <string>

#include "threadutil.h"
#include "clutil.h"
#include "util.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "sos-util.h"
#include "timer.h"
#include "ansi.h"
#include "map-util.h"

// This is like 28% faster!
#define TRANSPOSE 1

// New version, based on nsoks.
//
// Runs many in batch. Even though there's a lot of parallelism for
// a single number, it does not even come close to beating the CPU
// unless we do a 2D workload.
struct WaysGPU {
  // This code needs to allocate a rectangular buffer in order to write
  // the ways for each input. This gives the width of that buffer, and
  // then also sets the maximum that we could output.
  // PERF: Could tune this. It's probably not very expensive, though.
  static constexpr int MAX_WAYS = 32;

  // PERF: Can disable output checking, and timers.
  static constexpr bool CHECK_OUTPUT = false;

  CL *cl = nullptr;
  int height = 0;

  WaysGPU(CL *cl, int height) : cl(cl), height(height) {
    std::string defines = StringPrintf("#define MAX_WAYS %d\n"
                                       "#define TRANSPOSE %d\n",
                                       MAX_WAYS,
                                       TRANSPOSE);
    std::string kernel_src = defines + Util::ReadFile("sos.cl");
    const auto &[prog, kernels] =
      cl->BuildKernels(kernel_src, {"PerfectSquares", "Ways"}, false);
    CHECK(prog != 0);
    program = prog;
    kernel1 = FindOrDefault(kernels, "PerfectSquares", 0);
    kernel2 = FindOrDefault(kernels, "Ways", 0);
    CHECK(kernel1 != 0);
    CHECK(kernel2 != 0);

    // Input buffer.
    sums_gpu =
      CreateUninitializedGPUMemory<uint64_t>(cl->context, height * 3);

    // Two 64-bit words per way.
    // We keep reusing this buffer.
    output_gpu =
      CreateUninitializedGPUMemory<uint64_t>(cl->context,
                                             height * MAX_WAYS * 2);

    output_size_gpu =
      CreateUninitializedGPUMemory<uint32_t>(cl->context, height);
  }

  // Synchronized access.
  std::mutex m;
  cl_program program = 0;
  cl_kernel kernel1, kernel2 = 0;
  cl_mem sums_gpu = nullptr;
  cl_mem output_gpu = nullptr;
  cl_mem output_size_gpu = nullptr;

  double t_prep = 0.0;
  double t_ml = 0.0;
  double t_clear = 0.0;
  double t_input = 0.0;
  double t_args1 = 0.0;
  double t_kernel1 = 0.0;
  double t_args2 = 0.0;
  double t_kernel2 = 0.0;
  double t_read = 0.0;
  double t_ret = 0.0;

  double t_all = 0.0;

# define TIMER_START(d) Timer d ## _timer
# define TIMER_END(d) t_ ## d += d ## _timer.Seconds()

  void PrintTimers() {
#define ONETIMER(d) printf(ACYAN( #d ) ": %s\n", ANSI::Time(t_ ## d).c_str())
    ONETIMER(prep);
    ONETIMER(ml);
    ONETIMER(clear);
    ONETIMER(input);
    ONETIMER(args1);
    ONETIMER(kernel1);
    ONETIMER(args2);
    ONETIMER(kernel2);
    ONETIMER(read);
    ONETIMER(ret);
    ONETIMER(all);
  };

  // Can be used to fill the height when you don't have a full set
  // to process (or we could do this internally...)
  // 25 = 0^2 + 5^2 = 3^2 + 4^2
  static constexpr std::tuple<uint64_t, uint32_t, CollatedFactors>
  dummy = {
    25,
    2,
    CollatedFactors{
      .bases = {5},
      .exponents = {2},
      .num_factors = 1}
  };

  // An input is a target sum, with its expected number of ways (use CWW).
  // Ways should be > 0. Computation is proportional to the largest sum,
  // so this is intended for use with batches of sums that are of similar
  // magnitude.
  std::vector<std::vector<std::pair<uint64_t, uint64_t>>>
  GetWays(const std::vector<
          std::tuple<uint64_t, uint32_t, CollatedFactors>> &inputs);

  ~WaysGPU() {
    CHECK_SUCCESS(clReleaseKernel(kernel1));
    CHECK_SUCCESS(clReleaseKernel(kernel2));
    CHECK_SUCCESS(clReleaseProgram(program));

    CHECK_SUCCESS(clReleaseMemObject(sums_gpu));
    CHECK_SUCCESS(clReleaseMemObject(output_size_gpu));
    CHECK_SUCCESS(clReleaseMemObject(output_gpu));
  }
};


// Another attempt. This is a 1D kernel, which completely avoids
// square roots, but has to do a loop. Slower on the CPU, but
// it is significantly faster than the approach above on GPU.
struct WaysGPUMerge {
  // This code needs to allocate a rectangular buffer in order to write
  // the ways for each input. This gives the width of that buffer, and
  // then also sets the maximum that we could output.
  // PERF: Could tune this. It's probably not very expensive, though.
  static constexpr int MAX_WAYS = 8;

  // PERF: Can disable output checking, and timers.
  static constexpr bool CHECK_OUTPUT = false;

  CL *cl = nullptr;
  int height = 0;

  WaysGPUMerge(CL *cl, int height) : cl(cl), height(height) {
    std::string defines = StringPrintf("#define MAX_WAYS %d\n",
                                       MAX_WAYS);
    std::string kernel_src = defines + Util::ReadFile("sos.cl");
    const auto &[prog, kern] =
      cl->BuildOneKernel(kernel_src, {"WaysMerge"}, false);
    CHECK(prog != 0);
    program = prog;
    CHECK(kern != 0);
    kernel = kern;

    // Input buffer.
    sums_gpu =
      CreateUninitializedGPUMemory<uint64_t>(cl->context, height);

    // Two 64-bit words per way.
    // We keep reusing this buffer.
    output_gpu =
      CreateUninitializedGPUMemory<uint64_t>(cl->context,
                                             height * MAX_WAYS * 2);

    output_size_gpu =
      CreateUninitializedGPUMemory<uint32_t>(cl->context, height);
  }

  // Synchronized access.
  std::mutex m;
  cl_program program = 0;
  cl_kernel kernel = 0;
  cl_mem sums_gpu = nullptr;
  cl_mem output_gpu = nullptr;
  cl_mem output_size_gpu = nullptr;

  double t_prep = 0.0;
  double t_ml = 0.0;
  double t_clear = 0.0;
  double t_input = 0.0;
  double t_args1 = 0.0;
  double t_kernel1 = 0.0;
  double t_args2 = 0.0;
  double t_kernel2 = 0.0;
  double t_read = 0.0;
  double t_ret = 0.0;

  double t_all = 0.0;

# define TIMER_START(d) Timer d ## _timer
# define TIMER_END(d) t_ ## d += d ## _timer.Seconds()

  void PrintTimers() {
#define ONETIMER(d) printf(ACYAN( #d ) ": %s\n", ANSI::Time(t_ ## d).c_str())
    ONETIMER(prep);
    ONETIMER(ml);
    ONETIMER(clear);
    ONETIMER(input);
    ONETIMER(args1);
    ONETIMER(kernel1);
    ONETIMER(args2);
    ONETIMER(kernel2);
    ONETIMER(read);
    ONETIMER(ret);
    ONETIMER(all);
  };

  // Can be used to fill the height when you don't have a full set
  // to process (or we could do this internally...)
  // 25 = 0^2 + 5^2 = 3^2 + 4^2
  static constexpr std::tuple<uint64_t, uint32_t, CollatedFactors>
  dummy = {
    25,
    2,
    CollatedFactors{
      .bases = {5},
      .exponents = {2},
      .num_factors = 1}
  };


  // An input is a target sum, with its expected number of ways (use CWW).
  // Ways should be > 0. Computation is proportional to the largest sum,
  // so this is intended for use with batches of sums that are of similar
  // magnitude.
  std::vector<std::vector<std::pair<uint64_t, uint64_t>>>
  GetWays(const std::vector<
          std::tuple<uint64_t, uint32_t, CollatedFactors>> &inputs);

  ~WaysGPUMerge() {
    CHECK_SUCCESS(clReleaseKernel(kernel));
    CHECK_SUCCESS(clReleaseProgram(program));

    CHECK_SUCCESS(clReleaseMemObject(sums_gpu));
    CHECK_SUCCESS(clReleaseMemObject(output_size_gpu));
    CHECK_SUCCESS(clReleaseMemObject(output_gpu));
  }
};


// Filters the output of nwaysgpu for rows that might have an interesting
// square.
struct TryFilterGPU {
  static constexpr int MAX_WAYS = 32;

  CL *cl = nullptr;
  int height = 0;
  // If set, every row is assumed to have exactly this many ways. We
  // separate out common small nways and run them in their own batches
  // (by having multiple instantiations of this object). This
  // allows loop unrolling and prevents hard problems from slowing
  // down easy ones; the kernel is O(n^3).
  std::optional<int> fixed_width = std::nullopt;
  // Either MAX_WAYS or fixed_width.
  int data_width = 0;

  int Height() const { return height; }

  TryFilterGPU(CL *cl, int height,
               std::optional<int> fixed_width = std::nullopt) :
    cl(cl), height(height), fixed_width(fixed_width) {

    std::string defines;
    if (fixed_width.has_value()) {
      // Maybe we should just ignore max_ways in this case?
      CHECK(fixed_width.value() <= MAX_WAYS);
      defines = StringPrintf("#define FIXED_WIDTH %d\n"
                             "#define ROW_STRIDE %d\n",
                             fixed_width.value(),
                             fixed_width.value());
      data_width = fixed_width.value();
    } else {
      defines = StringPrintf("#define ROW_STRIDE %d\n",
                             MAX_WAYS);
      data_width = MAX_WAYS;
    }

    std::string kernel_src = defines + Util::ReadFile("try.cl");
    const auto &[prog, kernels] =
      cl->BuildKernels(kernel_src, {"TryFilter"}, false);
    CHECK(prog != 0);
    program = prog;
    kernel = FindOrDefault(kernels, "TryFilter", 0);
    CHECK(kernel != 0);

    /*
      std::optional<std::string> ptx = cl->DecodeProgram(program);
      CHECK(ptx.has_value());
      Util::WriteFile("tryfilter.ptx", ptx.value());
      printf("\n");
      printf("Wrote tryfilter.ptx\n");
    */

    // Same meaning as above, but we square the elements in place.
    ways_gpu =
      CreateUninitializedGPUMemory<uint64_t>(cl->context,
                                             height * data_width * 2);
    ways_size_gpu =
      CreateUninitializedGPUMemory<uint32_t>(cl->context, height);
    // Output.
    rejected_gpu =
      CreateUninitializedGPUMemory<uint32_t>(cl->context, height);
  }

  // Synchronized access.
  std::mutex m;
  cl_program program = 0;
  cl_kernel kernel = 0;
  cl_mem ways_gpu = nullptr;
  cl_mem ways_size_gpu = nullptr;
  cl_mem rejected_gpu = nullptr;

  // Sets *rejected_f to the number of squares (not rows) that were
  // filtered. Returns the rows that were kept.
  std::vector<TryMe> FilterWays(std::vector<TryMe> &input,
                                uint64_t *rejected_f);

  ~TryFilterGPU() {
    CHECK_SUCCESS(clReleaseKernel(kernel));
    CHECK_SUCCESS(clReleaseProgram(program));

    CHECK_SUCCESS(clReleaseMemObject(ways_size_gpu));
    CHECK_SUCCESS(clReleaseMemObject(ways_gpu));
    CHECK_SUCCESS(clReleaseMemObject(rejected_gpu));
  }
};

struct EligibleFilterGPU {
  CL *cl = nullptr;
  // Number (times 8) to process in one call.
  size_t height = 0;

  EligibleFilterGPU(CL *cl, size_t height) : cl(cl), height(height) {
    std::string defines = "";
    std::string kernel_src = defines + Util::ReadFile("eligible.cl");
    const auto &[prog, kern] =
      cl->BuildOneKernel(kernel_src, "NotSumOfSquares", false);
    CHECK(prog != 0);
    program = prog;
    CHECK(kern != 0);
    kernel = kern;

    out_gpu = CreateUninitializedGPUMemory<uint8_t>(cl->context, height);
  }

  // Synchronized access.
  std::mutex m;
  cl_program program = 0;
  cl_kernel kernel = 0;
  cl_mem out_gpu = nullptr;

  // Processes k in [start, start+height) and returns height/8 bytes
  // with a bitmask labeling the sums that can be filtered out.
  std::vector<uint8_t> Filter(uint64_t start) {
    // Only one GPU process at a time.
    MutexLock ml(&m);

    // Run kernel.
    {
      CHECK_SUCCESS(clSetKernelArg(kernel, 0, sizeof (uint64_t),
                                   (void *)&start));

      CHECK_SUCCESS(clSetKernelArg(kernel, 1, sizeof (cl_mem),
                                   (void *)&out_gpu));

      // Simple 1D Kernel
      size_t global_work_offset[] = { (size_t)0 };
      size_t global_work_size[] = { (size_t)height };

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
    }

    return CopyBufferFromGPU<uint8_t>(cl->queue, out_gpu, height);
  }

  ~EligibleFilterGPU() {
    CHECK_SUCCESS(clReleaseKernel(kernel));
    CHECK_SUCCESS(clReleaseProgram(program));

    CHECK_SUCCESS(clReleaseMemObject(out_gpu));
  }
};

struct FactorizeGPU {
  CL *cl = nullptr;
  size_t height = 0;

  // PERF we could use 32-bit for all but the final factor.
  // Maybe better would be to have an array of 8-bit factors,
  // then 64-bit ones. This probably isn't memory-bound, though.
  //
  // output is an array of 64 64-bit integer factors. The
  // most factors we could have is 64 (2^64).
  //
  // Since we have the option to fail, we also could reduce this
  // and just bail if we see too many factors. Such numbers are
  // easy to factor, anyway.
  static constexpr int MAX_FACTORS = 64;

  // Advanced tuning
  enum class IsPrimeRoutine {
    OLD,
    UNROLLED,
    GENERAL,
    FEW,
  };

  FactorizeGPU(CL *cl, size_t height,
               IsPrimeRoutine is_prime = IsPrimeRoutine::FEW,
               bool sub_128 = false,
               bool geq_128 = false,
               bool mul_128 = false,
               bool fused_try = false,
               bool binv_table = false,
               bool dumas = false,
               int next_prime = 137)
    : cl(cl), height(height) {

    CHECK(next_prime > 2);

    const char *is_prime_routine = [&]() {
      switch (is_prime) {
      default: LOG(FATAL) << "Invalid";
      case IsPrimeRoutine::OLD: return "IsPrimeInternalOld";
      case IsPrimeRoutine::UNROLLED: return "IsPrimeInternalUnrolled";
      case IsPrimeRoutine::GENERAL: return "IsPrimeInternalGeneral";
      case IsPrimeRoutine::FEW: return "IsPrimeInternalFew";
      }
    }();


    std::string defines =
      StringPrintf("#define MAX_FACTORS %d\n"
                   "#define PTX_SUB128 %d\n"
                   "#define PTX_GEQ128 %d\n"
                   "#define PTX_MUL128 %d\n"
                   "#define FUSED_TRY %d\n"
                   "#define BINV_USE_TABLE %d\n"
                   "#define BINV_USE_DUMAS %d\n"
                   "#define NEXT_PRIME %d\n"
                   "#define IsPrimeInternal %s\n",
                   MAX_FACTORS,
                   sub_128 ? 1 : 0,
                   geq_128 ? 1 : 0,
                   mul_128 ? 1 : 0,
                   fused_try ? 1 : 0,
                   binv_table ? 1 : 0,
                   dumas ? 1 : 0,
                   next_prime,
                   is_prime_routine);

    // printf("%s\n", defines.c_str());

    std::string kernel_src = defines + Util::ReadFile("factorize.cl");
    const auto &[prog, kern] =
      cl->BuildOneKernel(kernel_src, "Factorize", false);
    CHECK(prog != 0);
    program = prog;
    CHECK(kern != 0);
    kernel = kern;

    nums_gpu = CreateUninitializedGPUMemory<uint64_t>(
        cl->context,
        height);
    out_gpu = CreateUninitializedGPUMemory<uint64_t>(
        cl->context,
        height * MAX_FACTORS);
    // Number of factors in each row.
    out_size_gpu = CreateUninitializedGPUMemory<uint8_t>(
        cl->context,
        height);
  }

  // Synchronized access.
  std::mutex m;
  cl_program program = 0;
  cl_kernel kernel = 0;
  cl_mem nums_gpu = nullptr;
  cl_mem out_gpu = nullptr;
  cl_mem out_size_gpu = nullptr;

  // Processes a batch of numbers (size height).
  // Returns a dense array of factors (MAX_FACTORS x height)
  // with the count of factors per input.
  std::pair<std::vector<uint64_t>,
            std::vector<uint8>>
  Factorize(const std::vector<uint64_t> &nums);

  ~FactorizeGPU() {
    CHECK_SUCCESS(clReleaseKernel(kernel));
    CHECK_SUCCESS(clReleaseProgram(program));

    CHECK_SUCCESS(clReleaseMemObject(nums_gpu));
    CHECK_SUCCESS(clReleaseMemObject(out_gpu));
    CHECK_SUCCESS(clReleaseMemObject(out_size_gpu));
  }
};

// Two-phase factorization.
// First phase is trial factoring. This succeeds quickly for many
// numbers.
struct TrialDivideGPU {
  CL *cl = nullptr;
  size_t height = 0;

  // Consider reducing this.
  static constexpr int MAX_FACTORS = 64;

  TrialDivideGPU(CL *cl, size_t height) : cl(cl), height(height) {
    std::string defines = StringPrintf("#define MAX_FACTORS %d\n",
                                       MAX_FACTORS);
    std::string kernel_src = defines + Util::ReadFile("trialdivide.cl");
    const auto &[prog, kern] =
      cl->BuildOneKernel(kernel_src, "TrialDivide", false);
    CHECK(prog != 0);
    program = prog;
    CHECK(kern != 0);
    kernel = kern;

    // Input numbers.
    nums_gpu = CreateUninitializedGPUMemory<uint64_t>(
        cl->context,
        height);

    // 1 factor can be >32 bits.
    large_factors_gpu = CreateUninitializedGPUMemory<uint64_t>(
        cl->context,
        height);
    // The rest must be small.
    small_factors_gpu = CreateUninitializedGPUMemory<uint32_t>(
        cl->context,
        height * MAX_FACTORS);
    // Number of factors in each row. High bit set if factoring
    // was incomplete.
    // XXX including the large factor?
    num_factors_gpu = CreateUninitializedGPUMemory<uint8_t>(
        cl->context,
        height);
  }

  // Synchronized access.
  std::mutex m;
  cl_program program = 0;
  cl_kernel kernel = 0;
  cl_mem nums_gpu = nullptr;
  cl_mem large_factors_gpu = nullptr;
  cl_mem small_factors_gpu = nullptr;
  cl_mem num_factors_gpu = nullptr;

  // Processes a batch of numbers (size height).
  // Returns three vectors, parallel to the input:
  //   - The large factor, a 64-bit number
  //   - A row of MAX_FACTORS small factors (and padding)
  //   - The count of factors, with the high bit set to one if
  //     factoring failed.
  std::tuple<
    // Large factors. Size height.
    std::vector<uint64_t>,
    // Small factors. height * MAX_FACTORS
    std::vector<uint32_t>,
    // Counts
    std::vector<uint8>>
  Factorize(const std::vector<uint64_t> &nums) {
    // Only one GPU process at a time.
    MutexLock ml(&m);

    CHECK(nums.size() == height);

    // Run kernel.
    {

      CopyBufferToGPU(cl->queue, nums, nums_gpu);

      CHECK_SUCCESS(clSetKernelArg(kernel, 0, sizeof (cl_mem),
                                   (void *)&nums_gpu));

      CHECK_SUCCESS(clSetKernelArg(kernel, 1, sizeof (cl_mem),
                                   (void *)&large_factors_gpu));

      CHECK_SUCCESS(clSetKernelArg(kernel, 2, sizeof (cl_mem),
                                   (void *)&small_factors_gpu));

      CHECK_SUCCESS(clSetKernelArg(kernel, 3, sizeof (cl_mem),
                                   (void *)&num_factors_gpu));

      // Simple 1D Kernel
      size_t global_work_offset[] = { (size_t)0 };
      size_t global_work_size[] = { (size_t)height };

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
    }

    return make_tuple(
        CopyBufferFromGPU<uint64_t>(cl->queue, large_factors_gpu,
                                    height),
        CopyBufferFromGPU<uint32_t>(cl->queue, small_factors_gpu,
                                    MAX_FACTORS * height),
        CopyBufferFromGPU<uint8_t>(cl->queue, num_factors_gpu,
                                   height));
  }

  ~TrialDivideGPU() {
    CHECK_SUCCESS(clReleaseKernel(kernel));
    CHECK_SUCCESS(clReleaseProgram(program));

    CHECK_SUCCESS(clReleaseMemObject(nums_gpu));
    CHECK_SUCCESS(clReleaseMemObject(large_factors_gpu));
    CHECK_SUCCESS(clReleaseMemObject(small_factors_gpu));
    CHECK_SUCCESS(clReleaseMemObject(num_factors_gpu));
  }
};

// Tests a range of numbers for primality; outputs a bytemask.
struct IsPrimeGPU {
  IsPrimeGPU(CL *cl, size_t height) : cl(cl), height(height) {
    // Need to define stuff that factorize.cl uses, even
    // if it's not used for this routine.
    static constexpr int MAX_FACTORS = 64;

    std::string defines =
      StringPrintf(
          "#define MAX_FACTORS %d\n"
          "#define PTX_SUB128 %d\n"
          "#define PTX_GEQ128 %d\n"
          "#define PTX_MUL128 %d\n"
          "#define FUSED_TRY %d\n"
          "#define BINV_USE_TABLE %d\n"
          "#define BINV_USE_DUMAS %d\n"
          "#define NEXT_PRIME %d\n"
          "#define IsPrimeInternal %s\n",
          MAX_FACTORS,
          0, 1, 1, 0, 1, 1,
          NEXT_PRIME,
          is_prime_routine);

    std::string kernel_src = defines + Util::ReadFile("factorize.cl");
    const auto &[prog, kern] =
      cl->BuildOneKernel(kernel_src, "IsPrimeRange", false);
    CHECK(prog != 0);
    program = prog;
    CHECK(kern != 0);
    kernel = kern;

    out_gpu = CreateUninitializedGPUMemory<uint8_t>(
        cl->context,
        height);
  }

  // Processes a batch of odd numbers, starting with start_idx.
  // The returned array has length 'height', and the nth element
  // is 1 if start_idx + (2 * n) is prime; 0 if composite.
  std::vector<uint8_t>
  GetPrimes(uint64_t start_idx);

  ~IsPrimeGPU() {
    CHECK_SUCCESS(clReleaseKernel(kernel));
    CHECK_SUCCESS(clReleaseProgram(program));

    CHECK_SUCCESS(clReleaseMemObject(out_gpu));
  }

private:
  // Synchronized access.
  std::mutex m;
  cl_program program = 0;
  cl_kernel kernel = 0;
  cl_mem out_gpu = nullptr;

  CL *cl = nullptr;
  // Number of inputs to test at once.
  size_t height = 0;

  // Might be useful to tune this.
  static constexpr int NEXT_PRIME = 1709;
  static constexpr const char *is_prime_routine = "IsPrimeInternalFew";
};


#endif
