
#include <mutex>
#include <vector>
#include <string>
#include <tuple>

#include "ansi.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "opencl/clutil.h"
#include "periodically.h"
#include "threadutil.h"
#include "util.h"

static CL *cl = nullptr;

// Runs the (smart-ish) brute force search on GPU.
namespace {
struct FloatSqrtGPU {
  CL *cl = nullptr;

  static constexpr int MAX_REPORT = 64;

  FloatSqrtGPU(CL *cl) : cl(cl) {
    std::string defines =
      StringPrintf("#define MAX_OUTPUT_SIZE %d\n", MAX_REPORT - 1);
    std::string kernel_src = defines + Util::ReadFile("floatsqrt.cl");
    const auto &[prog, kern] =
      cl->BuildOneKernel(kernel_src, "CheckAll", false);
    CHECK(prog != 0);
    program = prog;
    CHECK(kern != 0);
    kernel = kern;

    if (std::optional<std::string> src = cl->DecodeProgram(program)) {
      Util::WriteFile("floatsqrt.ptx", src.value());
      printf("Wrote " AWHITE("floatsqrt.ptx") "\n");
    }

    out_size_gpu = CreateUninitializedGPUMemory<uint32_t>(cl->context, 1);
    // Using 64 bit, since in the kernel we actually have signed x
    // and unsigned y.
    out_gpu = CreateUninitializedGPUMemory<int64_t>(cl->context,
                                                    MAX_REPORT * 3);
  }

  // Synchronized access.
  std::mutex m;
  cl_program program = 0;
  cl_kernel kernel = 0;
  cl_mem out_gpu = nullptr;
  cl_mem out_size_gpu = nullptr;

  // returns n, err32, err64
  // when they do not match.
  std::vector<std::tuple<uint32_t, uint32_t, uint32_t>>
  RunOne(uint32_t start, uint32_t size) {

    std::vector<uint32_t> zero = {0};

    // Only one GPU process at a time.
    MutexLock ml(&m);

    // Make sure the count is zero.
    CopyBufferToGPU(cl->queue, zero, out_size_gpu);

    CHECK_SUCCESS(clSetKernelArg(kernel, 0, sizeof (cl_mem),
                                 (void *)&out_size_gpu));

    CHECK_SUCCESS(clSetKernelArg(kernel, 1, sizeof (cl_mem),
                                 (void *)&out_gpu));

    // All uint32_t.
    size_t global_work_offset[] = { start };
    size_t global_work_size[] = { size };

    // printf("Run y=[%d, %zu)\n", y_start, y_limit);
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

    CHECK(out_size.size() == 1);
    std::vector<std::tuple<uint32_t, uint32_t, uint32_t>> report;
    size_t num = std::min(out_size[0], (uint32_t)MAX_REPORT);
    if (num > 0) {
      printf("There are %zu\n", num);
      std::vector<int64_t> out =
        CopyBufferFromGPU<int64_t>(cl->queue, out_gpu, MAX_REPORT * 3);

      report.reserve(num);
      for (int i = 0; i < num; i++) {
        report.emplace_back((uint32_t)out[3 * i + 0],
                            (uint32_t)out[3 * i + 1],
                            (uint32_t)out[3 * i + 2]);
      }
    }

    return report;
  }

  ~FloatSqrtGPU() {
    CHECK_SUCCESS(clReleaseKernel(kernel));
    CHECK_SUCCESS(clReleaseProgram(program));

    CHECK_SUCCESS(clReleaseMemObject(out_size_gpu));
    CHECK_SUCCESS(clReleaseMemObject(out_gpu));
  }
};
}  // namespace

static void TestFloatSqrt() {
  FloatSqrtGPU fsgpu(cl);

  int64_t total_wrong = 0;
  // static constexpr uint64_t LIMIT = 0x1'0000'0000;
  static constexpr uint64_t LIMIT = 0xFFFF'0000;
  // static constexpr uint64_t LIMIT = 4'294'902'432;
  bool printed = false;
  static constexpr uint32_t CHUNK = 1 << 21;
  Periodically status_per(1.0);


  for (uint64_t start = 0; start < LIMIT; start += CHUNK) {
    uint32_t chunk_size = std::min((uint32_t)(LIMIT - start), CHUNK);
    std::vector<std::tuple<uint32_t, uint32_t, uint32_t>> wrong =
      fsgpu.RunOne(start, chunk_size);

    if (!wrong.empty()) {
      if (!printed) {
        for (const auto &[n, e32, e64] : wrong) {
          printf(AWHITE("%u") " %u " ARED("≠") " %u\n",
                 n, e32, e64);
        }
        printed = true;
        LOG(FATAL) << "Exit early.\n";
      }
      total_wrong += wrong.size();
    }

    if (status_per.ShouldRun()) {
      printf("%llu/%llu\n", start, LIMIT);
    }
  }

  if (total_wrong > 0) {
    printf("Total wrong: " ARED("%lld") "\n", total_wrong);
  } else {
    printf(AGREEN("All correct.") "\n");
  }
}

int main(int argc, char **argv) {
  ANSI::Init();
  cl = new CL;

  CHECK(cl);

  TestFloatSqrt();

  return 0;
}

