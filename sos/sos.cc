#include <cmath>
#include <chrono>
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
#include "atomic-util.h"
#include "image.h"

#include "database.h"
#include "sos-util.h"
#include "sos-gpu.h"

using namespace std;

static CL *cl = nullptr;

using int64 = int64_t;
using Square = Database::Square;

// 2x as fast!
// Using this for epoch 2,264,000,000,000+
// (Still need to prove correctness, but it passes lots of
// tests)
using GPUMethod = WaysGPUMerge;

static constexpr bool WRITE_IMAGE = false;

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

static constexpr int NUM_GPU_THREADS = 2;
static constexpr int TRY_BATCH_SIZE = 256;
static constexpr int TRY_ROLL_SIZE = 32;
static_assert(TRY_BATCH_SIZE % TRY_ROLL_SIZE == 0,
              "this is not strictly required, but would hurt "
              "performance!");

static constexpr uint64_t MAX_EPOCH_SIZE = 2'000'000'000; /* ' */
// Each of these yields 8 sums. Should divide EPOCH_SIZE/8.
static constexpr size_t EPOCH_GPU_CHUNK = 1000000;

// PERF: Tune it
static constexpr int STEADY_WORK_STEALING_THREADS = 0;
// Be careful not to set this too low, or everything will stall
// on a stolen batch (size GPU_HEIGHT) that is processed by a
// small number of threads.
static constexpr int ENDGAME_WORK_STEALING_THREADS = 8;

static std::mutex file_mutex;
static const char *INTERESTING_FILE = "interesting.txt";

static StatusBar status(6);

