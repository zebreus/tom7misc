
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

// New version, based on nsoks.
//
// Runs many in batch. Even though there's a lot of paralellism for
// a single number, it does not even come close to beating the CPU
// unless we do a 2D workload.
struct NWaysGPU {
  // This code needs to allocate a rectangular buffer in order to write
  // the ways for each input. This gives the width of that buffer, and
  // then also sets the maximum that we could output.
  // PERF: Could tune this. It's probably not very expensive, though.
  static constexpr int MAX_WAYS = 32;

  // PERF: Can disable output checking, and timers.
  static constexpr bool CHECK_OUTPUT = true;

  CL *cl = nullptr;
  int height = 0;

  NWaysGPU(CL *cl, int height) : cl(cl), height(height) {
    std::string defines = StringPrintf("#define MAX_WAYS %d\n",
                                       MAX_WAYS);
    std::string kernel_src = defines + Util::ReadFile("sos.cl");
    const auto &[prog, kernels] =
      cl->BuildKernels(kernel_src, {"PerfectSquares", "NWays"}, false);
    CHECK(prog != 0);
    program = prog;
    kernel1 = FindOrDefault(kernels, "PerfectSquares", 0);
    kernel2 = FindOrDefault(kernels, "NWays", 0);
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
  GetNWays(const std::vector<std::pair<uint64_t, uint32_t>> &inputs) {
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

    // We perform a rectangular calculation. global_idx(0) is the offset
    //   of the trial square, which ranges from 0..width inclusive.
    // global_idx(1) is the target sum.
    CHECK(minmin <= maxmax) << " " << minmin << " " << maxmax;
    // Inclusive.
    uint64_t width = maxmax - minmin + 1;
    TIMER_END(prep);

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
        // PERF try the transpose.
        //   Width: Nonnegative offsets from minmin that cover all of
        //     the (min-max) spans (inclusive) for the rectangle.
        //   Height: Number of sums.
        size_t global_work_size[] = { (size_t)width, (size_t)height };

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

    // XXX verbose
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

    TIMER_START(ret);
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

      if (CHECK_OUTPUT) {
        for (const auto &[a, b] : one_ret) {
          CHECK(a * a + b * b == sum) << a << "^2 + " << b << "^2 != "
                                      << sum << "(out size " << size << ")"
                                      << "\nwith width " << width
                                      << " and height " << height
                                      << "\nWays: " << WaysString(one_ret);
        }
      }

      ret.push_back(std::move(one_ret));
    }
    TIMER_END(ret);


    TIMER_END(all);
    return ret;
  }

  ~NWaysGPU() {
    CHECK_SUCCESS(clReleaseKernel(kernel1));
    CHECK_SUCCESS(clReleaseKernel(kernel2));
    CHECK_SUCCESS(clReleaseProgram(program));

    CHECK_SUCCESS(clReleaseMemObject(sums_gpu));
    CHECK_SUCCESS(clReleaseMemObject(output_size_gpu));
    CHECK_SUCCESS(clReleaseMemObject(output_gpu));
  }
};

#endif
