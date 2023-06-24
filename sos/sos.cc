#include <cmath>
#include <memory>
#include <vector>
#include <functional>
#include <string>
#include <bit>
#include <tuple>
#include <atomic>
#include <deque>

#include <windows.h>

#include "clutil.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "arcfour.h"
#include "randutil.h"
#include "threadutil.h"
#include "util.h"
#include "periodically.h"
#include "timer.h"
#include "ansi.h"
#include "autoparallel.h"
#include "factorize.h"

#include "sos-util.h"
#include "sos-gpu.h"

using namespace std;

static CL *cl = nullptr;

using int64 = int64_t;

// Tuned by sos-gpu_test.
static constexpr int GPU_HEIGHT = 49912;
static constexpr uint64_t EPOCH_SIZE = 1'000'000'000; /* ' */
static constexpr int TRY_PARALLELISM = 4;

static std::mutex file_mutex;
static const char *INTERESTING_FILE = "interesting.txt";
static const char *DONE_FILE = "sos-done.txt";

static uint64_t GetDone() {
  MutexLock ml(&file_mutex);
  string s = Util::ReadFile(DONE_FILE);
  if (s.empty()) return 0;
  return stoull(s);
}

static void SetDone(uint64 next) {
  MutexLock ml(&file_mutex);
  Util::WriteFile(DONE_FILE, StringPrintf("%llu\n", next));
}

static void Interesting(const std::string &s) {
  MutexLock ml(&file_mutex);
  printf("%s\n", s.c_str());
  string raw = AnsiStripCodes(s);
  FILE *f = fopen(INTERESTING_FILE, "ab");
  CHECK(f != nullptr);
  fprintf(f, "%s\n", raw.c_str());
  fclose(f);
}

// These are not guaranteed to be accurate!
static std::atomic<int64_t> rejected_f{0ULL};
static std::atomic<int64_t> rejected_h{0ULL};
static std::atomic<int64_t> rejected_ff{0ULL};
static std::atomic<int64_t> rejected_hh{0ULL};
static std::atomic<int64_t> rejected_aa{0ULL};
#define INCREMENT(rej) rej.fetch_add(1, std::memory_order_relaxed)
static void ResetCounters() {
  rejected_f.store(0ULL);
  rejected_h.store(0ULL);
  rejected_ff.store(0ULL);
  rejected_hh.store(0ULL);
  rejected_aa.store(0ULL);
}

// So now take numbers that can be written as sums of squares
// three ways: Z = B^2 + C^2 = D^2 + G^2 = E^2 + I^2
//
//  [a]  B   C
//
//   D   E  [f]
//
//   G  [h]  I
//
// This gives us the SUM = G + E + C, which then uniquely
// determines a, f, h (if they exist). Since the starting
// values were distinct, these residues are also distinct.
//
// The order of (B, C), (D, G), (E, I) matters, although there
// are some symmetries. We can req
template<class F>
inline static void AllWays(
    const std::vector<std::pair<uint64_t, uint64_t>> &ways,
    const F &fn) {
  for (int p = 0; p < ways.size(); p++) {
    const auto &[b, c] = ways[p];
    for (int q = 0; q < ways.size(); q++) {
      if (p != q) {
        const auto &[d, g] = ways[q];
        for (int r = 0; r < ways.size(); r++) {
          if (p != r && q != r) {
            const auto &[e, i] = ways[r];

            // Now eight ways of ordering the pairs.
            fn(/**/  b,    c,
               d,    e,  /**/
               g,  /**/    i);
            fn(/**/  c,    b,
               d,    e,  /**/
               g,  /**/    i);
            fn(/**/  b,    c,
               g,    e,  /**/
               d,  /**/    i);
            fn(/**/  c,    b,
               g,    e,  /**/
               d,  /**/    i);

            fn(/**/  b,    c,
               d,    i,  /**/
               g,  /**/    e);
            fn(/**/  c,    b,
               d,    i,  /**/
               g,  /**/    e);
            fn(/**/  b,    c,
               g,    i,  /**/
               d,  /**/    e);
            fn(/**/  c,    b,
               g,    i,  /**/
               d,  /**/    e);
          }
        }
      }
    }
  }
}

