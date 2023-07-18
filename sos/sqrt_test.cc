
#include <utility>
#include <mutex>
#include <vector>
#include <string>
#include <optional>

#include "threadutil.h"
#include "clutil.h"
#include "util.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "timer.h"
#include "ansi.h"
#include "map-util.h"
#include "sos-util.h"
#include "arcfour.h"
#include "randutil.h"

static CL *cl = nullptr;

static void TestSqrt() {
  std::string kernel_src = Util::ReadFile("sqrt.cl");
  const auto &[program, kernel] =
    cl->BuildOneKernel(kernel_src, "SquareRoot", false);
  CHECK(program != 0);
  CHECK(kernel != 0);

  std::optional<std::string> ptx = cl->DecodeProgram(program);
  CHECK(ptx.has_value());
  Util::WriteFile("sqrt.ptx", ptx.value());
  printf("\n");
  printf("Wrote sqrt.ptx\n");

  constexpr int NUMBER = 131072 * 8;

  ArcFour rc("sqrt.cl");
  static constexpr int ITERS = 64;
  Timer run_timer;
  for (int iter = 0; iter < ITERS; iter++) {
    std::vector<uint64_t> inputs;
    inputs.reserve(NUMBER);

    if (iter == 0)
      inputs.push_back(0);

    // Make sure there are some squares in there.
    for (int i = 0; i < NUMBER * 0.25; i++) {
      uint32_t r = Rand32(&rc);
      inputs.push_back(r * r);
    }

    while (inputs.size() < NUMBER) {
      inputs.push_back(Rand64(&rc));
    }
    Shuffle(&rc, &inputs);

    cl_mem inputs_gpu = CopyMemoryToGPU(cl->context, cl->queue, inputs);

    cl_mem outputs_gpu =
      CreateUninitializedGPUMemory<uint64_t>(cl->context, NUMBER);

    CHECK_SUCCESS(clSetKernelArg(kernel, 0, sizeof (cl_mem),
                                 (void *)&inputs_gpu));

    CHECK_SUCCESS(clSetKernelArg(kernel, 1, sizeof (cl_mem),
                                 (void *)&outputs_gpu));

    size_t global_work_offset[] = { (size_t)0 };
    size_t global_work_size[] = { (size_t)NUMBER };

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

    std::vector<uint64_t> outputs =
      CopyBufferFromGPU<uint64_t>(cl->queue, outputs_gpu, NUMBER);

    int64_t correct = 0;
    for (int i = 0; i < NUMBER; i++) {
      uint64_t rr = inputs[i];
      uint64_t g = outputs[i];
      /*
        auto ro = Sqrt64Opt(rr);
        if (ro.has_value()) {
        CHECK(g * g == rr && g == ro.value()) <<
        rr << " " << g << " " << ro.value();
        } else {
        CHECK(g == 0) << rr << " " << g;
        }
      */
      uint64_t gg = g * g;
      if (gg == rr) {
        // Correct!
        correct++;
      } else {
        // It must be the largest integer less than the square root, then.
        CHECK(gg < rr) << "After getting " << correct << " correct:\n"
          "rr: " << rr << " gg: " << gg << " g: " << g;
        // Don't need to check the largest integer (and in fact it overflows).
        if (g < 4294967295) {
          uint64_t gpgp = (g + 1) * (g + 1);
          CHECK(rr < gpgp) << "After getting " << correct << " correct:\n"
            "rr: " << rr << " gpgp: " << gpgp << " g: " << g;
        }

        correct++;
      }
    }

    CHECK_SUCCESS(clReleaseMemObject(outputs_gpu));
    CHECK_SUCCESS(clReleaseMemObject(inputs_gpu));

    printf(ANSI_PREVLINE ANSI_BEGINNING_OF_LINE ANSI_CLEARLINE
           ANSI_BEGINNING_OF_LINE "%s\n",
           ANSI::ProgressBar(iter,
                             ITERS,
                             "test sqrt",
                             run_timer.Seconds()).c_str());
  }

  CHECK_SUCCESS(clReleaseKernel(kernel));
  CHECK_SUCCESS(clReleaseProgram(program));

  printf("GPU Sqrt " AGREEN("OK") "\n");
}

int main(int argc, char **argv) {
  ANSI::Init();
  cl = new CL;

  TestSqrt();

  printf("OK\n");
  return 0;
}
