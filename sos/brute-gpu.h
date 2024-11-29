
#ifndef _SOS_BRUTE_GPU_H
#define _SOS_BRUTE_GPU_H

#include <utility>
#include <mutex>
#include <vector>
#include <string>

#include "ansi.h"
#include "threadutil.h"
#include "clutil.h"
#include "util.h"
#include "base/logging.h"
#include "base/stringprintf.h"

// Runs the (smart-ish) brute force search on GPU.
struct BruteGPU {
  CL *cl = nullptr;

  // Maximum number of interesting squares in an execution of the
  // kernel. There are almost always zero interesting squares, but
  // it's not really expensive to have a large max here.
  static constexpr int MAX_INTERESTING = 131072;
  static constexpr int MAX_Y_PER_KERNEL = 16384;

  BruteGPU(CL *cl, const int report_threshold[10]) : cl(cl) {
    std::string defines =
      StringPrintf("#define MAX_INTERESTING %d\n", MAX_INTERESTING);

    defines += "static uint INTERESTING_THRESHOLD[10] = {\n";
    for (int i = 0; i < 10; i++) {
      StringAppendF(&defines, "  %d,  // %d non-squares\n",
                    report_threshold[i]);
    }
    StringAppendF(&defines, "};\n\n");

    std::string kernel_src = defines + Util::ReadFile("brute.cl");
    const auto &[prog, kern] =
      cl->BuildOneKernel(kernel_src, "BruteXY", false);
    CHECK(prog != 0);
    program = prog;
    CHECK(kern != 0);
    kernel = kern;

    if (std::optional<std::string> src = cl->DecodeProgram(program)) {
      Util::WriteFile("brute.ptx", src.value());
      printf("Wrote " AWHITE("brute.ptx") "\n");
    }

    out_size_gpu = CreateUninitializedGPUMemory<uint32_t>(cl->context, 1);
    // Using 64 bit, since in the kernel we actually have signed x
    // and unsigned y.
    out_gpu = CreateUninitializedGPUMemory<int64_t>(cl->context,
                                                    MAX_INTERESTING * 2);
  }

  // Synchronized access.
  std::mutex m;
  cl_program program = 0;
  cl_kernel kernel = 0;
  cl_mem out_gpu = nullptr;
  cl_mem out_size_gpu = nullptr;

  // Returns interesting (x, y) from the range.
  // PERF: Automatically chunk when y size is too big
  std::vector<std::pair<int32_t, uint32_t>>
  RunOne(uint32_t base,
         uint32_t y_start_in,
         uint32_t y_end_in,
         int64_t *executed) {

    uint32_t y_start = y_start_in;
    uint32_t y_end = y_end_in;

    std::vector<uint32_t> zero = {0};

    if (y_start < 1) y_start = 1;
    if (y_end <= y_start) return {};

    // Only one GPU process at a time.
    MutexLock ml(&m);

    // Make sure the count is zero.
    CopyBufferToGPU(cl->queue, zero, out_size_gpu);
    // const std::vector<int64_t> init(MAX_INTERESTING * 2, 12345678);
    // CopyBufferToGPU(cl->queue, init, out_gpu);
    clFinish(cl->queue);

    if (false) {
      std::vector<int64_t> out =
        CopyBufferFromGPU<int64_t>(cl->queue, out_gpu, MAX_INTERESTING * 2);
      for (int i = 0; i < (int)out.size(); i++) {
        printf("init %d. %lld\n", i, out[i]);
      }
    }

    {
      std::vector<uint32_t> out_size =
        CopyBufferFromGPU<uint32_t>(cl->queue, out_size_gpu, 1);

      CHECK(out_size.size() == 1);
      CHECK(out_size[0] == 0);
    }

    // 2D kernel where x is in [1, y_end)
    //             and y is in [y_start, y_end)

    // Run kernel.
    while (y_start < y_end) {
      CHECK_SUCCESS(clSetKernelArg(kernel, 0, sizeof (uint32_t),
                                   (void *)&base));

      CHECK_SUCCESS(clSetKernelArg(kernel, 1, sizeof (cl_mem),
                                   (void *)&out_size_gpu));

      CHECK_SUCCESS(clSetKernelArg(kernel, 2, sizeof (cl_mem),
                                   (void *)&out_gpu));

      // Don't run a rectangle that's too big. This can cause
      // desktop latency. It's also more efficient to chunk
      // because we only run the lower triangle.
      size_t y_limit = std::min(y_start + MAX_Y_PER_KERNEL, y_end);

      // x starts from -base/2. Since the offsets are unsigned, we
      // start x at zero and subtract in the kernel. This will
      // include the degenerate value of zero (hence the + 1), but
      // it's not harmful to also compute some degenerate squares.
      size_t x_range = base / 2 + y_limit + 1;

      // 2D kernel where x is in [0, x_range)
      //             and y is in [y_start, y_limit)
      size_t global_work_offset[] = { 0, y_start };
      size_t global_work_size[] = { x_range, y_limit - y_start };

      *executed += x_range * (y_limit - y_start);

      // printf("Run y=[%d, %zu)\n", y_start, y_limit);
      CHECK_SUCCESS(
          clEnqueueNDRangeKernel(cl->queue, kernel,
                                 // 2D
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
      // Advance to next chunk.
      y_start = y_limit;
    }

    std::vector<uint32_t> out_size =
      CopyBufferFromGPU<uint32_t>(cl->queue, out_size_gpu, 1);

    CHECK(out_size.size() == 1);
    const int num = out_size[0];
    CHECK(num <= MAX_INTERESTING) << "invalid: " << num;

    std::vector<std::pair<int32_t, uint32_t>> interesting;
    if (num > 0) {
      std::vector<int64_t> out =
        CopyBufferFromGPU<int64_t>(cl->queue, out_gpu, MAX_INTERESTING * 2);

      interesting.reserve(num);
      for (int i = 0; i < num; i++) {
        int64_t xx = out[i * 2 + 0];
        int64_t yy = out[i * 2 + 1];
        /*
        printf("%d/%d. %d  %lld,%lld\n",
               i, num, base,
               xx, yy);
        */
        interesting.emplace_back((int32_t)xx, (uint32_t)yy);
      }

      if (num >= MAX_INTERESTING) {
        for (int i = 0; i < 3 && i < (int)interesting.size(); i++) {
          printf("e.g. %u %d %u\n", base,
                 interesting[i].first,
                 interesting[i].second);
        }

        LOG(FATAL) << "Exhausted slots at "
                   << base << " "
                   << y_start_in << " "
                   << y_end_in << " (got " << num << ")";
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
