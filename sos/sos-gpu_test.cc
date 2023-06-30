
#include "sos-util.h"
#include "sos-gpu.h"
#include "clutil.h"

#include "base/logging.h"
#include "ansi.h"
#include "periodically.h"
#include "opt/opt.h"
#include "arcfour.h"
#include "randutil.h"

static CL *cl = nullptr;

static constexpr bool CHECK_ANSWERS = true;

static constexpr int GLOBAL_BATCH_SIZE = 65536;

// XXX do
static std::vector<std::pair<uint64_t, uint32_t>> global_batch;
static void MakeBatch() {
  // Make batch and put it in the global.
  Timer batch_timer;
  global_batch.clear();
  global_batch.reserve(GLOBAL_BATCH_SIZE);

  std::mutex m;
  static constexpr uint64_t START = 10'000'000'000ULL; /* ' */
  static constexpr int THREADS = 6;
  ParallelComp(
      THREADS,
      [&m](int idx) {
        uint64_t sum = START + idx;
        for (;;) {
          int expected = ChaiWahWu(sum);
          if (expected >= 3 && expected <= NWaysGPU::MAX_WAYS) {
            MutexLock ml(&m);
            // Someone else may have finished it.
            if (global_batch.size() == GLOBAL_BATCH_SIZE)
              return;
            global_batch.emplace_back(sum, (uint32_t)expected);
            // Exit right away.
            if (global_batch.size() == GLOBAL_BATCH_SIZE)
              return;
          }
          sum += THREADS;
        }
      }, THREADS);

  printf("Made batch in %s\n", AnsiTime(batch_timer.Seconds()).c_str());
}


static int best_height = 0;
static double best_sec_per = 999999.0;
static ArcFour *rc = nullptr;
double OptimizeMe(double h) {
  CHECK(rc != nullptr);

  int height = (int)std::round(h);
  if (height < 1 || height > GLOBAL_BATCH_SIZE) return 9999999.0;

  // Create a batch of the target height
  std::vector<std::pair<uint64_t, uint32_t>> batch =
    global_batch;

  Shuffle(rc, &batch);
  batch.resize(height);
  NWaysGPU nways_gpu(cl, height);

  Timer run_timer;
  std::vector<std::vector<std::pair<uint64_t, uint64_t>>> res =
    nways_gpu.GetNWays(batch);
  double sec = run_timer.Seconds();

  double sec_per = sec / height;

  if (sec_per < best_sec_per) {
    best_height = height;
    best_sec_per = sec_per;
    printf(AGREEN("New best") ": %d (%s/ea.)\n", best_height,
           AnsiTime(best_sec_per).c_str());
  }
  return sec_per;
}

static void Optimize() {
  rc = new ArcFour(StringPrintf("gpu.%lld", time(nullptr)));
  printf("Make batch...\n");
  MakeBatch();
  printf("... done.\n");
  CHECK((int)global_batch.size() == GLOBAL_BATCH_SIZE);

  // Convenience versions for small N.
  // Returns {best_arg, f(best_arg)}.
  const auto &[height, sec_per] =
    Opt::Minimize1D(
        OptimizeMe,
        // bounds
        1.0, (double)GLOBAL_BATCH_SIZE,
        100,
        1, 10,
        Rand64(rc));

  printf("Optimization finished. Best was " APURPLE("%.3f")
         " which took %s/ea.\n", height,
         AnsiTime(sec_per).c_str());
  printf("\n");
}

static void TestNWays() {
  printf("Test...\n");
  const int height = 512;
  NWaysGPU nways_gpu(cl, height);
  std::map<int, int> too_big;

  double batch_sec = 0.0;
  double gpu_sec = 0.0;
  double cpu_sec = 0.0;

  // old was 3.716 CPU, 15.259 GPU
  // batchedx512 CPU: 17.405s
  //             GPU: 5.006s

  Timer run_timer;
  Periodically bar_per(1.0);
  uint64_t sum = 1000000000ULL;
  static constexpr int NUM_BATCHES = 512;
  for (int batch_idx = 0; batch_idx < NUM_BATCHES; batch_idx++) {
    // Make batch.
    Timer batch_timer;
    std::vector<std::pair<uint64_t, uint32_t>> batch;
    batch.reserve(height);
    while (batch.size() < height) {
      int expected = ChaiWahWu(sum);
      if (expected >= 3) {
        if (expected > NWaysGPU::MAX_WAYS) {
          too_big[expected]++;
        } else {
          batch.emplace_back(sum, (uint32_t)expected);
        }
      }
      sum++;
    }
    batch_sec += batch_timer.Seconds();

    Timer cpu_timer;
    std::vector<std::vector<std::pair<uint64_t, uint64_t>>> outs_cpu;
    outs_cpu.reserve(height);
    // XXX not fair to cpu version. Use parallelism. Use expected!
    for (const auto &[sum, expected] : batch) {
      outs_cpu.push_back(BruteGetNWays(sum));
    }
    cpu_sec += cpu_timer.Seconds();

    CHECK((int)outs_cpu.size() == height);

    Timer gpu_timer;
    std::vector<std::vector<std::pair<uint64_t, uint64_t>>> outs_gpu =
      nways_gpu.GetNWays(batch);
    gpu_sec += gpu_timer.Seconds();

    CHECK((int)outs_gpu.size() == height);

    if (CHECK_ANSWERS) {
      for (int row = 0; row < height; row++) {
        const auto &out_cpu = outs_cpu[row];
        const auto &out_gpu = outs_gpu[row];
        CHECK(out_gpu == out_cpu) << "Sum: " << batch[row].first << "\n"
          "CPU: " << WaysString(out_cpu) << "\n"
          "GPU: " << WaysString(out_gpu) << "\n";
      }
    }

    if (bar_per.ShouldRun()) {
      printf(ANSI_PREVLINE ANSI_BEGINNING_OF_LINE ANSI_CLEARLINE
             ANSI_BEGINNING_OF_LINE "%s\n",
             AnsiProgressBar(batch_idx,
                             NUM_BATCHES,
                             "test",
                             run_timer.Seconds()).c_str());
    }
  }

  if constexpr (!CHECK_ANSWERS) {
    printf(ARED("Note") ": Answers not checked.\n");
  }

  printf("Too big:\n");
  for (const auto [ways, count] : too_big) {
    printf("  %d x%d\n", ways, count);
  }

  printf("Height: " APURPLE("%d") "\n"
         "Time:\n"
         "Batches: %s\n"
         "CPU: %s\n"
         "GPU: %s\n",
         height,
         AnsiTime(batch_sec).c_str(),
         AnsiTime(cpu_sec).c_str(),
         AnsiTime(gpu_sec).c_str());

  nways_gpu.PrintTimers();
}

int main(int argc, char **argv) {
  AnsiInit();
  cl = new CL;

  Optimize();

  TestNWays();

  printf("OK\n");
  return 0;
}