static void Try(int z,
                const std::vector<std::pair<uint64_t, uint64_t>> &ways) {

  AllWays(ways,
          [z](/*     a */ uint64_t b, uint64_t c,
              uint64_t d, uint64_t e, /*     f */
              uint64_t g, /*     h */ uint64_t i) {

            // We could factor these multiplications out, but since
            // AllWays inlines, the compiler can probably do it.
            const uint64_t bb = b * b;
            const uint64_t cc = c * c;
            const uint64_t dd = d * d;
            const uint64_t ee = e * e;
            const uint64_t gg = g * g;
            const uint64_t ii = i * i;

            // f is specified two ways; they must have the
            // same sum then. This is by far the most common
            // rejection reason.
            if (cc + ii != dd + ee) {
              INCREMENT(rejected_f);
              return;
            }
            // Same for h.
            if (gg + ii != bb + ee) {
              // XXX This never fails? Is it implied by the above?
              // Prove it?
              INCREMENT(rejected_h);
              return;
            }

            // Finally, check that a, f, h are integral.
            const uint64_t sum = cc + ee + gg;

            const uint64_t ff = sum - (dd + ee);
            const uint64_t f = Sqrt64(ff);
            if (f * f != ff) {
              INCREMENT(rejected_ff);
              return;
            }

            const uint64_t hh = sum - (bb + ee);
            const uint64_t h = Sqrt64(hh);
            if (h * h != hh) {
              INCREMENT(rejected_hh);
              const uint64_t aa = sum - (bb + cc);
              Interesting(
                  StringPrintf(
                      // For easy parsing. Everything is its squared version.
                      "(!) %llu %llu %llu %llu %llu %llu %llu %llu %llu\n"
                      ARED("sqrt(%llu)^2") " %llu^2 %llu^2\n"
                      "%llu^2 %llu^2 %llu^2\n"
                      "%llu^2 " ARED("sqrt(%llu)^2") " %llu^2\n"
                      ARED("but %llu * %llu != %llu") "\n"
                      "Sum: %llu\n",
                      aa, bb, cc, dd, ee, ff, gg, hh, ii,
                      aa, b, c,
                      d, e, f,
                      g, hh, i,
                      // error
                      h, h, hh,
                      sum));
              return;
            }

            const uint64_t aa = sum - (bb + cc);
            const uint64_t a = Sqrt64(aa);
            if (a * a != aa) {
              INCREMENT(rejected_aa);
              Interesting(
                  StringPrintf(
                      // For easy parsing. Everything is its squared version.
                      "(!!!) %llu %llu %llu %llu %llu %llu %llu %llu %llu\n"
                      ARED("sqrt(%llu)^2") " %llu^2 %llu^2\n"
                      "%llu^2 %llu^2 %llu^2\n"
                      "%llu^2 %llu^2 %llu^2\n"
                      ARED("but %llu * %llu != %llu") "\n"
                      "Sum: %llu\n",
                      aa, bb, cc, dd, ee, ff, gg, hh, ii,
                      aa, b, c,
                      d, e, f,
                      g, h, i,
                      // error
                      a, a, aa,
                      sum));
              return;
            }

            string success =
              StringPrintf("(!!!!!)"
                           "%llu^2 %llu^2 %llu^2\n"
                           "%llu^2 %llu^2 %llu^2\n"
                           "%llu^2 %llu^2 %llu^2\n"
                           "Sum: %llu\n"
                           AGREEN("It works?!") "\n",
                           a, b, c,
                           d, e, f,
                           g, h, i,
                           sum);

            Interesting(success);

            printf("Success!!\n");

            printf("Note: didn't completely check for uniqueness "
                   "or overflow!\n");
            CHECK(false) << "winner";
          });

}