static std::pair<uint64_t, uint64_t> GetNext(uint64_t max_size) {
  MutexLock ml(&file_mutex);
  // PERF don't keep loading this
  Database db = Database::FromInterestingFile(INTERESTING_FILE);
  return db.NextToDo(max_size);
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

// Fast counters to avoid lock contention in tight loops.
DECLARE_COUNTERS(rejected_f, rejected_ff, rejected_hh, rejected_aa,
                 unused1_, unused2_, unused3_, unused4_);

#define INCREMENT(rej) (rej)++
#define INCREMENT_BY(rej, by) (rej) += (by)
#define READ(rej) rej.Read()
#define RESET(rej) rej.Reset()

static void ResetCounters() {
  RESET(rejected_f);
  RESET(rejected_ff);
  RESET(rejected_hh);
  RESET(rejected_aa);
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

  // Just compute the squares once.
  std::vector<std::pair<uint64_t, uint64_t>> ways_squared;
  ways_squared.resize(ways.size());
  for (int i = 0; i < ways.size(); i++) {
    const auto &[a, b] = ways[i];
    ways_squared[i] = make_pair(a * a, b * b);
  }

  for (int p = 0; p < ways.size(); p++) {
    const auto &[b, c] = ways[p];
    const auto &[bb, cc] = ways_squared[p];
    for (int q = 0; q < ways.size(); q++) {
      if (p != q) {
        const auto &[d, g] = ways[q];
        const auto &[dd, gg] = ways_squared[q];

        // require that the smallest of b,c,d,g appears on the
        // top, to reduce symmetries.
        if (std::min(b, c) > std::min(d, g))
          continue;

        for (int r = 0; r < ways.size(); r++) {
          if (p != r && q != r) {
            const auto &[e, i] = ways[r];
            const auto &[ee, ii] = ways_squared[r];

            // Now eight ways of ordering the pairs.
            fn(/**/   b,bb,  c,cc,
               d,dd,  e,ee,  /**/
               g,gg,  /**/   i,ii);
            fn(/**/   c,cc,  b,bb,
               d,dd,  e,ee,  /**/
               g,gg,  /**/   i,ii);
            fn(/**/   b,bb,  c,cc,
               g,gg,  e,ee,  /**/
               d,dd,  /**/   i,ii);
            fn(/**/   c,cc,  b,bb,
               g,gg,  e,ee,  /**/
               d,dd,  /**/   i,ii);

            fn(/**/   b,bb,  c,cc,
               d,dd,  i,ii,  /**/
               g,gg,  /**/   e,ee);
            fn(/**/   c,cc,  b,bb,
               d,dd,  i,ii,  /**/
               g,gg,  /**/   e,ee);
            fn(/**/   b,bb,  c,cc,
               g,gg,  i,ii,  /**/
               d,dd,  /**/   e,ee);
            fn(/**/   c,cc,  b,bb,
               g,gg,  i,ii,  /**/
               d,dd,  /**/   e,ee);
          }
        }
      }
    }
  }
}

static void Try(int z,
                const std::vector<std::pair<uint64_t, uint64_t>> &ways) {

  // PERF we don't actually need the roots until we print it out,
  // so we could just pass the squares and compute sqrts in the rare
  // case that we get through filters.
  AllWays(
      ways,
      [z](/*     a */ uint64_t b,  uint64_t bb, uint64_t c,  uint64_t cc,
          uint64_t d, uint64_t dd, uint64_t e,  uint64_t ee, /*     f */
          uint64_t g, uint64_t gg, /*     h */  uint64_t i,  uint64_t ii) {

            // f is specified two ways; they must have the
            // same sum then. This is by far the most common
            // rejection reason.
            if (cc + ii != dd + ee) [[likely]] {
              INCREMENT(rejected_f);
              return;
            }

            // Now we also know gg + ii == bb + ee. See sos.z3.

            // Finally, check that a, f, h are integral.
            const uint64_t sum = cc + ee + gg;

            const uint64_t ff = sum - (dd + ee);
            auto fo = Sqrt64Opt(ff);
            if (!fo.has_value()) {
              INCREMENT(rejected_ff);
              return;
            }
            const uint64_t f = fo.value();

            const uint64_t hh = sum - (bb + ee);
            const auto ho = Sqrt64Opt(hh);
            if (!ho.has_value()) {
              uint64_t h = Sqrt64(hh);
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
            const uint64_t h = ho.value();

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

  // PERF: Versions that take rvalue reference.

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

  void WaitAddVec(const std::vector<Item> &items) {
    {
      std::unique_lock ml(mutex);
      CHECK(!done);
      // PERF: This can be tighter, but the main thing is to avoid
      // repeatedly taking the lock.
      size += items.size();
      for (const Item &item : items) {
        CHECK(!queue.empty() && queue.back().size() < batch_size);
        queue.back().push_back(item);
        if (queue.back().size() == batch_size) {
          // Finished batch, so add new empty batch.
          queue.push_back(std::vector<Item>());
        }
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


struct SOS {
  std::unique_ptr<AutoParallelComp> factor_comp;
  std::unique_ptr<AutoParallelComp> try_comp;
  std::unique_ptr<GPUMethod> ways_gpu;
  std::unique_ptr<TryFilterGPU> tryfilter_gpu;
  std::unique_ptr<EligibleFilterGPU> eligiblefilter_gpu;

  // Pre-filtered; ready to have the number of ways computed on CPU.
  // Start number, then bitmask of numbers that can be skipped.
  std::unique_ptr<
    WorkQueue<std::pair<uint64_t, std::vector<uint8_t>>>
    > nways_queue;

  // Eligible. Ready to produce the actual ways on GPU.
  // An element is a number and its expected number of ways.
  std::unique_ptr<
    BatchedWorkQueue<std::pair<uint64_t, uint32_t>>
    > ways_queue;

  // Candidate for full try.
  std::unique_ptr<
    BatchedWorkQueue<TryMe>
    > try_queue;

  SOS() : status_per(10.0) {
    // Performance is pretty workload-dependent, so just tune in-process
    // rather than saving to disk.
    factor_comp.reset(new AutoParallelComp(40, 1000, false));
    try_comp.reset(new AutoParallelComp(12, 1000, false));

    eligiblefilter_gpu.reset(new EligibleFilterGPU(cl, EPOCH_GPU_CHUNK));
    ways_gpu.reset(new GPUMethod(cl, GPU_HEIGHT));
    tryfilter_gpu.reset(new TryFilterGPU(cl, GPU_HEIGHT));

    nways_queue.reset(
        new WorkQueue<std::pair<uint64_t, std::vector<uint8_t>>>);
    ways_queue.reset(
        new BatchedWorkQueue<std::pair<uint64_t, uint32_t>>(GPU_HEIGHT));
    try_queue.reset(new BatchedWorkQueue<TryMe>(TRY_BATCH_SIZE));
  }

  void GPUThread(int thread_idx) {
    for (;;) {
      std::optional<std::vector<std::pair<uint64_t, uint32_t>>> batchopt =
        ways_queue->WaitGet();

      if (!batchopt.has_value()) {
        // Done!
        status.Printf("GPU thread " APURPLE("%d") " done!\n", thread_idx);
        fflush(stdout);
        return;
      }

      auto batch = std::move(batchopt.value());
      const int real_batch_size = batch.size();

      // Last batch can be incomplete.
      CHECK(batch.size() <= GPU_HEIGHT);
      while (batch.size() < GPU_HEIGHT) {
        // Fill with dummy values.
        batch.push_back(GPUMethod::dummy);
      }
      std::vector<std::vector<std::pair<uint64_t, uint64_t>>> res =
        ways_gpu->GetWays(batch);

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

      // PERF: We copy the output to CPU and back, but we could
      // just do this in place. The main issue is making sure the
      // tests can still work.

      // TODO: Probably better in a separate thread so it can run
      // concurrently?
      uint64_t num_filtered = 0;
      bool gone = false;
      if (trybatch.size() == GPU_HEIGHT) {
        uint64_t rej = 0;
        trybatch = tryfilter_gpu->FilterWays(trybatch, &rej);
        INCREMENT_BY(rejected_f, rej);
        num_filtered = real_batch_size - trybatch.size();
        if (trybatch.empty()) gone = true;
      }

      // Add to CPU-side Try queue.
      uint64_t local_pending_try = 0;
      if (!trybatch.empty()) {
        local_pending_try = trybatch.size();
        try_queue->WaitAddVec(std::move(trybatch));
        trybatch.clear();
      }

      {
        MutexLock ml(&m);
        triple_pending_ways -= real_batch_size;
        pending_try += local_pending_try;
        if (gone) batches_completely_filtered++;
        done_gpu_filtered += num_filtered;
      }
    }
  }

  std::mutex m;
  Timer timer;
  // Counters and stats.
  // These were too big to process on the GPU.
  uint64_t too_big = 0;
  // Work stolen by CPU in endgame.
  uint64_t stolen = 0;
  int64_t recent_try_size = 0;
  uint64_t batches_completely_filtered = 0;
  // Numbers that can be written as the sum of squares at least three
  // different ways.
  uint64_t eligible_triples = 0;

  // State of a number. These always sum to epoch_size.
  uint64_t pending = 0;
  // Done. Many are rejected because they can't be written as the sum of
  // squares enough ways.
  uint64_t done_ineligible_gpu = 0;
  uint64_t done_ineligible_cpu = 0;
  // Waiting for ways calculation.
  uint64_t triple_pending_ways = 0;
  // Filtered out by GPU TryFilter.
  uint64_t done_gpu_filtered = 0;
  uint64_t pending_try = 0;
  uint64_t done_full_try = 0;

  bool nways_is_done = false;

  // Must hold lock.
  uint64_t NumDone() {
    return done_ineligible_cpu + done_ineligible_gpu + done_gpu_filtered +
      done_full_try;
  }

  int work_stealing_threads = STEADY_WORK_STEALING_THREADS;
  // Reset autoparallel when we get to the Try endgame, since at
  // this point the other CPU threads are free.
  bool try_endgame = false;
  // Signal that we're done and the status thread (and perhaps others
  // in the future) should exit. (TODO: std::jthread?)
  bool should_die = false;

  Periodically status_per;

  void PrintStats(uint64_t epoch_size) {
    MutexLock ml(&m);
    uint64_t done = NumDone();
    uint64_t tested = eligible_triples + done_ineligible_cpu +
      done_ineligible_gpu;
    double pct = (eligible_triples * 100.0)/(double)tested;
    double sec = timer.Seconds();
    double nps = done / sec;
    // Do we actually care about the eligible fraction?
    string line1 =
      StringPrintf("%llu/%llu (%.5f%%) are eligible. Took %s (%.1f/sec)\n",
                   eligible_triples, tested, pct, ANSI::Time(sec).c_str(), nps);

    const int64_t rf = READ(rejected_f);
    const int64_t rff = READ(rejected_ff);
    const int64_t rhh = READ(rejected_hh);
    const int64_t raa = READ(rejected_aa);

    const int64_t gpu_size = ways_queue->Size();
    const int64_t try_size = try_queue->Size();

    string line2 =
      StringPrintf("%lld " AGREY("rf")
                   " %lld " AGREY("rff") " %lld " AGREY("rhh")
                   " %lld " AGREY("raa")
                   "\n",
                   rf, rff, rhh, raa);
    string line3 = StringPrintf(ARED("%llu") " big  "
                                APURPLE("%s") " gpu q  "
                                ACYAN("%lld") " try q  "
                                ABLUE("%lld") " lts "
                                "(" AGREEN("%s") " stolen)\n",
                                too_big,
                                Util::UnsignedWithCommas(gpu_size).c_str(),
                                try_size,
                                recent_try_size,
                                Util::UnsignedWithCommas(stolen).c_str());
    string line4 =
      StringPrintf(
          ACYAN("%s") " try-filtered  "
          AGREEN("%s") " complete batches\n",
          Util::UnsignedWithCommas(done_gpu_filtered).c_str(),
          Util::UnsignedWithCommas(batches_completely_filtered).c_str());

    // Get the fractions other than pending.
    double blood = (100.0 * done_ineligible_gpu) / epoch_size;
    double red = (100.0 * done_ineligible_cpu) / epoch_size;
    double green = (100.0 * triple_pending_ways) / epoch_size;
    double blue = (100.0 * done_gpu_filtered) / epoch_size;
    double cyan = (100.0 * pending_try) / epoch_size;
    double white = (100.0 * done_full_try) / epoch_size;
    double black = (100.0 * pending) / epoch_size;

    string line5 =
      StringPrintf(AGREY("%.1f%% left") " "
                   AFGCOLOR(140, 10, 10, "%.1f%% igpu") " "
                   ARED("%.1f%% icpu") " "
                   AGREEN("%.1f%% pways") " "
                   ABLUE("%.1f%% filt gpu") " "
                   ACYAN("%.1f%% ptry") " "
                   AWHITE("%.1f%% full") "\n",
                   black, blood, red, green, blue, cyan, white);

    string line6;
    if (!nways_is_done) {
      string info = StringPrintf(
          "%s eligible",
          Util::UnsignedWithCommas(eligible_triples).c_str());
      line6 = ANSI::ProgressBar(done, epoch_size, info, sec) + "\n";
    } else {
      string info = StringPrintf("%llu gpu filtered + %llu full",
                                 done_gpu_filtered, done_full_try);
      ANSI::ProgressBarOptions options;
      options.bar_filled = 0x0f9115;
      options.bar_empty  = 0x001a03;

      line6 = ANSI::ProgressBar(done_gpu_filtered + done_full_try,
                                eligible_triples,
                                info, sec, options) + "\n";
    }

    status.EmitStatus(line1 + line2 + line3 + line4 + line5 + line6);
  }

  void StatusThread(uint64_t start_idx, uint64_t epoch_size) {
    static constexpr int WIDTH = 1024 + 512, HEIGHT = 768;
    std::unique_ptr<ImageRGBA> img;
    if (WRITE_IMAGE) {
      img.reset(new ImageRGBA(WIDTH, HEIGHT));
      img->Clear32(0x000000FF);
    }
    int xpos = 0;
    Periodically bar_per(0.5);
    for (;;) {
      // XXX would be nice to have an efficient Periodically::Await
      using namespace std::chrono_literals;
      std::this_thread::sleep_for(250ms);

      if (bar_per.ShouldRun()) {
        MutexLock ml(&m);

        if (img.get() != nullptr) {
          // Get the fractions other than pending.
          // double HEIGHT_SCALE = HEIGHT / EPOCH_SIZE;
          auto HeightOf = [epoch_size](double ctr) {
              return (int)std::round((ctr / epoch_size) * HEIGHT);
            };
          int blood = HeightOf(done_ineligible_gpu);
          int red = HeightOf(done_ineligible_cpu);
          int green = HeightOf(triple_pending_ways);
          int blue = HeightOf(done_gpu_filtered);
          int cyan = HeightOf(pending_try);
          int white = HeightOf(done_full_try);

          int left = HEIGHT - (blood + red + green + blue + cyan + white);

          // Now draw the column.
          int y = 0;
          for (int u = 0; u < left; u++)
            img->SetPixel32(xpos, y++, 0x333333FF);
          for (int u = 0; u < white; u++)
            img->SetPixel32(xpos, y++, 0xFFFFFFFF);
          for (int u = 0; u < cyan; u++)
            img->SetPixel32(xpos, y++, 0x00AAAAFF);
          for (int u = 0; u < blue; u++)
            img->SetPixel32(xpos, y++, 0x3333AAFF);
          for (int u = 0; u < green; u++)
            img->SetPixel32(xpos, y++, 0x33AA33FF);
          for (int u = 0; u < red; u++)
            img->SetPixel32(xpos, y++, 0x883333FF);
          for (int u = 0; u < blood; u++)
            img->SetPixel32(xpos, y++, 0x440000FF);
          xpos++;
        }
      }

      // XXX Since we can often skip the Try phase now, maybe this
      // should be in its own thread.
      if (status_per.ShouldRun()) {
        PrintStats(epoch_size);
      }

      {
        MutexLock ml(&m);
        if (should_die) {
          if (img.get() != nullptr) {
            string filename = StringPrintf("progress.%llu.png", start_idx);
            img->Save(filename);
            status.Printf("Wrote %s\n", filename.c_str());
          }

          // Get the fractions other than pending.
          // double HEIGHT_SCALE = HEIGHT / EPOCH_SIZE;
          auto HeightOf = [epoch_size](double ctr) {
              return (int)std::round((ctr / epoch_size) * HEIGHT);
            };
          int blood = HeightOf(done_ineligible_gpu);
          int red = HeightOf(done_ineligible_cpu);
          int green = HeightOf(triple_pending_ways);
          int blue = HeightOf(done_gpu_filtered);
          int cyan = HeightOf(pending_try);
          int white = HeightOf(done_full_try);

          int left = HEIGHT - (blood + red + green + blue + cyan + white);

          status.Printf(
              "\n\n\n\n"
              AGREY("%d") " "
              AFGCOLOR(140, 10, 10, "%d") " "
              ARED("%d") " " AGREEN("%d") " " ABLUE("%d") " "
              ACYAN("%d") " " AWHITE("%d") "\n\n\n\n",
              left, blood, red, green, blue, cyan, white);

          return;
        }
      }
    }
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

      auto &batch = batchopt.value();
      const size_t original_batch_size = batch.size();

      // PERF: Since adding the GPU filter, this is almost always
      // processing very small batches. So we may want to fuse
      // batches or something like that.
      //
      // Get even batches. We only really expect this to happen for
      // singletons (too big) so it doesn't need to be fast.
      while (!batch.empty() && batch.size() % TRY_ROLL_SIZE != 0) {
        TryMe tryme = std::move(batch.back());
        batch.pop_back();
        Try(tryme.num, tryme.squareways);
      }

      // Now in parallel, unrolled
      if (!batch.empty()) {
        const int rolls = batch.size() / TRY_ROLL_SIZE;
        try_comp->
          ParallelComp(
              rolls,
              [&batch](int major_idx) {
                for (int minor_idx = 0;
                     minor_idx < TRY_ROLL_SIZE;
                     minor_idx++) {
                  const TryMe &tryme =
                    batch[major_idx * TRY_ROLL_SIZE + minor_idx];
                  Try(tryme.num, tryme.squareways);
                }
              });
      }

      {
        MutexLock ml(&m);

        if (try_endgame) {
          status.Printf(ACYAN("Try Endgame") "\n");
          status.Printf(AWHITE("Old autoparallel histo") ":\n");
          status.Emit(try_comp->HistoString());

          try_comp.reset(new AutoParallelComp(30, 1000, false));
          try_endgame = false;
        }

        recent_try_size = original_batch_size;

        done_full_try += original_batch_size;
        pending_try -= original_batch_size;
      }
    }
  }

  void StealThread() {
    // GPU part is the bottleneck, so also run some of these on the
    // CPU. When we reach the endgame, we'll increase the number of threads.
    int num_threads = work_stealing_threads;
    for (;;) {
      if (num_threads == 0) {
        // When CPU is a bottleneck, don't do any stealing. Just wait for
        // the endgame.
        using namespace std::chrono_literals;
        std::this_thread::sleep_for(500ms);

        {
          MutexLock ml(&m);
          num_threads = work_stealing_threads;
        }
      } else {
        std::optional<std::vector<std::pair<uint64_t, uint32_t>>> batchopt =
          ways_queue->WaitGet();

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

        try_queue->WaitAddVec(std::move(output));
        output.clear();
        {
          MutexLock ml(&m);
          stolen += batchopt.value().size();
          triple_pending_ways -= batchopt.value().size();
          pending_try += batchopt.value().size();
          num_threads = work_stealing_threads;
        }
      }
    }
  }

  void NWaysThread() {

    for (;;) {
      std::optional<std::pair<uint64_t, std::vector<uint8_t>>> batchopt =
        nways_queue->WaitGet();

      if (!batchopt.has_value()) {
        status.Printf("NWays queue is done.\n");
        return;
      }

      // Otherwise, rip over that thing.

      const auto &[start, bitmask] = batchopt.value();
      factor_comp->
        ParallelComp(
            bitmask.size(),
            [this, start, &bitmask](int byte_idx) {
              int local_too_big = 0;
              int local_eligible_triples = 0;
              int local_pending_try = 0;
              int local_done_ineligible_cpu = 0;
              std::vector<std::pair<uint64_t, uint32_t>> todo_gpu;

              // Do the whole byte.
              const uint64_t base_sum = start + byte_idx * 8;
              const uint8_t byte = bitmask[byte_idx];

              // PERF might want to do multiple bytes at a time...
              for (int i = 0; i < 8; i++) {
                // Skip ones that were filtered.
                if (byte & (1 << (7 - i))) continue;

                const uint64_t sum = base_sum + i;

                // PERF can skip some of the tests we know were already
                // done on GPU.
                const int nways = ChaiWahWuNoFilter(sum);

                if (nways >= 3) {
                  if (nways > GPUMethod::MAX_WAYS) {
                    // Do on CPU.
                    std::vector<std::pair<uint64_t, uint64_t>> ways =
                      NSoks2(sum, nways);
                    TryMe tryme;
                    tryme.num = sum;
                    tryme.squareways = std::move(ways);
                    try_queue->WaitAdd(std::move(tryme));

                    local_too_big++;
                    local_eligible_triples++;
                    local_pending_try++;
                  } else {
                    todo_gpu.emplace_back(sum, nways);
                  }
                } else {
                  local_done_ineligible_cpu++;
                }
              }

              ways_queue->WaitAddVec(todo_gpu);

              {
                MutexLock ml(&m);
                // Work done in this thread
                pending -= 8;
                done_ineligible_gpu += std::popcount<uint8_t>(byte);
                too_big += local_too_big;
                eligible_triples += local_eligible_triples;
                pending_try += local_pending_try;

                // Added to GPU in batch.
                eligible_triples += todo_gpu.size();
                triple_pending_ways += todo_gpu.size();

                // Ineligible
                done_ineligible_cpu += local_done_ineligible_cpu;
              }
            });
    }
  }

  void RunEpoch(uint64_t epoch_start, uint64 epoch_size) {
    CHECK(cl != nullptr);

    status.Printf(
        AWHITE("==") " Start epoch " APURPLE("%s") "+ " AWHITE("==") "\n",
        Util::UnsignedWithCommas(epoch_start).c_str());
    pending = epoch_size;

    work_stealing_threads = STEADY_WORK_STEALING_THREADS;

    std::vector<std::thread> gpu_threads;
    for (int i = 0; i < NUM_GPU_THREADS; i++)
      gpu_threads.emplace_back(&GPUThread, this, i + 1);
    std::thread try_thread(&TryThread, this);
    std::thread steal_thread(&StealThread, this);
    std::thread status_thread(&StatusThread, this, epoch_start, epoch_size);
    std::thread nways_thread(&NWaysThread, this);

    ResetCounters();

    // How many sums we do with each GPU chunk.
    constexpr size_t EPOCH_CHUNK = EPOCH_GPU_CHUNK * 8;
    CHECK(epoch_size % EPOCH_CHUNK == 0) << epoch_size << " % " << EPOCH_CHUNK;
    {
      Timer eligible_gpu_timer;
      for (uint64_t u = 0; u < epoch_size; u += EPOCH_CHUNK) {
        uint64_t base = epoch_start + u;
        std::vector<uint8_t> bitmask = eligiblefilter_gpu->Filter(base);
        nways_queue->WaitAdd(std::make_pair(base, std::move(bitmask)));
      }
      status.Printf("Did GPU eligible in %s\n",
                    ANSI::Time(eligible_gpu_timer.Seconds()).c_str());
    }
    nways_queue->MarkDone();

    nways_thread.join();
    status.Printf("Nways thread done.\n");
    ways_queue->MarkDone();

    {
      MutexLock ml(&m);
      nways_is_done = true;
      work_stealing_threads = ENDGAME_WORK_STEALING_THREADS;
    }

    status.Printf("Waiting for Steal/GPU threads.\n");

    steal_thread.join();

    // Now actually wait for gpu thread, which should be done very shortly
    // since there's no more work.
    for (auto &gpu_thread : gpu_threads) gpu_thread.join();
    gpu_threads.clear();

    try_queue->MarkDone();

    status.Printf("Waiting for Try thread.\n");
    {
      MutexLock ml(&m);
      try_endgame = true;
    }
    try_thread.join();

    {
      MutexLock ml(&m);
      should_die = true;
    }
    status.Printf("Done! Join status thread.\n");
    status_thread.join();

    status.Printf(AGREEN("Done with epoch!") "\n");

    status.Printf(AWHITE("Factor autoparallel histo") ":\n");
    status.Emit(factor_comp->HistoString());
    status.Printf(AWHITE("Try autoparallel histo") ":\n");
    status.Emit(try_comp->HistoString());

    double sec = timer.Seconds();
    status.Printf("CPU stole " AGREEN("%lld")
                  " work units from GPU (" ACYAN("%.2f") "%%)\n",
                  stolen, (100.0 * stolen) / eligible_triples);
    status.Printf("Eligible triples: %llu/%llu\n",
                  eligible_triples, epoch_size);
    status.Printf(ARED("%llu") " (%.2f%%) were too big.\n",
                  too_big, (too_big * 100.0) / epoch_size);
    status.Printf(AGREEN("Done") " in %s. (%s/ea.)\n",
                  ANSI::Time(sec).c_str(),
                  ANSI::Time(sec / epoch_size).c_str());
    status.Printf(
        "Did %s-%s\n",
        Util::UnsignedWithCommas(epoch_start).c_str(),
        Util::UnsignedWithCommas(epoch_start + epoch_size - 1).c_str());
    {
      const int64_t rf = READ(rejected_f);
      const int64_t rff = READ(rejected_ff);
      const int64_t rhh = READ(rejected_hh);
      const int64_t raa = READ(rejected_aa);

      Interesting(
          StringPrintf("EPOCH %llu %llu %llu "
                       "%lld %lld %lld %lld\n",
                       epoch_start, epoch_size, eligible_triples,
                       rf, rff, rhh, raa));

      FILE *f = fopen("sos.txt", "ab");
      CHECK(f != nullptr);
      fprintf(f, "Done in %s (%s/ea.)\n",
              ANSI::StripCodes(ANSI::Time(sec)).c_str(),
              ANSI::StripCodes(ANSI::Time(sec / epoch_size)).c_str());
      fprintf(f,
              "%lld rf "
              " %lld rff %lld rhh"
              " %lld raa\n",
              rf, rff, rhh, raa);
      fprintf(f, "Did %llu-%llu\n",
              epoch_start, epoch_start + epoch_size - 1);
      fclose(f);
    }
  }

};

static std::pair<uint64_t, uint64_t> PredictNext() {
  MutexLock ml(&file_mutex);
  // PERF don't keep loading this
  Database db = Database::FromInterestingFile(INTERESTING_FILE);

  // This uses the (as yet unexplained) observation that every fifth
  // error on the h term follows a nice curve which is nearly linear.
  // Use these to predict zeroes and explore regions near the predicted
  // zeroes that haven't yet been explored.

  // To predict zeroes, we need 6 *consecutive* almost2 squares.
  // We should get smarter about this, as we'd always rather explore
  // smaller numbers. But for now we just look at the last ones that
  // were processed, since the database is dense as of starting this
  // idea out.

  // PERF not all of them!
  printf("db ranges:\n%s\n", db.Epochs().c_str());

  // XXX improve heuristics so I don't need to keep increasing this
  static constexpr int64_t INTERCEPT_LB = 8'000'000'000'000;

  const auto &almost2 = db.Almost2();
  auto it = almost2.begin();
  while (it->first < INTERCEPT_LB) ++it;

  // Iterator ahead by five.
  auto it5 = it;
  for (int i = 0; i < 5; i++) {
    if (it5 == almost2.end()) {
      printf("Don't even have 5 squares yet?\n");
      return db.NextToDo(MAX_EPOCH_SIZE);
    }
    it5 = std::next(it5);
  }

  /*
  db.ForEveryIntercept([]() {

    };
  */

  int64_t best_score = 99999999999999;
  int64_t best_start = 0;
  uint64_t best_size = 0;
  auto Consider = [&best_score, &best_start, &best_size](
      int64_t score, int64_t start, uint64_t size) {
      printf("Consider score " ACYAN("%lld") " @" APURPLE("%lld") "\n",
             score, start);
      if (score < best_score) {
        best_score = score;
        best_start = start;
        best_size = size;
      }
    };

  while (it5 != almost2.end()) {
    const int64_t x0 = it->first;
    const int64_t x1 = it5->first;

    // Must be dense here or else we don't know if these are on
    // the same arc.
    if (db.CompleteBetween(x0, x1)) {
      const int64_t y0 = Database::GetHerr(it->second);
      const int64_t y1 = Database::GetHerr(it5->second);

      // The closer we are to the axis, the better the
      // prediction will be. This also keeps us from going
      // way out on the number line.
      // (Another option would be to just prefer lower x.)
      const int64_t dist = std::min(std::abs(y0), std::abs(y1));

      // Predicted intercept.
      double dx = x1 - x0;
      double dy = y1 - y0;
      double m = dy / dx;
      int64_t iceptx = x0 + std::round(-y0 / m);

      auto PrVec = [&](const char *msg) {
          if (dist < 200000) {
            printf("(%lld, %lld) -> (%lld, %lld)\n"
                   "  slope %.8f icept "
                   AWHITE("%lld") " score " ACYAN("%lld") " %s\n",
                   x0, y0, x1, y1, m, iceptx, dist, msg);
          }
        };

      #if 0
      if (y0 > 0 && y1 < 0) {
        CHECK(iceptx >= x0 && iceptx <= x1);
        PrVec(AYELLOW("ZERO"));
      } else {
        // printntf("(%lld, %lld) -> (%lld, %lld)\n",
        //  x0, y0, x1, y1);
      }
      #endif

      if (iceptx > 0) {
        // For valid points, see what completed span they're in.
        if (!db.IsComplete(iceptx)) {
          // If none, explore that point.

          // Round to avoid fragmentation.
          int64_t c = iceptx;
          c /= MAX_EPOCH_SIZE;
          c *= MAX_EPOCH_SIZE;

          Consider(dist, c, MAX_EPOCH_SIZE);
          PrVec("NEW");
        } else {
          // Otherwise, look at all the vectors in there. If any
          // vector crosses zero, we're truly done with this island.
          // Otherwise, extend the span on one side.

          bool really_done = false;
          bool extend_right = true;
          int64_t closest_dist = 999999999999;
          db.ForEveryVec(iceptx,
                         [&](int64_t x0, int64_t y0,
                             int64_t x1, int64_t y1,
                             int64_t iceptx) {
                           if (y0 > 0 && y1 < 0) {
                             really_done = true;
                           } else {
                             if (y1 > 0) {
                               // above the axis, so we'd
                               // extend to the right to find zero.
                               if (y1 < closest_dist) {
                                 extend_right = true;
                                 closest_dist = y1;
                               }
                             } else {
                               CHECK(y0 < 0);
                               if (-y0 < closest_dist) {
                                 extend_right = false;
                                 closest_dist = -y0;
                               }
                             }
                           }
                         });

          if (!really_done) {
            if (extend_right) {
              auto nga = db.NextGapAfter(iceptx, MAX_EPOCH_SIZE);
              Consider(dist, nga.first, nga.second);
              PrVec("AFTER");
            } else {
              auto go = db.NextGapBefore(iceptx, MAX_EPOCH_SIZE);
              if (go.has_value()) {
                Consider(dist, go.value().first, go.value().second);
                PrVec("BEFORE");
              }
            }
          } else {
            PrVec("REALLY DONE");
          }
        }
      } else {
        // Otherwise, it's invalid.
      }

    }

    ++it;
    ++it5;
  }

  if (best_start <= 0 || best_size == 0) {
    printf("\n" ARED("No valid intercepts?") "\n");
    return db.NextToDo(MAX_EPOCH_SIZE);

  } else {
    printf("\nNext guess: " APURPLE("%llu") " +" ACYAN("%llu") "\n",
           best_start, best_size);

    CHECK(best_size <= MAX_EPOCH_SIZE);

    // The above is supposed to produce an interval that still
    // remains to be done, but skip to the next if not.
    return db.NextGapAfter(best_start, best_size);
  }
}

static void Run() {
  for (;;) {
    // const auto [epoch_start, epoch_size] = GetNext(MAX_EPOCH_SIZE);
    const auto [epoch_start, epoch_size] = PredictNext();
    printf("\n\n\n\n\n\n\n\n\n");
    SOS sos;
    sos.RunEpoch(epoch_start, epoch_size);
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

