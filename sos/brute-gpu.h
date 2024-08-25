
#ifndef _SOS_BRUTE_GPU_H
#define _SOS_BRUTE_GPU_H

#include <utility>
#include <mutex>
#include <vector>
#include <string>

#include "threadutil.h"
#include "clutil.h"
#include "util.h"
#include "base/logging.h"

// Runs the (smart-ish) brute force search on GPU.
struct BruteGPU {
  CL *cl = nullptr;

  // Maximum number of interesting squares in an execution of the
  // kernel. There are almost always zero interesting squares.
  static constexpr int MAX_INTERESTING = 64;

  BruteGPU(CL *cl) : cl(cl) {
    std::string defines = "";
    std::string kernel_src = defines + Util::ReadFile("brute.cl");
    const auto &[prog, kern] =
      cl->BuildOneKernel(kernel_src, "BruteXY", false);
    CHECK(prog != 0);
    program = prog;
    CHECK(kern != 0);
    kernel = kern;

    out_size_gpu = CreateUninitializedGPUMemory<uint32_t>(cl->context, 1);
    out_gpu = CreateUninitializedGPUMemory<uint32_t>(cl->context,
                                                     MAX_INTERESTING * 2);
  }

  // Synchronized access.
  std::mutex m;
  cl_program program = 0;
  cl_kernel kernel = 0;
  cl_mem out_gpu = nullptr;
  cl_mem out_size_gpu = nullptr;

  // Returns interesting (x, y) from the range.
  std::vector<std::pair<uint32_t, uint32_t>>
  RunOne(uint32_t base,
         uint32_t y_start,
         uint32_t y_end) {
    std::vector<uint32_t> zero = {0};
    if (y_end <= y_start || y_start < 1) return {};

    // Only one GPU process at a time.
    MutexLock ml(&m);

    // Make sure the count is zero.
    CopyBufferToGPU(cl->queue, zero, out_size_gpu);

    // Run kernel.
    {
      CHECK_SUCCESS(clSetKernelArg(kernel, 0, sizeof (uint32_t),
                                   (void *)&base));

      CHECK_SUCCESS(clSetKernelArg(kernel, 1, sizeof (cl_mem),
                                   (void *)&out_size_gpu));

      CHECK_SUCCESS(clSetKernelArg(kernel, 2, sizeof (cl_mem),
                                   (void *)&out_gpu));

      // 2D kernel where x is in [1, y_end)
      //             and y is in [y_start, y_end)
      size_t global_work_offset[] = { 1, y_start };
      size_t global_work_size[] = { y_end, y_end - y_start };

      CHECK_SUCCESS(
          clEnqueueNDRangeKernel(cl->queue, kernel,
                                 // 1D
                                 2,
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

    std::vector<uint32_t> out_size =
      CopyBufferFromGPU<uint32_t>(cl->queue, out_size_gpu, 1);

    CHECK(out_size.size() == 1);
    std::vector<std::pair<uint32_t, uint32_t>> interesting;
    if (out_size[0] > 0) {
      CHECK((out_size[0] & 1) == 0) << "Must be even.";
      std::vector<uint32_t> out =
        CopyBufferFromGPU<uint32_t>(cl->queue, out_gpu, MAX_INTERESTING);

      interesting.reserve(out_size[0]);
      for (int i = 0; i < (out_size[0] >> 1); i++) {
        interesting.emplace_back(out[2 * i + 0], out[2 * i + 1]);
      }
    }

    return interesting;
  }

  ~BruteGPU() {
    CHECK_SUCCESS(clReleaseKernel(kernel));
    CHECK_SUCCESS(clReleaseProgram(program));

    CHECK_SUCCESS(clReleaseMemObject(out_size_gpu));
    CHECK_SUCCESS(clReleaseMemObject(out_gpu));
  }
};

#endif