// TODO: To threadutil?
template<class Item>
struct BatchedWorkQueue {
  const int batch_size = 0;
  BatchedWorkQueue(int batch_size) : batch_size(batch_size) {
    CHECK(batch_size > 0);
    // Set up invariant.
    queue.push_back(std::vector<Item>{});
  }

  std::mutex mutex;
  std::condition_variable cond;
  // Add at the end. This always consists of a series (maybe zero)
  // of full vectors and an incomplete vector (maybe empty) at the
  // end. (Unless "done", in which case it can be empty.)
  std::deque<std::vector<Item>> queue;
  bool done = false;

  // Consumers of the work queue call this in a loop. If the result
  // is nullopt, then the queue is done. The final work item can be
  // smaller than the batch size, but not empty.
  std::optional<std::vector<Item>> WaitGet() {
    std::vector<Item> batch;
    {
      std::unique_lock ml(mutex);
      cond.wait(ml, [this] {
          // Either the queue is empty (and we're totally done)
          // or there's something in the queue.
          if (done) return true;
          CHECK(!queue.empty());
          return queue.front().size() == batch_size;
        });
      if (done && queue.empty()) return nullopt;
      // Now holding lock with a full batch (or the last batch).
      // Take ownership.
      batch = std::move(queue.front());
      // It's the responsibility of those that insert into the
      // queue to maintain the presence of an incomplete vector.
      // So we can just remove the full one.
      queue.pop_front();
    }
    cond.notify_all();

    return {batch};
  }

  void WaitAdd(const Item &item) {
    {
      std::unique_lock ml(mutex);
      CHECK(!done);
      CHECK(!queue.empty() && queue.back().size() < batch_size);
      queue.back().push_back(item);
      if (queue.back().size() == batch_size) {
        // Finished batch, so add new empty batch.
        queue.push_back(std::vector<Item>());
      }
    }
    cond.notify_all();
  }

  void MarkDone() {
    {
      std::unique_lock ml(mutex);
      CHECK(!done);
      done = true;
    }
    cond.notify_all();
  }

  // Might be useful to be able to add in batch.
};

// TODO: To threadutil?
// Here, a serial queue. This is intended for larger items (perhaps
// pre-batched work).
template<class Item>
struct WorkQueue {
  // TODO: Could support max queue size pretty easily.
  WorkQueue() {
  }

  std::mutex mutex;
  std::condition_variable cond;
  // The items. Can be empty.
  std::deque<Item> queue;
  bool done = false;

  // Consumers of the work queue call this in a loop. If nullopt,
  // then the queue is done.
  std::optional<Item> WaitGet() {
    Item item;
    {
      std::unique_lock ml(mutex);
      cond.wait(ml, [this] {
          if (done) return true;
          return !queue.empty();
        });

      if (done && queue.empty()) return nullopt;

      item = std::move(queue.front());
      queue.pop_front();
    }
    cond.notify_all();

    return {item};
  }

  void WaitAdd(const Item &item) {
    {
      std::unique_lock ml(mutex);
      CHECK(!done);
      queue.push_back(item);
    }
    cond.notify_all();
  }

  void WaitAdd(Item &&item) {
    {
      std::unique_lock ml(mutex);
      CHECK(!done);
      queue.emplace_back(item);
    }
    cond.notify_all();
  }

  void MarkDone() {
    {
      std::unique_lock ml(mutex);
      CHECK(!done);
      done = true;
    }
    cond.notify_all();
  }

};

struct TryMe {
  uint64_t num;
  std::vector<std::pair<uint64_t, uint64_t>> squareways;
};


struct SOS {
  std::unique_ptr<AutoParallelComp> comp;
  std::unique_ptr<NWaysGPU> nways_gpu;

