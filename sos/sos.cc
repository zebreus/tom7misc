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
#include "base/port.h"
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

#define AORANGE(s) ANSI_FG(247, 155, 57) s ANSI_RESET

// Everything takes a complete line.
// Thread safe.
struct StatusBar {
  // Give the number of lines that the status bar uses.
  explicit StatusBar(int num_lines) : num_lines(num_lines) {
    CHECK(num_lines > 0);
  }

  // Print to the screen. Adds trailing newline if not present.
  void Printf(const char* format, ...) PRINTF_ATTRIBUTE(1, 2) {
    va_list ap;
    va_start(ap, format);
    string result;
    StringAppendV(&result, format, ap);
    va_end(ap);
    Emit(result);
  }

  // Prints to the screen. Adds trailing newline if not present.
  void Emit(const std::string &s) {
    std::vector<std::string> lines = Util::SplitToLines(s);
    MutexLock ml(&m);
    MoveUp();
    for (const string &line : lines) {
      printf("%s\n", line.c_str());
    }
    // Maintain space for status.
    for (int i = 0; i < num_lines; i++) {
      printf("\n");
    }
  }

  // Update the status bar. This should be done in one call that
  // contains num_lines lines. Trailing newline not necessary.
  void Statusf(const char* format, ...) PRINTF_ATTRIBUTE(1, 2) {
    va_list ap;
    va_start(ap, format);
    string result;
    StringAppendV(&result, format, ap);
    va_end(ap);
    EmitStatus(result);
  }

  // Prints the status to the screen.
  void EmitStatus(const std::string &s) {
    std::vector<std::string> lines = Util::SplitToLines(s);
    MutexLock ml(&m);
    MoveUp();
    if (lines.size() != num_lines) {
      printf(ARED("...wrong number of lines...") "\n");
    }
    for (const string &line : lines) {
      printf("%s\n", line.c_str());
    }
  }

private:
  // The idea is that we keep the screen in a state where there are
  // num_lines of status at the bottom, right before the cursor. This
  // is always throw-away space. When we print something other than
  // status, we just pad with the number of blank lines so that the
  // next call will not overwrite what we wrote.
  void MoveUp() {
    if (!first) {
      for (int i = 0; i < num_lines; i++) {
        printf(
            // Cursor to beginning of previous line
            "\x1B[F"
            // Clear line
            "\x1B[2K"
               );
      }
    }
    first = false;
  }

  std::mutex m;
  int num_lines = 0;
  bool first = true;
};

// Tuned by sos-gpu_test.
// static constexpr int GPU_HEIGHT = 49912;
// static constexpr int GPU_HEIGHT = 51504;
static constexpr int GPU_HEIGHT = 131072;

static constexpr uint64_t EPOCH_SIZE = 2'000'000'000; /* ' */
// PERF: Tune it
static constexpr int STEADY_WORK_STEALING_THREADS = 1;
static constexpr int ENDGAME_WORK_STEALING_THREADS = 8;

static std::mutex file_mutex;
static const char *INTERESTING_FILE = "interesting.txt";
static const char *DONE_FILE = "sos-done.txt";

static StatusBar status(5);

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
  status.Printf("%s\n", s.c_str());
  string raw = ANSI::StripCodes(s);
  FILE *f = fopen(INTERESTING_FILE, "ab");
  CHECK(f != nullptr);
  fprintf(f, "%s\n", raw.c_str());
  fclose(f);
}

