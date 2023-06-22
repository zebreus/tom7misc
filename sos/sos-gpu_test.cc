
#include "sos-util.h"
#include "sos-gpu.h"
#include "clutil.h"

#include "base/logging.h"
#include "ansi.h"

static CL *cl = nullptr;

static constexpr bool CHECK_ANSWERS = true;

static void TestNWays() {
  NWaysGPU nways_gpu(cl);

  double gpu_sec = 0.0;
  double cpu_sec = 0.0;

  for (uint64_t sum = 1000000000ULL; sum < 1000100000ULL; sum++) {
    Timer gpu_timer;
    std::vector<std::pair<uint64_t, uint64_t>> out_gpu =
      nways_gpu.GetNWays(sum);
    gpu_sec += gpu_timer.Seconds();

    Timer cpu_timer;
    std::vector<std::pair<uint64_t, uint64_t>> out_cpu =
      BruteGetNWays(sum);
    cpu_sec += cpu_timer.Seconds();

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
}

int main(int argc, char **argv) {
  AnsiInit();
  cl = new CL;

  TestNWays();

  printf("OK\n");
  return 0;
}