  // An element is a number and its expected number of ways.
  std::unique_ptr<
    BatchedWorkQueue<std::pair<uint64_t, uint32_t>>
    > nways_queue;
  std::unique_ptr<
    WorkQueue<std::vector<TryMe>>
    > try_queue;

  SOS() : status_per(10.0) {
    // Cache is pretty workload-dependent, so just tune in-process.
    comp.reset(new AutoParallelComp(16, 1000, false
                                    /* , "cww.autoparallel" */));
    nways_gpu.reset(new NWaysGPU(cl, GPU_HEIGHT));

    nways_queue.reset(
        new BatchedWorkQueue<std::pair<uint64_t, uint32_t>>(GPU_HEIGHT));
    try_queue.reset(new WorkQueue<std::vector<TryMe>>());
  }

  void GPUThread() {
    for (;;) {
      std::optional<std::vector<std::pair<uint64_t, uint32_t>>> batchopt =
        nways_queue->WaitGet();

      if (!batchopt.has_value()) {
        // Done!
        printf("GPU thread done!\n");
        fflush(stdout);

        try_queue->MarkDone();
        return;
      }

      auto batch = std::move(batchopt.value());
      const int real_batch_size = batch.size();

      // Last batch can be incomplete.
      CHECK(batch.size() <= GPU_HEIGHT);
      while (batch.size() < GPU_HEIGHT) {
        // Fill with dummy values.
        batch.push_back(NWaysGPU::dummy);
      }
      std::vector<std::vector<std::pair<uint64_t, uint64_t>>> res =
        nways_gpu->GetNWays(batch);

      // Rejoin with the number. PERF: We could avoid some copying here
      // if it's a bottleneck.
      std::vector<TryMe> trybatch;
      trybatch.reserve(GPU_HEIGHT);
      // But only populate the real batch size, in case this is the
      // last batch and short.
      for (int i = 0; i < real_batch_size; i++)
        trybatch.emplace_back(TryMe{.num = batch[i].first,
                                    .squareways = std::move(res[i])});
      res.clear();

      // Add to CPU-side Try queue.
      try_queue->WaitAdd(std::move(trybatch));
      trybatch.clear();
      {
        MutexLock ml(&m);
        done_nways += real_batch_size;
      }
    }
  }

  std::mutex m;
  Timer timer;
  // These were too big to process on the GPU.
  uint64_t too_big = 0;
  // Many are rejected because they can't be written as the sum of
  // squares enough ways.
  uint64_t ineligible = 0;
  // These two should eventually reach the same value.
  uint64_t triples = 0;
  uint64_t done_nways = 0;
  // Including ineligible. This eventually reaches EPOCH_SIZE.
  uint64_t done = 0;
  Periodically status_per;

  // Must hold lock m.
  void PrintStats() {
    double pct = (triples * 100.0)/(double)done;
    double sec = timer.Seconds();
    double nps = done / sec;
    printf("%llu/%llu (%.5f%%) are triples (%s) %.1f/sec\n",
           triples, done, pct, AnsiTime(sec).c_str(), nps);
    const int64_t rf = rejected_f.load();
    const int64_t rh = rejected_h.load();
    const int64_t rff = rejected_ff.load();
    const int64_t rhh = rejected_hh.load();
    const int64_t raa = rejected_aa.load();
    printf("%lld " AGREY("rf") " %lld " AGREY("rh")
           " %lld " AGREY("rff") " %lld " AGREY("rhh")
           " %lld " AGREY("raa") "\n",
           rf, rh, rff, rhh, raa);
    printf(ARED("%llu") " too big\n", too_big);
    string info = StringPrintf("%llu inel  %llu nw",
                               ineligible, done_nways);
    string bar = AnsiProgressBar(done, EPOCH_SIZE, info, sec);
    // XXX put in stable spot
    printf("%s\n", bar.c_str());
  }

