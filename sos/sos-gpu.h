
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



// Runs many in batch. Even though there's a lot of paralellism for
// a single number, it does not even come close to beating the CPU
// unless we do a 2D workload.
struct NWaysGPU {
  // This code needs to allocate a rectangular buffer in order to write
  // the ways for each input. This gives the width of that buffer, and
  // then also sets the maximum that we could output.
  static constexpr int MAX_WAYS = 32;

  static constexpr bool CHECK_OUTPUT = false;

  CL *cl = nullptr;
  int height = 0;

  NWaysGPU(CL *cl, int height) : cl(cl), height(height) {
    std::string defines = StringPrintf("#define MAX_WAYS %d\n",
                                       MAX_WAYS);
    std::string kernel_src = defines + Util::ReadFile("sos.cl");
    std::tie(program, kernel) = cl->BuildOneKernel(kernel_src, "NWays", false);
    CHECK(kernel != 0);

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
  double t_args = 0.0;
  double t_kernel = 0.0;
  double t_read = 0.0;
  double t_ret = 0.0;
  double t_sort = 0.0;

  double t_all = 0.0;

# define TIMER_START(d) Timer d ## _timer
# define TIMER_END(d) t_ ## d += d ## _timer.Seconds()

  void PrintTimers() {
#define ONETIMER(d) printf(ACYAN( #d ) ": %s\n", AnsiTime(t_ ## d).c_str())
    ONETIMER(prep);
    ONETIMER(ml);
    ONETIMER(clear);
    ONETIMER(input);
    ONETIMER(args);
    ONETIMER(kernel);
    ONETIMER(read);
    ONETIMER(ret);
    ONETIMER(sort);
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
    // We perform a rectangular calculation. global_idx(0) is a,
    //   which ranges from 0..limit_a inclusive.
    // global_idx(1) is the target sum.
    // For any row, we want a^2 + b^2 = sum with a < b.
    // So a^2 can be at most half sqrt(sum). But we have to use the
    // same a_limit to define the rectangle; we use the largest from
    // the input.
    uint64_t limit_a = 0;
    std::vector<uint64_t> sums;
    sums.reserve(height);
    for (const auto &[sum, e_] : inputs) {
      uint64_t lim = Sqrt64(sum / 2);
      while (lim * lim < (sum / 2)) lim++;
      limit_a = std::max(limit_a, lim);
      sums.push_back(sum);
      CHECK(e_ <= MAX_WAYS) << sum << " " << e_;
    }
    TIMER_END(prep);

    std::vector<uint64_t> output_rect;
    std::vector<uint32_t> output_sizes;

    // TODO PERF: Try passing the expected number and bailing early?

    if (true) {
      // Only one GPU process at a time.
      TIMER_START(ml);
      MutexLock ml(&m);
      TIMER_END(ml);

      TIMER_START(input);
      // PERF no clFinish
      CopyBufferToGPU(cl->queue, sums, sums_gpu);
      TIMER_END(input);

      TIMER_START(clear);
      /*
      // Unnecessary to clear output space; we only look at the
      // elements up to the size.
      uint64_t SENTINEL = -1;
      CHECK_SUCCESS(
          clEnqueueFillBuffer(cl->queue,
                              output_gpu,
                              // pattern and its size in bytes
                              &SENTINEL, sizeof (uint64_t),
                              // offset and size to fill (in BYTES)
                              0, (size_t)(MAX_WAYS * 2 *
                                          sizeof (uint64_t)),
                              // no wait list or event
                              0, nullptr, nullptr));
      */

      uint32_t ZERO = 0;
      // But we do need to clear the sizes.
      CHECK_SUCCESS(
          clEnqueueFillBuffer(cl->queue,
                              output_size_gpu,
                              // pattern and its size in bytes
                              &ZERO, sizeof (uint32_t),
                              // offset and size to fill (in BYTES)
                              0, (size_t)(height * sizeof (uint32_t)),
                              // no wait list or event
                              0, nullptr, nullptr));
      // PERF don't flush queues
      clFinish(cl->queue);
      TIMER_END(clear);


      TIMER_START(args);
      CHECK_SUCCESS(clSetKernelArg(kernel, 0, sizeof (cl_mem),
                                   (void *)&sums_gpu));

      CHECK_SUCCESS(clSetKernelArg(kernel, 1, sizeof (cl_mem),
                                   (void *)&output_size_gpu));

      CHECK_SUCCESS(clSetKernelArg(kernel, 2, sizeof (cl_mem),
                                   (void *)&output_gpu));
      TIMER_END(args);

      size_t global_work_offset[] = { (size_t)0, (size_t)0 };
      // PERF try the transpose.
      //   Width: All values of a, up to and including the limit.
      //   Height: Number of sums.
      size_t global_work_size[] = { (size_t)(limit_a + 1), (size_t)height };

      TIMER_START(kernel);
      CHECK_SUCCESS(
          clEnqueueNDRangeKernel(cl->queue, kernel,
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
      TIMER_END(kernel);

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
        if (CHECK_OUTPUT) {
          CHECK(a * a + b * b == sum) << "at " << i << ": "
                                      << a << "^2 + " << b << "^2 != "
                                      << sum << "(out size " << size << ")";
        }
        one_ret.emplace_back(a, b);
      }

      TIMER_START(sort);
      std::sort(one_ret.begin(), one_ret.end(),
                [](const std::pair<uint64_t, uint64_t> &x,
                   const std::pair<uint64_t, uint64_t> &y) {
                  return x.first < y.first;
                });
      TIMER_END(sort);

      ret.push_back(std::move(one_ret));
    }
    TIMER_END(ret);


    TIMER_END(all);
    return ret;
  }

  ~NWaysGPU() {
    CHECK_SUCCESS(clReleaseKernel(kernel));
    CHECK_SUCCESS(clReleaseProgram(program));

    CHECK_SUCCESS(clReleaseMemObject(sums_gpu));
    CHECK_SUCCESS(clReleaseMemObject(output_size_gpu));
    CHECK_SUCCESS(clReleaseMemObject(output_gpu));
  }
};

#endif
