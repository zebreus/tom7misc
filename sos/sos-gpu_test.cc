
#include "sos-util.h"
#include "sos-gpu.h"
#include "clutil.h"

#include "base/logging.h"
#include "ansi.h"

static CL *cl = nullptr;

static constexpr bool CHECK_ANSWERS = true;

static void TestNWays() {
  NWaysGPU nways_gpu(cl, 1);

  double gpu_sec = 0.0;
  double cpu_sec = 0.0;

  for (uint64_t sum = 1000000000ULL; sum < 1000100000ULL; sum++) {
    Timer cpu_timer;
    std::vector<std::pair<uint64_t, uint64_t>> out_cpu =
      BruteGetNWays(sum);
    cpu_sec += cpu_timer.Seconds();

    Timer gpu_timer;
    // XXX compute second arg. but it is unused.
    std::vector<std::pair<uint64_t, uint32_t>> in_gpu = {{sum, 3}};
    std::vector<std::vector<std::pair<uint64_t, uint64_t>>> outs_gpu =
      nways_gpu.GetNWays(in_gpu);
    const auto out_gpu = outs_gpu[0];
    gpu_sec += gpu_timer.Seconds();

    if constexpr (CHECK_ANSWERS) {
      CHECK(out_gpu == out_cpu) << "Sum: " << sum << "\n"
        "CPU: " << WaysString(out_cpu) << "\n"
        "GPU: " << WaysString(out_gpu) << "\n";
    }
  }

  if constexpr (!CHECK_ANSWERS) {
    printf(ARED("Note") ": Answers not checked.\n");
  }

  printf("Time:\n"
         "CPU: %s\n"
         "GPU: %s\n",
         AnsiTime(cpu_sec).c_str(),
         AnsiTime(gpu_sec).c_str());

  nways_gpu.PrintTimers();
}

int main(int argc, char **argv) {
  AnsiInit();
  cl = new CL;

  TestNWays();

  printf("OK\n");
  return 0;
}
