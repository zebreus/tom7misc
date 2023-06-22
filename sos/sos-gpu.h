
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
#include "sos-util.h"
#include "timer.h"

struct NWaysGPU {
  // This is probably 1-2 orders of magnitude too high, but it
  // doesn't really matter. We just want to make sure the code
  // never writes off the end.
  static constexpr int MAX_WAYS = 512;

  CL *cl = nullptr;

  NWaysGPU(CL *cl) : cl(cl) {
    std::string defines = "";
    std::string kernel_src = defines + Util::ReadFile("sos.cl");
    std::tie(program, kernel) = cl->BuildOneKernel(kernel_src, "NWays");
    CHECK(kernel != 0);

    // Two 64-bit words per way.
    // We keep reusing this buffer.
    output_gpu =
      CreateUninitializedGPUMemory<uint64_t>(cl->context, MAX_WAYS * 2);

    output_idx_gpu =
      CreateUninitializedGPUMemory<uint32_t>(cl->context, 1);
  }

  // Synchronized access.
  std::mutex m;
  cl_program program = 0;
  cl_kernel kernel = 0;
  cl_mem output_gpu = nullptr;
  cl_mem output_idx_gpu = nullptr;

  std::vector<std::pair<uint64_t, uint64_t>>
  GetNWays(uint64_t sum) {
    // Since a^2 + b^2 = sum, but a < b, a^2 can actually
    // be at most half the square root.
    uint64_t limit_a = Sqrt64(sum / 2);
    while (limit_a * limit_a < (sum / 2)) limit_a++;
    // limit_a = std::min((uint64_t)128, limit_a);

    std::vector<uint64_t> output;
    uint32_t size = 0;

    {
      // Only one GPU process at a time.
      MutexLock ml(&m);

      uint64_t SENTINEL = -1;
      // Maybe unnecessary. We have the length at the end.
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

      uint32_t ZERO = 0;
      // But we do need to reset this.
      CHECK_SUCCESS(
          clEnqueueFillBuffer(cl->queue,
                              output_idx_gpu,
                              // pattern and its size in bytes
                              &ZERO, sizeof (uint32_t),
                              // offset and size to fill (in BYTES)
                              0, (size_t)(1 * sizeof (uint32_t)),
                              // no wait list or event
                              0, nullptr, nullptr));
      clFinish(cl->queue);

      CHECK_SUCCESS(clSetKernelArg(kernel, 0, sizeof (uint64_t),
                                   (void *)&sum));

      CHECK_SUCCESS(clSetKernelArg(kernel, 1, sizeof (cl_mem),
                                   (void *)&output_idx_gpu));

      CHECK_SUCCESS(clSetKernelArg(kernel, 2, sizeof (cl_mem),
                                   (void *)&output_gpu));

      // All values of a, up to and including the limit.
      size_t global_work_offset[] = { (size_t)0, };
      size_t global_work_size[] = { limit_a + 1, };
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

      size =
        CopyBufferFromGPU<uint32_t>(cl->queue, output_idx_gpu, 1)[0];
      output =
        CopyBufferFromGPU<uint64_t>(cl->queue, output_gpu, MAX_WAYS * 2);
    }
    // Done with GPU.
    std::vector<std::pair<uint64_t, uint64_t>> ret;
    CHECK(size % 2 == 0) << "Size is supposed to be incremented by 2 each "
      "time. " << size << " " << sum;
    ret.reserve(size / 2);
    for (int i = 0; i < size / 2; i++) {
      // PERF unnecessary to check
      uint64_t a = output[i * 2 + 0];
      uint64_t b = output[i * 2 + 1];
      CHECK(a * a + b * b == sum) << "at " << i << ": "
                                  << a << "^2 + " << b << "^2 != " << sum;
      ret.emplace_back(a, b);
    }
    std::sort(ret.begin(), ret.end(),
              [](const std::pair<uint64_t, uint64_t> &x,
                 const std::pair<uint64_t, uint64_t> &y) {
                return x.first < y.first;
              });
    return ret;
  }

  ~NWaysGPU() {
    CHECK_SUCCESS(clReleaseKernel(kernel));
    CHECK_SUCCESS(clReleaseProgram(program));

    CHECK_SUCCESS(clReleaseMemObject(output_idx_gpu));
    CHECK_SUCCESS(clReleaseMemObject(output_gpu));
  }
};

#endif