  void TryThread() {
    for (;;) {
      std::optional<std::vector<TryMe>> batchopt = try_queue->WaitGet();
      if (!batchopt.has_value()) {
        // Totally done!
        printf("Try thread done!\n");
        fflush(stdout);
        return;
      }

      const auto &batch = batchopt.value();
      printf("Try batch of size %d\n", (int)batch.size());

      ParallelApp(
          batch,
          [](const TryMe &tryme) {
            Try(tryme.num, tryme.squareways);
          },
          TRY_PARALLELISM);

      {
        MutexLock ml(&m);
        done += batch.size();
        if (status_per.ShouldRun()) {
          PrintStats();
        }
      }
    }
  }

  void RunEpoch(uint64_t start) {
    CHECK(cl != nullptr);

    printf(AWHITE("==") " Start epoch " APURPLE("%llu") "+ " AWHITE("==") "\n",
           start);

    std::thread gpu_thread(&GPUThread, this);
    std::thread try_thread(&TryThread, this);

    ResetCounters();
    comp->
      ParallelComp(
        EPOCH_SIZE,
        [this, start](uint64_t idx) {
          const uint64_t num = start + idx;

          const int nways = ChaiWahWu(num);

          if (nways >= 3) {
            if (nways > NWaysGPU::MAX_WAYS) {
              {
                MutexLock ml(&m);
                too_big++;
              }
              // Do on CPU.
              std::vector<std::pair<uint64_t, uint64_t>> ways =
                BruteGetNWays(num, nways);
              TryMe tryme;
              tryme.num = num;
              tryme.squareways = std::move(ways);
              try_queue->WaitAdd({std::move(tryme)});

            } else {
              nways_queue->WaitAdd(make_pair(num, nways));
            }
          } else {
            MutexLock ml(&m);
            ineligible++;
            done++;
          }
        });

    nways_queue->MarkDone();

    printf("Waiting for GPU thread.\n");
    gpu_thread.join();
    printf("Waiting for Try thread.\n");
    try_thread.join();

    printf(AGREEN("Done with epoch!") "\n");

    printf(AWHITE("Autoparallel histo") ":\n");
    comp->PrintHisto();

    double sec = timer.Seconds();
    printf("Total triples: %llu/%llu\n", triples, EPOCH_SIZE);
    printf(AGREEN ("Done") " in %s. (%s/ea.)\n",
           AnsiTime(sec).c_str(), AnsiTime(sec / EPOCH_SIZE).c_str());
    printf("Did %llu-%llu\n", start, start + EPOCH_SIZE - 1);
    {
      const int64_t rf = rejected_f.load();
      const int64_t rh = rejected_h.load();
      const int64_t rff = rejected_ff.load();
      const int64_t rhh = rejected_hh.load();
      const int64_t raa = rejected_aa.load();

      Interesting(
          StringPrintf("EPOCH %llu %llu %llu %lld %lld %lld %lld %lld\n",
                       start, EPOCH_SIZE, triples,
                       rf, rh, rff, rh, raa));

      FILE *f = fopen("sos.txt", "ab");
      CHECK(f != nullptr);
      fprintf(f, "Done in %s (%s/ea.)\n",
              AnsiStripCodes(AnsiTime(sec)).c_str(),
              AnsiStripCodes(AnsiTime(sec / EPOCH_SIZE)).c_str());
      fprintf(f,
              "%lld rf %lld rh"
              " %lld rff %lld rhh"
              " %lld raa\n",
              rf, rh, rff, rhh, raa);
      fprintf(f, "Did %llu-%llu\n", start, start + EPOCH_SIZE - 1);
      fclose(f);
    }
  }

};

static void Run() {
  uint64_t start = GetDone();
  for (;;) {
    SOS sos;
    sos.RunEpoch(start);
    start += EPOCH_SIZE;
    SetDone(start);
  }
}


int main(int argc, char **argv) {
  AnsiInit();
  cl = new CL;

  if (!SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS)) {
    LOG(FATAL) << "Unable to go to BELOW_NORMAL priority.\n";
  }

  Run();

  return 0;
}