// These are not guaranteed to be accurate!
static std::atomic<int64_t> rejected_f{0ULL};
static std::atomic<int64_t> rejected_ff{0ULL};
static std::atomic<int64_t> rejected_hh{0ULL};
static std::atomic<int64_t> rejected_aa{0ULL};
#define INCREMENT(rej) rej.fetch_add(1, std::memory_order_relaxed)
static void ResetCounters() {
  rejected_f.store(0ULL);
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
// are some symmetries. A reflection like
//
//   I  [h]  G
//
//  [f]  E   D
//
//   C   B  [a]
//
// will just be found during some other call to Try (with
// a different z) but flipping along the c-e-g diagonal
// does give us a redundant form:
//
//  [a]  D   G
//
//   B   E  [h]
//
//   C  [f]  I
//
// which we do not need to search. To avoid computing each
// square twice in here, we require that the smallest of B, C,
// D, G appears on the top.
template<class F>
inline static void AllWays(
    const std::vector<std::pair<uint64_t, uint64_t>> &ways,
    const F &fn) {
  for (int p = 0; p < ways.size(); p++) {
    const auto &[b, c] = ways[p];
    for (int q = 0; q < ways.size(); q++) {
      if (p != q) {
        const auto &[d, g] = ways[q];
        // require that the smallest of b,c,d,g appears on the
        // top, to reduce symmetries.
        if (std::min(b, c) > std::min(d, g))
          continue;

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

            // Now we also know gg + ii == bb + ee. See sos.z3.

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
                      AORANGE("sqrt(%llu)^2?") " %llu^2 %llu^2\n"
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
              StringPrintf("(!!!!!) "
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

            CHECK(gg + ii == bb + ee) << "Supposedly this is always "
              "the case (see sos.z3).";

            printf("\n\n\n\n" AGREEN("Success!!") "\n");

            printf("%s\n", success.c_str());

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
      size -= batch.size();
      // It's the responsibility of those that insert into the
      // queue to maintain the presence of an incomplete vector.
      // So we can just remove the full one.
      queue.pop_front();
    }
    cond.notify_all();

    return {batch};
  }

  int64_t Size() {
    std::unique_lock ml(mutex);
    return size;
  }

  void WaitAdd(const Item &item) {
    {
      std::unique_lock ml(mutex);
      CHECK(!done);
      CHECK(!queue.empty() && queue.back().size() < batch_size);
      queue.back().push_back(item);
      size++;
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
 private:
  std::mutex mutex;
  std::condition_variable cond;
  // Add at the end. This always consists of a series (maybe zero)
  // of full vectors and an incomplete vector (maybe empty) at the
  // end. (Unless "done", in which case it can be empty.)
  std::deque<std::vector<Item>> queue;
  int64_t size = 0;
  bool done = false;
};

// TODO: To threadutil?
// Here, a serial queue. This is intended for larger items (perhaps
// pre-batched work).
template<class Item>
struct WorkQueue {
  // TODO: Could support max queue size pretty easily.
  WorkQueue() {
  }

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

  int64_t Size() {
    std::unique_lock ml(mutex);
    return queue.size();
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

private:
  std::mutex mutex;
  std::condition_variable cond;
  // The items. Can be empty.
  std::deque<Item> queue;
  bool done = false;
};

struct TryMe {
  uint64_t num;
  std::vector<std::pair<uint64_t, uint64_t>> squareways;
};


struct SOS {
  std::unique_ptr<AutoParallelComp> factor_comp;
  std::unique_ptr<AutoParallelComp> try_comp;
  std::unique_ptr<NWaysGPU> nways_gpu;

  // An element is a number and its expected number of ways.
  std::unique_ptr<
    BatchedWorkQueue<std::pair<uint64_t, uint32_t>>
    > nways_queue;
  std::unique_ptr<
    WorkQueue<std::vector<TryMe>>
    > try_queue;

  SOS() : status_per(10.0) {
    // Performance is pretty workload-dependent, so just tune in-process
    // rather than saving to disk.
    factor_comp.reset(new AutoParallelComp(20, 1000, false));
    try_comp.reset(new AutoParallelComp(8, 1000, false));

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
        status.Printf("GPU thread done!\n");
        fflush(stdout);
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
  // Should eventually reach the value of triples.
  uint64_t triples_done = 0;
  // Including ineligible. This eventually reaches EPOCH_SIZE.
  uint64_t done = 0;
  // Work stolen by CPU in endgame.
  uint64_t stolen = 0;

  int work_stealing_threads = 2;

  Periodically status_per;

  // Must hold lock m.
  void PrintStats() {
    double pct = (triples * 100.0)/(double)done;
    double sec = timer.Seconds();
    double nps = done / sec;
    string line1 =
      StringPrintf("%llu/%llu (%.5f%%) are triples (%s) %.1f/sec\n",
                   triples, done, pct, ANSI::Time(sec).c_str(), nps);

    const int64_t rf = rejected_f.load();
    const int64_t rff = rejected_ff.load();
    const int64_t rhh = rejected_hh.load();
    const int64_t raa = rejected_aa.load();

    const int64_t gpu_size = nways_queue->Size();
    const int64_t try_size = try_queue->Size();

    string line2 =
      StringPrintf("%lld " AGREY("rf")
                   " %lld " AGREY("rff") " %lld " AGREY("rhh")
                   " %lld " AGREY("raa")
                   "\n",
                   rf, rff, rhh, raa);
    string line3 = StringPrintf(ARED("%llu") " too big  "
                                APURPLE("%s") " gpu queue  "
                                ACYAN("%lld") " try queue  ("
                                AGREEN("%s") " stolen)\n",
                                too_big,
                                Util::UnsignedWithCommas(gpu_size).c_str(),
                                try_size,
                                Util::UnsignedWithCommas(stolen).c_str());
    string info = StringPrintf("%s inel  %s nw",
                               Util::UnsignedWithCommas(ineligible).c_str(),
                               Util::UnsignedWithCommas(done_nways).c_str());
    string line4 = ANSI::ProgressBar(done, EPOCH_SIZE, info, sec) + "\n";
    string line5 = ANSI::ProgressBar(triples_done, triples, "", sec,
                                     ANSI::ProgressBarOptions{
                                       .bar_filled = 0x046204,
                                       .bar_empty = 0x012701,
                                     });
    status.EmitStatus(line1 + line2 + line3 + line4 + line5);
  }

  void TryThread() {
    for (;;) {
      std::optional<std::vector<TryMe>> batchopt = try_queue->WaitGet();
      if (!batchopt.has_value()) {
        // Totally done!
        status.Printf("Try thread done!\n");
        fflush(stdout);
        return;
      }

      const auto &batch = batchopt.value();

      try_comp->
      ParallelApp(
          batch,
          [](const TryMe &tryme) {
            Try(tryme.num, tryme.squareways);
          });

      {
        MutexLock ml(&m);
        done += batch.size();
        triples_done += batch.size();
        if (status_per.ShouldRun()) {
          PrintStats();
        }
      }
    }
  }

  void StealThread() {
    // GPU part is the bottleneck, so also run some of these on the
    // CPU. When we reach the endgame, we'll increase the number of threads.
    int num_threads = work_stealing_threads;
    for (;;) {
      std::optional<std::vector<std::pair<uint64_t, uint32_t>>> batchopt =
        nways_queue->WaitGet();

      if (!batchopt.has_value()) {
        // Done -- but let the GPU mark the queue as done.
        status.Printf("No more work to steal!\n");
        return;
      }

      std::vector<TryMe> output =
        ParallelMap(
            batchopt.value(),
            [](const std::pair<uint64_t, uint32_t> &input) {
              TryMe tryme;
              tryme.num = input.first;
              tryme.squareways = NSoks2(input.first, input.second);
              return tryme;
            },
            num_threads);

      try_queue->WaitAdd(std::move(output));
      output.clear();
      {
        MutexLock ml(&m);
        stolen += batchopt.value().size();
        num_threads = work_stealing_threads;
      }
    }
  }

  void RunEpoch(uint64_t start) {
    CHECK(cl != nullptr);

    status.Printf(
        AWHITE("==") " Start epoch " APURPLE("%s") "+ " AWHITE("==") "\n",
        Util::UnsignedWithCommas(start).c_str());

    work_stealing_threads = STEADY_WORK_STEALING_THREADS;

    std::thread gpu_thread(&GPUThread, this);
    std::thread try_thread(&TryThread, this);
    std::thread steal_thread(&StealThread, this);

    ResetCounters();
    factor_comp->
      ParallelComp(
        EPOCH_SIZE,
        [this, start](uint64_t idx) {
          const uint64_t num = start + idx;

          const int nways = ChaiWahWu(num);

          if (nways >= 3) {

            if (nways > NWaysGPU::MAX_WAYS) {
              // Do on CPU.
              std::vector<std::pair<uint64_t, uint64_t>> ways =
                NSoks2(num, nways);
              TryMe tryme;
              tryme.num = num;
              tryme.squareways = std::move(ways);
              try_queue->WaitAdd({std::move(tryme)});

              {
                MutexLock ml(&m);
                too_big++;
                triples++;
                done_nways++;
              }
            } else {
              {
                MutexLock ml(&m);
                triples++;
              }
              nways_queue->WaitAdd(make_pair(num, nways));
            }
          } else {
            MutexLock ml(&m);
            ineligible++;
            done++;
          }
        });

    nways_queue->MarkDone();

    {
      MutexLock ml(&m);
      work_stealing_threads = ENDGAME_WORK_STEALING_THREADS;
    }

    status.Printf("Waiting for Steal/GPU threads.\n");

    steal_thread.join();

    // Now actually wait for gpu thread, which should be done very shortly
    // since there's no more work.
    gpu_thread.join();

    try_queue->MarkDone();

    status.Printf("Waiting for Try thread.\n");
    try_thread.join();

    status.Printf(AGREEN("Done with epoch!") "\n");

    status.Printf(AWHITE("Factor autoparallel histo") ":\n");
    status.Emit(factor_comp->HistoString());
    status.Printf(AWHITE("Try autoparallel histo") ":\n");
    status.Emit(try_comp->HistoString());

    double sec = timer.Seconds();
    status.Printf("CPU stole " AGREEN("%lld")
                  " work units from GPU (" ACYAN("%.2f") "%%)\n",
                  stolen, (100.0 * stolen) / triples);
    status.Printf("Total triples: %llu/%llu\n", triples, EPOCH_SIZE);
    status.Printf(AGREEN ("Done") " in %s. (%s/ea.)\n",
                  ANSI::Time(sec).c_str(),
                  ANSI::Time(sec / EPOCH_SIZE).c_str());
    status.Printf("Did %s-%s\n",
                  Util::UnsignedWithCommas(start).c_str(),
                  Util::UnsignedWithCommas(start + EPOCH_SIZE - 1).c_str());
    {
      const int64_t rf = rejected_f.load();
      const int64_t rff = rejected_ff.load();
      const int64_t rhh = rejected_hh.load();
      const int64_t raa = rejected_aa.load();

      Interesting(
          StringPrintf("EPOCH %llu %llu %llu "
                       "%lld %lld %lld %lld\n",
                       start, EPOCH_SIZE, triples,
                       rf, rff, rhh, raa));

      FILE *f = fopen("sos.txt", "ab");
      CHECK(f != nullptr);
      fprintf(f, "Done in %s (%s/ea.)\n",
              ANSI::StripCodes(ANSI::Time(sec)).c_str(),
              ANSI::StripCodes(ANSI::Time(sec / EPOCH_SIZE)).c_str());
      fprintf(f,
              "%lld rf "
              " %lld rff %lld rhh"
              " %lld raa\n",
              rf, rff, rhh, raa);
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

