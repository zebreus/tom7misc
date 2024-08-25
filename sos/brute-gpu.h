
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
#include "base/stringprintf.h"
#include "sos-util.h"
#include "timer.h"
#include "ansi.h"
#include "map-util.h"

// Runs the (smart-ish) brute force search on GPU.
struct BruteGPU {
  CL *cl = nullptr;

  BruteGPU(CL *cl) : cl(cl) {
    std::string defines = "";
    std::string kernel_src = defines + Util::ReadFile("brute.cl");
    const auto &[prog, kern] =
      cl->BuildOneKernel(kernel_src, "BruteXY", false);
    CHECK(prog != 0);
    program = prog;
    CHECK(kern != 0);
    kernel = kern;

    out_gpu = CreateUninitializedGPUMemory<uint32_t>(cl->context);
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

#endif
