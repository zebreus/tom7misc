
#ifndef _SOS_BRUTESQ_GPU_H
#define _SOS_BRUTESQ_GPU_H

#include <mutex>
#include <vector>
#include <string>

#include "ansi.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "opencl/clutil.h"
#include "threadutil.h"
#include "util.h"

// Runs the (smart-ish) brute force search (with u,v squares) on GPU.
// Based on brute-gpu.h
struct BruteSqGPU {
  CL *cl = nullptr;

  // Maximum number of interesting squares in an execution of the
  // kernel. There are almost always zero interesting squares, but
  // it's not really expensive to have a large max here.
  static constexpr int MAX_INTERESTING = 131072;

  BruteSqGPU(CL *cl, const int report_threshold[10]) : cl(cl) {
    std::string defines =
      StringPrintf("#define MAX_INTERESTING %d\n", MAX_INTERESTING);

    defines += "static uint INTERESTING_THRESHOLD[10] = {\n";
    for (int i = 0; i < 10; i++) {
      StringAppendF(&defines, "  %d,  // %d non-squares\n",
                    report_threshold[i]);
    }
    StringAppendF(&defines, "};\n\n");

    std::string kernel_src = defines + Util::ReadFile("brutesq.cl");
    const auto &[prog, kern] =
      cl->BuildOneKernel(kernel_src, "BruteX", false);
    CHECK(prog != 0);
    program = prog;
    CHECK(kern != 0);
    kernel = kern;

    if (std::optional<std::string> src = cl->DecodeProgram(program)) {
      Util::WriteFile("brutesq.ptx", src.value());
      printf("Wrote " AWHITE("brutesq.ptx") "\n");
    }

    out_size_gpu = CreateUninitializedGPUMemory<uint32_t>(cl->context, 1);
    // Using 64 bit, since in the kernel we actually have signed x
    // and unsigned y.
    out_gpu = CreateUninitializedGPUMemory<int64_t>(cl->context,
                                                    MAX_INTERESTING);
  }

  // Synchronized access.
  std::mutex m;
  cl_program program = 0;
  cl_kernel kernel = 0;
  cl_mem out_gpu = nullptr;
  cl_mem out_size_gpu = nullptr;

  const std::vector<uint32_t> zero = {0};

  // Unlike brute-gpu, this is just a 1D kernel.
  // Returns interesting x from the range.
  std::vector<int64_t>
  RunOne(int64_t base,
         int64_t y,
         int64_t x_start, int64_t x_end,
         int64_t *executed) {

    if (x_start == 0) x_start++;

    int64_t x_range = x_end - x_start;
    if (x_range <= 0) return {};

    // Only one GPU process at a time.
    MutexLock ml(&m);

    // Make sure the count is zero.
    CopyBufferToGPU(cl->queue, zero, out_size_gpu);
    clFinish(cl->queue);

    // Run kernel.
    CHECK_SUCCESS(clSetKernelArg(kernel, 0, sizeof (int64_t),
                                 (void *)&base));
    CHECK_SUCCESS(clSetKernelArg(kernel, 1, sizeof (int64_t),
                                 (void *)&y));
    CHECK_SUCCESS(clSetKernelArg(kernel, 2, sizeof (cl_mem),
                                 (void *)&out_size_gpu));
    CHECK_SUCCESS(clSetKernelArg(kernel, 3, sizeof (cl_mem),
                                 (void *)&out_gpu));

    size_t global_work_offset[] = { (size_t)x_start };
    size_t global_work_size[] = { (size_t)x_range };

    CHECK_SUCCESS(
        clEnqueueNDRangeKernel(cl->queue, kernel,
                               // 1D
                               1,
                               global_work_offset,
                               global_work_size,
                               // No local work
                               nullptr,
                               // No wait list
                               0, nullptr,
                               // no event
                               nullptr));

    clFinish(cl->queue);

    std::vector<uint32_t> out_size =
      CopyBufferFromGPU<uint32_t>(cl->queue, out_size_gpu, 1);

    *executed = x_range;

    CHECK(out_size.size() == 1);
    const int num = out_size[0];
    CHECK(num <= MAX_INTERESTING) << "invalid: " << num;

    /*
    printf("For base=%lld y=%lld x=%lld-%lld, num=%d\n",
           base, y, x_start, x_end, num);
    */

    if (num > 0) {
      std::vector<int64_t> interesting =
        CopyBufferFromGPU<int64_t>(cl->queue, out_gpu, num);

      if (num >= MAX_INTERESTING) {
        for (int i = 0; i < 3 && i < (int)interesting.size(); i++) {
          printf("e.g. %lld %lld %lld\n", base, y, interesting[i]);
        }

        LOG(FATAL) << "Exhausted slots at "
                   << base << " "
                   << y << " (got " << num << ")";
      }

      return interesting;
    }

    return {};
  }

  ~BruteSqGPU() {
    CHECK_SUCCESS(clReleaseKernel(kernel));
    CHECK_SUCCESS(clReleaseProgram(program));

    CHECK_SUCCESS(clReleaseMemObject(out_size_gpu));
    CHECK_SUCCESS(clReleaseMemObject(out_gpu));
  }
};

#endif
