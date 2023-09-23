
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

namespace internal {
// Process the rectangular output of the GPU 'ways' algorithm
// into a CPU-friendly vector of pairs, and optionally check
// that they sum to the right value.
template<bool CHECK_OUTPUT, size_t MAX_WAYS>
inline std::vector<std::vector<std::pair<uint64_t, uint64_t>>>
ProcessGPUOutput(int height,
                 const std::vector<std::pair<uint64_t, uint32_t>> &inputs,
                 const std::vector<uint64_t> &output_rect,
                 const std::vector<uint32_t> &output_sizes) {

  // XXX verbose flag
  if (false) {
    for (int y = 0; y < height; y++) {
      printf(ABLUE("%llu") " " ACYAN("%d") " size: " APURPLE("%d") "\n",
             inputs[y].first, (int)inputs[y].second, (int)output_sizes[y]);
      for (int x = 0; x < MAX_WAYS; x ++) {
        int rect_base = y * MAX_WAYS * 2;
        CHECK(rect_base < output_rect.size()) <<
          rect_base << " " << output_rect.size();
        uint64_t a = output_rect[rect_base + x * 2 + 0];
        uint64_t b = output_rect[rect_base + x * 2 + 1];
        printf("  %llu^2 + %llu^2 " AGREY("= %llu") "\n",
               a, b, a * a + b * b);
      }
    }
  }

  CHECK((int)output_sizes.size() == height);
  std::vector<std::vector<std::pair<uint64_t, uint64_t>>> ret;
  ret.reserve(height);
  for (int row = 0; row < height; row++) {
    const uint64_t sum = inputs[row].first;
    const int rect_base = row * MAX_WAYS * 2;
    std::vector<std::pair<uint64_t, uint64_t>> one_ret;
    const int size = output_sizes[row];
    CHECK(size % 2 == 0) << "Size is supposed to be incremented by 2 each "
      "time. " << size << " " << sum;
    one_ret.reserve(size / 2);
    for (int i = 0; i < size / 2; i++) {
      uint64_t a = output_rect[rect_base + i * 2 + 0];
      uint64_t b = output_rect[rect_base + i * 2 + 1];
      one_ret.emplace_back(a, b);
    }

    if constexpr (CHECK_OUTPUT) {
      for (const auto &[a, b] : one_ret) {
        CHECK(a * a + b * b == sum) << a << "^2 + " << b << "^2 != "
                                    << sum << "(out size " << size << ")"
                                    << " with height " << height
                                    << "\nWays: " << WaysString(one_ret);
      }
    }

    ret.push_back(std::move(one_ret));
  }
  return ret;
}
}  // internal

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
  static constexpr std::pair<uint64_t, uint32_t> dummy = {25, 2};
  std::vector<std::vector<std::pair<uint64_t, uint64_t>>>
  // An input is a target sum, with its expected number of ways (use CWW).
  // Ways should be > 0. Computation is proportional to the largest sum,
  // so this is intended for use with batches of sums that are of similar
  // magnitude.
  GetWays(const std::vector<std::pair<uint64_t, uint32_t>> &inputs) {
    TIMER_START(all);
    CHECK(inputs.size() == height) << inputs.size() << " " << height;

    TIMER_START(prep);
    // Rectangular calculation. For the batch, we compute the low and
    // high values, which are the lowest and highest that any sum needs.

    uint64_t minmin = (uint64_t)-1;
    uint64_t maxmax = (uint64_t)0;

    // Make the array of sums, min, and max.
    std::vector<uint64_t> sums;
    sums.reserve(height * 3);
    for (const auto &[num, e] : inputs) {
      uint64_t mx = Sqrt64(num - 2 + 1);
      uint64_t mxmx = mx * mx;
      if (mxmx > num - 2 + 1)
        mx--;

      uint64_t q = num / 2;
      uint64_t r = num % 2;
      uint64_t mn = Sqrt64(q + (r ? 1 : 0));
      if (2 * mn * mn < num) {
        mn++;
      }

      CHECK(e <= MAX_WAYS) << num << " " << e;
      sums.push_back(num);
      sums.push_back(mn);
      sums.push_back(mx);

      minmin = std::min(minmin, mn);
      maxmax = std::max(maxmax, mx);
    }

    // We perform a rectangular calculation. The "x" coordinate of the
    // input is the offset of the trial square, which ranges from
    // 0..width inclusive. The "y" coordinate is the target sum.
    CHECK(minmin <= maxmax) << " " << minmin << " " << maxmax;
    // Inclusive.
    uint64_t width = maxmax - minmin + 1;
    TIMER_END(prep);

    // PERF ideas: I think we spend all the time computing sqrts, for
    // which we need double precision because the numbers are large. (So
    // a faster integer square root would be best here!)
    //
    // But we end up using the same trial squares for all of them, since
    // the calculation is rectangular between minmin/maxmax. Is there
    // some way that we could perform the necessary square roots in a
    // pre-pass? What we actually call sqrt on is target = sum - trialsquare^2,
    // which differs for each sum, so it's not even clear that we redo
    // a lot of work, actually.
    //
    // Another possibility is to compute the minimum and maximum sqrt(target)
    // for the rectangle beforehand, and then just loop over them and square?
    // I think the thing about this is that we end up generating a lot of sums
    // that aren't interesting to us.

    /*
    CHECK(!inputs.empty());
    printf(ACYAN("%llu") " height %d, Width: %llu, minmin %llu maxmax %llu\n",
           inputs[0].first,
           height, width, minmin, maxmax);
    */

    std::vector<uint64_t> output_rect;
    std::vector<uint32_t> output_sizes;

    {
      // Only one GPU process at a time.
      TIMER_START(ml);
      MutexLock ml(&m);
      TIMER_END(ml);

      TIMER_START(input);
      // PERF no clFinish
      CopyBufferToGPU(cl->queue, sums, sums_gpu);
      TIMER_END(input);

      TIMER_START(clear);
      uint64_t SENTINEL = -1;
      CHECK_SUCCESS(
          clEnqueueFillBuffer(cl->queue,
                              output_gpu,
                              // pattern and its size in bytes
                              &SENTINEL, sizeof (uint64_t),
                              // offset and size to fill (in BYTES)
                              0, (size_t)(MAX_WAYS * 2 * height *
                                          sizeof (uint64_t)),
                              // no wait list or event
                              0, nullptr, nullptr));
      TIMER_END(clear);

      // Kernel 1.
      {
        TIMER_START(args1);
        CHECK_SUCCESS(clSetKernelArg(kernel1, 0, sizeof (cl_mem),
                                     (void *)&sums_gpu));

        CHECK_SUCCESS(clSetKernelArg(kernel1, 1, sizeof (cl_mem),
                                     (void *)&output_size_gpu));

        CHECK_SUCCESS(clSetKernelArg(kernel1, 2, sizeof (cl_mem),
                                     (void *)&output_gpu));
        TIMER_END(args1);

        // PerfectSquares is a 1D kernel.
        size_t global_work_offset[] = { (size_t)0 };
        // Height: Number of sums.
        size_t global_work_size[] = { (size_t)height };

        TIMER_START(kernel1);
        CHECK_SUCCESS(
            clEnqueueNDRangeKernel(cl->queue, kernel1,
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
        // PERF: No wait
        clFinish(cl->queue);
        TIMER_END(kernel1);
      }

      // Kernel 2.
      {
        TIMER_START(args2);
        CHECK_SUCCESS(clSetKernelArg(kernel2, 0, sizeof (uint64_t),
                                     (void *)&minmin));
        CHECK_SUCCESS(clSetKernelArg(kernel2, 1, sizeof (cl_mem),
                                     (void *)&sums_gpu));

        CHECK_SUCCESS(clSetKernelArg(kernel2, 2, sizeof (cl_mem),
                                     (void *)&output_size_gpu));

        CHECK_SUCCESS(clSetKernelArg(kernel2, 3, sizeof (cl_mem),
                                     (void *)&output_gpu));
        TIMER_END(args2);

        size_t global_work_offset[] = { (size_t)0, (size_t)0 };

        //   Width: Nonnegative offsets from minmin that cover all of
        //     the (min-max) spans (inclusive) for the rectangle.
        //   Height: Number of sums.
        // (Transpose seems much faster; not sure why.)

        #if TRANSPOSE
        size_t global_work_size[] = { (size_t)height, (size_t)width };
        #else
        size_t global_work_size[] = { (size_t)width, (size_t)height };
        #endif

        TIMER_START(kernel2);
        CHECK_SUCCESS(
            clEnqueueNDRangeKernel(cl->queue, kernel2,
                                   // 2D
                                   2,
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
        TIMER_END(kernel2);
      }

      TIMER_START(read);
      output_sizes =
        CopyBufferFromGPU<uint32_t>(cl->queue, output_size_gpu, height);
      output_rect =
        CopyBufferFromGPU<uint64_t>(cl->queue, output_gpu,
                                    height * MAX_WAYS * 2);
      TIMER_END(read);
    }
    // Done with GPU.

    TIMER_START(ret);
    auto ret = internal::ProcessGPUOutput<CHECK_OUTPUT, MAX_WAYS>(
        height, inputs, output_rect, output_sizes);
    TIMER_END(ret);

    TIMER_END(all);
    return ret;
  }

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
  static constexpr int MAX_WAYS = 32;

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
  static constexpr std::pair<uint64_t, uint32_t> dummy = {25, 2};
  std::vector<std::vector<std::pair<uint64_t, uint64_t>>>
  // An input is a target sum, with its expected number of ways (use CWW).
  // Ways should be > 0. Computation is proportional to the largest sum,
  // so this is intended for use with batches of sums that are of similar
  // magnitude.
  GetWays(const std::vector<std::pair<uint64_t, uint32_t>> &inputs) {
    TIMER_START(all);
    CHECK(inputs.size() == height) << inputs.size() << " " << height;

    TIMER_START(prep);

    // Make the array of sums.
    std::vector<uint64_t> sums;
    sums.reserve(height);
    for (const auto &[num, e] : inputs) {
      CHECK(e <= MAX_WAYS) << num << " " << e;
      sums.push_back(num);
    }
    TIMER_END(prep);

    std::vector<uint64_t> output_rect;
    std::vector<uint32_t> output_sizes;

    {
      // Only one GPU process at a time.
      TIMER_START(ml);
      MutexLock ml(&m);
      TIMER_END(ml);

      TIMER_START(input);
      // PERF no clFinish
      CopyBufferToGPU(cl->queue, sums, sums_gpu);
      TIMER_END(input);

      // Run kernel.
      {
        TIMER_START(args2);
        CHECK_SUCCESS(clSetKernelArg(kernel, 0, sizeof (cl_mem),
                                     (void *)&sums_gpu));

        CHECK_SUCCESS(clSetKernelArg(kernel, 1, sizeof (cl_mem),
                                     (void *)&output_size_gpu));

        CHECK_SUCCESS(clSetKernelArg(kernel, 2, sizeof (cl_mem),
                                     (void *)&output_gpu));
        TIMER_END(args2);

        // Simple 1D Kernel
        size_t global_work_offset[] = { (size_t)0 };
        size_t global_work_size[] = { (size_t)height };

        TIMER_START(kernel2);
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
        TIMER_END(kernel2);
      }

      TIMER_START(read);
      output_sizes =
        CopyBufferFromGPU<uint32_t>(cl->queue, output_size_gpu, height);
      output_rect =
        CopyBufferFromGPU<uint64_t>(cl->queue, output_gpu,
                                    height * MAX_WAYS * 2);
      TIMER_END(read);
    }
    // Done with GPU.

    TIMER_START(ret);
    auto ret = internal::ProcessGPUOutput<CHECK_OUTPUT, MAX_WAYS>(
        height, inputs, output_rect, output_sizes);
    TIMER_END(ret);

    TIMER_END(all);
    return ret;
  }

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

  // TODO PERF: We don't have to go all the way to MAX_WAYS, and outliers
  // may cause the entire warp (thread block?) to take longer. Tune some
  // limit.
  TryFilterGPU(CL *cl, int height) : cl(cl), height(height) {
    std::string defines = StringPrintf("#define MAX_WAYS %d\n",
                                       MAX_WAYS);
    std::string kernel_src = defines + Util::ReadFile("try.cl");
    const auto &[prog, kernels] =
      cl->BuildKernels(kernel_src, {"SquareVec", "TryFilter"}, false);
    CHECK(prog != 0);
    program = prog;
    kernel1 = FindOrDefault(kernels, "SquareVec", 0);
    kernel2 = FindOrDefault(kernels, "TryFilter", 0);
    CHECK(kernel1 != 0);
    CHECK(kernel2 != 0);

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
                                             height * MAX_WAYS * 2);
    ways_size_gpu =
      CreateUninitializedGPUMemory<uint32_t>(cl->context, height);
    // Output.
    rejected_gpu =
      CreateUninitializedGPUMemory<uint32_t>(cl->context, height);
  }

  // Synchronized access.
  std::mutex m;
  cl_program program = 0;
  cl_kernel kernel1 = 0, kernel2 = 0;
  cl_mem ways_gpu = nullptr;
  cl_mem ways_size_gpu = nullptr;
  cl_mem rejected_gpu = nullptr;

  // Sets *rejected_f to the number of squares (not rows) that were
  // filtered. Returns the rows that were kept.
  std::vector<TryMe> FilterWays(std::vector<TryMe> &input,
                                uint64_t *rejected_f) {

    CHECK(input.size() == height) << input.size() << " " << height;

    // Make the input arrays.
    std::vector<uint64_t> ways;
    ways.reserve(height * MAX_WAYS * 2);
    std::vector<uint32_t> ways_size;
    ways_size.reserve(height);

    for (const TryMe &tryme : input) {
      CHECK(tryme.squareways.size() <= MAX_WAYS) << tryme.squareways.size();
      ways_size.push_back(tryme.squareways.size() * 2);
      for (int i = 0; i < MAX_WAYS; i++) {
        if (i < tryme.squareways.size()) {
          const auto &[a, b] = tryme.squareways[i];
          // PERF compare using kernel
          ways.push_back(a * a);
          ways.push_back(b * b);
        } else {
          ways.push_back(0);
          ways.push_back(0);
        }
      }
    }

    std::vector<uint32_t> rejected;

    {
      // Only one GPU process at a time.
      MutexLock ml(&m);

      CopyBufferToGPU(cl->queue, ways, ways_gpu);
      CopyBufferToGPU(cl->queue, ways_size, ways_size_gpu);

      // Run kernel.
      {
        CHECK_SUCCESS(clSetKernelArg(kernel2, 0, sizeof (cl_mem),
                                     (void *)&ways_size_gpu));

        CHECK_SUCCESS(clSetKernelArg(kernel2, 1, sizeof (cl_mem),
                                     (void *)&ways_gpu));
        CHECK_SUCCESS(clSetKernelArg(kernel2, 2, sizeof (cl_mem),
                                     (void *)&rejected_gpu));

        // Simple 1D Kernel
        // PERF: Might help to factor out one or more of the loops in
        // the kernel...
        size_t global_work_offset[] = { (size_t)0 };
        size_t global_work_size[] = { (size_t)height };

        CHECK_SUCCESS(
            clEnqueueNDRangeKernel(cl->queue, kernel2,
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

      rejected =
        CopyBufferFromGPU<uint32_t>(cl->queue, rejected_gpu, height);
    }
    // Done with GPU.

    uint64_t rejected_counter = 0;
    std::vector<TryMe> out;
    out.reserve(height);
    for (int row = 0; row < height; row++) {
      if (rejected[row] == 0) {
        // Keep it. Since the order is the same as the input, the
        // simplest thing is to just copy the input row.
        out.push_back(input[row]);
      } else {
        // Filter out, but accumulate counter.
        rejected_counter += rejected[row];
      }
    }

    *rejected_f = rejected_counter;
    return out;
  }

  ~TryFilterGPU() {
    CHECK_SUCCESS(clReleaseKernel(kernel1));
    CHECK_SUCCESS(clReleaseKernel(kernel2));
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
               int next_prime = 137)
    : cl(cl), height(height) {

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
                   "#define NEXT_PRIME %d\n"
                   "#define IsPrimeInternal %s\n",
                   MAX_FACTORS,
                   sub_128 ? 1 : 0,
                   geq_128 ? 1 : 0,
                   mul_128 ? 1 : 0,
                   fused_try ? 1 : 0,
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
                                   (void *)&out_gpu));

      CHECK_SUCCESS(clSetKernelArg(kernel, 2, sizeof (cl_mem),
                                   (void *)&out_size_gpu));


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

    return make_pair(
        CopyBufferFromGPU<uint64_t>(cl->queue, out_gpu, MAX_FACTORS * height),
        CopyBufferFromGPU<uint8_t>(cl->queue, out_size_gpu, height));
  }

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


#endif
