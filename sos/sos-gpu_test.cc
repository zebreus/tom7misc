
#include "sos-util.h"
#include "sos-gpu.h"
#include "clutil.h"

#include "base/logging.h"
#include "ansi.h"
#include "periodically.h"
#include "opt/opt.h"
#include "arcfour.h"
#include "randutil.h"
#include "threadutil.h"
#include "atomic-util.h"

static CL *cl = nullptr;

// Enable this to actually test (as opposed to benchmark) the GPU code.
static constexpr bool TEST_AGAINST_CPU = false;

static constexpr bool CHECK_ANSWERS = true;

static constexpr int GLOBAL_BATCH_SIZE = 131072;

DECLARE_COUNTERS(ineligible, u1_, u2_, u3_, u4_, u5_, u6, u7_);

static std::vector<std::pair<uint64_t, uint32_t>> global_batch;
template<class GPUMethod>
static void MakeBatch() {
  // Make batch and put it in the global.
  Timer batch_timer;
  global_batch.clear();
  global_batch.reserve(GLOBAL_BATCH_SIZE);

  std::mutex m;
  static constexpr uint64_t START = 800'000'000'000ULL; /* ' */
  static constexpr int THREADS = 6;
  ParallelComp(
      THREADS,
      [&m](int idx) {
        uint64_t sum = START + idx;
        for (;;) {
          int expected = ChaiWahWu(sum);
          if (expected >= 3 && expected <= GPUMethod::MAX_WAYS) {
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

  printf("Made batch in %s\n", ANSI::Time(batch_timer.Seconds()).c_str());
}

static constexpr int NUM_PASSES = 10;

static int best_height = 0;
static double best_sec_per = 999999.0;
static ArcFour *rc = nullptr;
template<class GPUMethod>
double OptimizeMe(double h) {
  CHECK(rc != nullptr);

  int height = (int)std::round(h);
  if (height < 1 || height > GLOBAL_BATCH_SIZE) return 9999999.0;

  GPUMethod ways_gpu(cl, height);

  double sec = 0.0;
  for (int p = 0; p < NUM_PASSES; p++) {
    // Create a batch of the target height
    std::vector<std::pair<uint64_t, uint32_t>> batch =
      global_batch;

    Shuffle(rc, &batch);
    batch.resize(height);

    Timer run_timer;
    std::vector<std::vector<std::pair<uint64_t, uint64_t>>> res =
      ways_gpu.GetWays(batch);
    sec += run_timer.Seconds();
  }


  double sec_per = sec / (height * NUM_PASSES);

  if (sec_per < best_sec_per) {
    best_height = height;
    best_sec_per = sec_per;
    printf(AGREEN("New best") ": %d (%s/ea.)\n", best_height,
           ANSI::Time(best_sec_per).c_str());
  }
  return sec_per;
}

// Too slow now :(
// Maybe better to just sample the 1D interval?
template<class GPUMethod>
static void Optimize() {
  rc = new ArcFour(StringPrintf("gpu.%lld", time(nullptr)));
  printf("Make batch...\n");
  MakeBatch<GPUMethod>();
  printf("... done.\n");
  CHECK((int)global_batch.size() == GLOBAL_BATCH_SIZE);

  // The first execution(s) of the kernel take longer; perhaps because
  // there is some internal JIT or branch predictors or whatever. Try
  // to avoid sampling these during optimization.
  //
  // (Actually, the observed behavior seems to suggest that even after
  // warm-up, you need to run the kernel several times with the same
  // batch size in order to see its steady-state performance. So maybe
  // the OptimizeMe function should run in a loop and take the min.)
  Timer warm_timer;
  printf("Warming kernels...\n");
  while (warm_timer.Seconds() < 5.0) (void)OptimizeMe<GPUMethod>(GLOBAL_BATCH_SIZE);
  printf("... done.\n");
  // reset best
  best_sec_per = 999999.0;

  // Returns {best_arg, f(best_arg)}.
  const auto &[height, sec_per] =
    Opt::Minimize1D(
        OptimizeMe<GPUMethod>,
        // bounds
        4096.0, (double)GLOBAL_BATCH_SIZE,
        25,
        1, 10,
        Rand64(rc));

  printf("Optimization finished. Best was " APURPLE("%.3f")
         " which took %s/ea.\n", height,
         ANSI::Time(sec_per).c_str());
  printf("\n");
}

template<class GPUMethod, bool USE_CPU, int NUM_BATCHES>
static void TestWays(const char * method) {
  printf("Test...\n");
  // const int height = 16384;
  // static constexpr int GPU_HEIGHT = 56002;
  static constexpr int GPU_HEIGHT = 65536 * 2;
  static constexpr uint64_t SUM_START = 800'000'000'000ULL;
  GPUMethod ways_gpu(cl, GPU_HEIGHT);
  std::map<int, int> too_big;

  double batch_sec = 0.0;
  double gpu_sec = 0.0;
  double cpu_sec = 0.0;
  double check_sec = 0.0;

  Timer run_timer;
  Periodically bar_per(1.0);
  uint64_t sum = SUM_START;
  for (int batch_idx = 0; batch_idx < NUM_BATCHES; batch_idx++) {
    // Make batch.
    Timer batch_timer;
    std::vector<std::pair<uint64_t, uint32_t>> batch;
    batch.reserve(GPU_HEIGHT);
    std::mutex m;
    ParallelFan(6,
                [&sum, &batch, &m, &too_big](int id) {
                  for (;;) {
                    uint64_t mine = 0;
                    {
                      MutexLock ml(&m);
                      mine = sum;
                      sum++;
                      if (batch.size() == GPU_HEIGHT)
                        return;
                    }

                    int expected = ChaiWahWu(mine);
                    if (expected >= 3) {
                      MutexLock ml(&m);
                      if (expected > GPUMethod::MAX_WAYS) {
                        too_big[expected]++;
                      } else {
                        if (batch.size() < GPU_HEIGHT) {
                          batch.emplace_back(mine, (uint32_t)expected);
                        }
                      }
                    }
                  }
                });
    batch_sec += batch_timer.Seconds();

    std::vector<std::vector<std::pair<uint64_t, uint64_t>>> outs_cpu;
    if (USE_CPU) {
      Timer cpu_timer;
      outs_cpu =
        ParallelMap(batch,
                    [](const std::pair<uint64_t, uint32_t> &p) {
                      return NSoks2(p.first, p.second);
                    },
                    6);
      cpu_sec += cpu_timer.Seconds();
      CHECK((int)outs_cpu.size() == GPU_HEIGHT);
    }

    Timer gpu_timer;
    std::vector<std::vector<std::pair<uint64_t, uint64_t>>> outs_gpu =
      ways_gpu.GetWays(batch);
    gpu_sec += gpu_timer.Seconds();

    CHECK((int)outs_gpu.size() == GPU_HEIGHT);

    if (USE_CPU && CHECK_ANSWERS) {
      Timer check_timer;
      for (int row = 0; row < GPU_HEIGHT; row++) {
        auto &out_cpu = outs_cpu[row];
        NormalizeWays(&out_cpu);
        auto &out_gpu = outs_gpu[row];
        NormalizeWays(&out_gpu);

        if (out_gpu != out_cpu) {
          printf(ARED("FAIL") "\n"
                 "Sum: %llu\n"
                 "CPU: %s\n"
                 "GPU: %s\n",
                 batch[row].first,
                 WaysString(out_cpu).c_str(),
                 WaysString(out_gpu).c_str());

          auto out_nsok = NSoks2(batch[row].first);
          NormalizeWays(&out_nsok);
          printf("nsoks: %s\n", WaysString(out_nsok).c_str());
          CHECK(false);
        }
      }
      check_sec += check_timer.Seconds();
    }

    if (bar_per.ShouldRun()) {
      printf(ANSI_PREVLINE ANSI_BEGINNING_OF_LINE ANSI_CLEARLINE
             ANSI_BEGINNING_OF_LINE "%s\n",
             ANSI::ProgressBar(batch_idx,
                               NUM_BATCHES,
                               StringPrintf("benchmark/test. batch size %d",
                                            GPU_HEIGHT).c_str(),
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

  printf("\n"
         ABGCOLOR(16, 220, 16,
                  AFGCOLOR(255, 255, 255, " == TIMING == ")) " \n"
         "Method: " APURPLE("%s") "\n"
         "Sum start: " ABLUE("%s") "\n"
         "Height: " ACYAN("%d") "\n"
         "Time:\n"
         "Batches: %s\n"
         "Check: %s\n"
         "CPU: %s\n"
         "GPU: %s (%s/ea)\n",
         method,
         Util::UnsignedWithCommas(SUM_START).c_str(),
         GPU_HEIGHT,
         ANSI::Time(batch_sec).c_str(),
         ANSI::Time(check_sec).c_str(),
         ANSI::Time(cpu_sec).c_str(),
         ANSI::Time(gpu_sec).c_str(),
         ANSI::Time(gpu_sec / (GPU_HEIGHT * NUM_BATCHES)).c_str());

  ways_gpu.PrintTimers();
}

static void TestTryFilter() {

  TryMe tryme_keep;
  tryme_keep.num = 425*425 + 373*373;
  tryme_keep.squareways = {{425, 373}, {527, 205}, {23, 565}};

  TryMe tryme_remove;
  tryme_remove.num = 10976645;
  tryme_remove.squareways = {{26, 3313}, {118, 3311}, {271, 3302}, {481, 3278}, {574, 3263}, {623, 3254}, {862, 3199}, {1153, 3106}, {1454, 2977}, {1582, 2911}, {1793, 2786}, {1967, 2666}, {2042, 2609}, {2081, 2578}, {2198, 2479}, {2266, 2417}};

  std::vector<TryMe> input = {tryme_keep, tryme_remove};
  TryFilterGPU tryfilter(cl, input.size());

  uint64_t rejected_f = 0;
  std::vector<TryMe> out = tryfilter.FilterWays(input, &rejected_f);
  // printf("rejected_f: %llu (should be about 16 * 15 * 14 * 4 = %d)\n",
  // rejected_f, 16 * 15 * 14 * 4);
  CHECK(rejected_f == 16 * 15 * 14 * 4);
  CHECK(out.size() == 1);
  CHECK(out[0].num == input[0].num);
  CHECK(out[0].squareways == input[0].squareways);

  printf("TryFilter GPU " AGREEN("OK") "\n");
}

static void TestEligibleFilter() {
  static constexpr int HEIGHT = 8192 * 256;
  EligibleFilterGPU filter(cl, HEIGHT);

  static constexpr int START_NUM = 1000000;
  vector<uint8_t> out = filter.Filter(START_NUM);
  ineligible.Reset();
  ParallelComp(
      HEIGHT * 8,
      [&out](int idx) {
        const uint64_t sum = START_NUM + idx;

        uint8_t byte = out[idx / 8];
        uint8_t bit = !!(byte & (1 << (7 - (idx % 8))));
        if (bit) {
          ineligible++;
          // This means it must not be eligible.
          int numways = ChaiWahWu(sum);
          // Currently we check that it cannot be the sum of two
          // squares *at all*, though it would be useful for this
          // filter to exclude ones that are not triples.
          CHECK(numways == 0) << sum << ": " << numways;
        } else {
          // False negatives are allowed.
        }
      },
      8);
  printf("%llu/%llu were ineligible.\n", ineligible.Read(), HEIGHT * 8ULL);

  printf("TestEligibleFilter " AGREEN("OK") "\n");
}

int main(int argc, char **argv) {
  ANSI::Init();
  cl = new CL;

  TestEligibleFilter();
  TestTryFilter();

  TestWays<WaysGPUMerge, TEST_AGAINST_CPU, 16>("merge");
  TestWays<WaysGPU, TEST_AGAINST_CPU, 16>("orig2d");

  // Optimize<WaysGPU>();

  printf("OK\n");
  return 0;
}
