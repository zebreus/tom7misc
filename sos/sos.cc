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
#include "factorization.h"

#include "database.h"
#include "sos-util.h"
#include "sos-gpu.h"
#include "predict.h"

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
    if (prev_status_lines.empty()) {
      for (int i = 0; i < num_lines; i++) {
        printf("\n");
      }
    } else {
      EmitStatusLinesWithLock(prev_status_lines);
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
    prev_status_lines = lines;
    MoveUp();
    EmitStatusLinesWithLock(lines);
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
            ANSI_PREVLINE
            // Clear line
            ANSI_CLEARLINE
               );
      }
    }
    first = false;
  }

  void EmitStatusLinesWithLock(const std::vector<std::string> &lines) {
    if (lines.size() != num_lines) {
      printf(ARED("...wrong number of lines...") "\n");
    }
    for (const string &line : lines) {
      printf("%s\n", line.c_str());
    }
  }

  std::mutex m;
  int num_lines = 0;
  bool first = true;
  std::vector<std::string> prev_status_lines;
};

// Tuned by sos-gpu_test.
// static constexpr int GPU_HEIGHT = 49912;
// static constexpr int GPU_HEIGHT = 51504;
// 131072 0.205us/ea.
// 131072x8 0.246us/ea.
static constexpr int GPU_WAYS_HEIGHT = 131072;
// PERF: Tune!
// static constexpr int GPU_FACTOR_HEIGHT = 8192;
// 8192: 2m52s
// 131072: 2m19s
// 524288: 2m15s
//  (then with larger numbers...)
// 524288x2: 2m23s
// 524288x4: 3m24s
// static constexpr int GPU_FACTOR_HEIGHT =  524288 * 2;

// tuned
// static constexpr int GPU_FACTOR_HEIGHT = 2875870;
// #define GPU_FACTOR_TUNING FactorizeGPU::IsPrimeRoutine::GENERAL, false, false, false, true, 2243

static constexpr int GPU_FACTOR_HEIGHT = 3552322;
#define GPU_FACTOR_TUNING FactorizeGPU::IsPrimeRoutine::GENERAL, true, true, true, false, 2393

// static constexpr int GPU_FACTOR_HEIGHT = 3058640;

static constexpr int NUM_GPU_WAYS_THREADS = 2;
static constexpr int TRY_BATCH_SIZE = 256;
static constexpr int TRY_ROLL_SIZE = 32;
static_assert(TRY_BATCH_SIZE % TRY_ROLL_SIZE == 0,
              "this is not strictly required, but would hurt "
              "performance!");

// PERF Probably better to increase this as the speed has improved.
static constexpr uint64_t MAX_EPOCH_SIZE = 800'000'000 * 2; /* ' */
// Each of these yields 8 sums. If it's larger, we may run off the
// end of the batch, although this is usually a trivial cost.
// This phase is super fast, but large chunks cause us to allocate
// memory for pending work.
static constexpr size_t EPOCH_GPU_CHUNK = 12'500'000; // 100000000;

// PERF: Tune it
static constexpr int STEADY_WORK_STEALING_THREADS = 0;
// Be careful not to set this too low, or everything will stall
// on a stolen batch (size GPU_WAYS_HEIGHT) that is processed by a
// small number of threads.
static constexpr int ENDGAME_WORK_STEALING_THREADS = 0;

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
                 other_almost2, batches_factored, unused3_, unused4_);

#define INCREMENT(rej) (rej)++
#define INCREMENT_BY(rej, by) (rej) += (by)
#define READ(rej) rej.Read()
#define RESET(rej) rej.Reset()

static void ResetCounters() {
  RESET(rejected_f);
  RESET(rejected_ff);
  RESET(rejected_hh);
  RESET(rejected_aa);
  RESET(other_almost2);
  RESET(batches_factored);
}


// TODO: To threadutil?
template<class Item>
struct BatchedWorkQueue {
  const int batch_size = 0;
  explicit BatchedWorkQueue(int batch_size) : batch_size(batch_size) {
    CHECK(batch_size > 0);
    // Set up invariant.
    queue.push_back(std::vector<Item>{});
    queue.back().reserve(batch_size);
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
      // It's the responsibility of the functions that insert into the
      // queue to maintain the presence of an incomplete vector. So we
      // can just remove the full one.
      queue.pop_front();
    }
    cond.notify_all();

    return {batch};
  }

  bool IsDone() {
    std::unique_lock ml(mutex);
    return done;
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
      MaybeFinishBatch();
    }
    cond.notify_all();
  }

  void WaitAdd(Item &&item) {
    {
      std::unique_lock ml(mutex);
      CHECK(!done);
      CHECK(!queue.empty() && queue.back().size() < batch_size);
      queue.back().emplace_back(item);
      size++;
      MaybeFinishBatch();
    }
    cond.notify_all();
  }

  void WaitAddVec(const std::vector<Item> &items) {
    if (items.empty()) return;
    {
      std::unique_lock ml(mutex);
      CHECK(!done);
      // PERF: This can be tighter, but the main thing is to avoid
      // repeatedly taking the lock.
      size += items.size();
      for (const Item &item : items) {
        CHECK(!queue.empty() && queue.back().size() < batch_size);
        queue.back().push_back(item);
        MaybeFinishBatch();
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

  // Wait until the number of pending batches is fewer than the
  // argument. An incomplete batch counts, including an empty one, so
  // there is always at least one pending batch. This can be used to
  // efficiently throttle threads that add to the queue. Queue may
  // not be done.
  void WaitUntilFewer(int num_batches) {
    CHECK(num_batches > 0) << "This would never return.";
    {
      std::unique_lock ml(mutex);
      CHECK(!done);
      cond.wait(ml, [this, num_batches] {
          return queue.size() < num_batches;
        });
    }
    // State hasn't changed, so no need to notify others.
  }

 private:
  // Must hold lock.
  inline void MaybeFinishBatch() {
    if (queue.back().size() == batch_size) {
      // Finished batch, so add new empty batch.
      queue.push_back(std::vector<Item>());
      queue.back().reserve(batch_size);
    }
  }

  std::mutex mutex;
  std::condition_variable cond;
  // Add at the end. This always consists of a series (maybe zero)
  // of full vectors and an incomplete vector (maybe empty) at the
  // end. (Unless "done", in which case it can be empty.)
  std::deque<std::vector<Item>> queue;
  // Size is the number of items, not batches.
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

struct GPUFactored {
  // The numbers.
  std::vector<uint64_t> nums;
  // Up to FactorizeGPU::MAX_FACTORS * |nums| factors.
  std::vector<uint64_t> factors;
  // Parallel to nums. Gives the number of factors. If 0xFF,
  // then factoring on the GPU failed; need to fail back to
  // a CPU method.
  std::vector<uint8_t> num_factors;
};

struct SOS {
  std::unique_ptr<AutoParallelComp> nways_comp;
  std::unique_ptr<AutoParallelComp> try_comp;
  std::unique_ptr<GPUMethod> ways_gpu;
  std::unique_ptr<TryFilterGPU> tryfilter_gpu;
  std::unique_ptr<EligibleFilterGPU> eligiblefilter_gpu;

  // Pre-filtered; ready to have the number of ways computed on CPU.
  // These are divided by stride. Because they come in chunks, they
  // may exceed the epoch size (so the consumer should check).
  // (base, bitmask)
  // if bit b is CLEAR in the bitmask,
  //  then (base + b) has passed the filter.
  std::unique_ptr<
    WorkQueue<std::pair<uint64_t, std::vector<uint8_t>>>
    > prefiltered_queue;

  // Prefiltered numbers grouped into batches for GPU factoring.
  // We keep this limited in size so that we don't have to allocate
  // gigabytes up front.
  std::unique_ptr<
    BatchedWorkQueue<uint64_t>
    > factor_queue;

  // Pre-filtered and factored by GPU. Ready to compute the
  // eligible ones using the nways test.
  std::unique_ptr<
    WorkQueue<GPUFactored>
    > factored_queue;

  // Eligible. Ready to produce the actual ways on GPU.
  // An element is a number and its expected number of ways.
  std::unique_ptr<
    BatchedWorkQueue<std::pair<uint64_t, uint32_t>>
    > ways_queue;

  // Candidate for full try.
  std::unique_ptr<
    BatchedWorkQueue<TryMe>
    > try_queue;

  // We find so many almost2 squares now that it's a performance
  // problem to lock / write them to disk / print them to screen.
  // This is set up so that we could write them to disk occasionally,
  // but right now we just flush them at the end of the epoch.
  std::unique_ptr<
    WorkQueue<Database::Square>
    > almost2_queue;

  SOS() : status_per(10.0) {
    // Performance is pretty workload-dependent, so just tune in-process
    // rather than saving to disk.
    // We're much less CPU bound now, so using lower max_parallelism here.
    // was 30, 12
    nways_comp.reset(new AutoParallelComp(16, 1000, false));
    try_comp.reset(new AutoParallelComp(8, 1000, false));

    eligiblefilter_gpu.reset(
        new EligibleFilterGPU(cl, EPOCH_GPU_CHUNK));
    ways_gpu.reset(new GPUMethod(cl, GPU_WAYS_HEIGHT));
    tryfilter_gpu.reset(new TryFilterGPU(cl, GPU_WAYS_HEIGHT));

    prefiltered_queue.reset(
        new WorkQueue<std::pair<uint64_t, std::vector<uint8_t>>>);
    factor_queue.reset(
        new BatchedWorkQueue<uint64_t>(GPU_FACTOR_HEIGHT));
    factored_queue.reset(new WorkQueue<GPUFactored>);

    ways_queue.reset(
        new BatchedWorkQueue<std::pair<uint64_t, uint32_t>>(GPU_WAYS_HEIGHT));
    try_queue.reset(new BatchedWorkQueue<TryMe>(TRY_BATCH_SIZE));
    almost2_queue.reset(new WorkQueue<Database::Square>);
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
  inline void AllWays(
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
        if (q != p) {
          const auto &[d, g] = ways[q];
          const auto &[dd, gg] = ways_squared[q];

          // require that the smallest of b,c,d,g appears on the
          // top, to reduce symmetries.
          if (std::min(b, c) > std::min(d, g))
            continue;

          for (int r = 0; r < ways.size(); r++) {
            if (r != p && r != q) {
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

  void Try(int z,
           const std::vector<std::pair<uint64_t, uint64_t>> &ways) {

    // PERF we don't actually need the roots until we print it out,
    // so we could just pass the squares and compute sqrts in the rare
    // case that we get through filters.
    AllWays(
        ways,
        [this, z](
            /*     a */ uint64_t b,  uint64_t bb, uint64_t c,  uint64_t cc,
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

                // but if hh or aa is a square, record it
                const uint64_t aa = sum - (bb + cc);
                const auto ao = Sqrt64Opt(aa);
                const uint64_t hh = sum - (bb + ee);
                const auto ho = Sqrt64Opt(hh);

                if (ao.has_value() || ho.has_value()) {
                  INCREMENT(other_almost2);
                  if (ao.has_value() && ho.has_value()) {
                    uint64_t f = Sqrt64(ff);
                    Interesting(
                        StringPrintf(
                            // For easy parsing. Everything is its squared version.
                            "(!!!) %llu %llu %llu %llu %llu %llu %llu %llu %llu\n"
                            "%llu^2 %llu^2 %llu^2\n"
                            "%llu^2 %llu^2 " ARED("sqrt(%llu)^2") "\n"
                            "%llu^2 %llu^2 %llu^2\n"
                            ARED("but %llu * %llu != %llu") "\n"
                            "sum: %llu\n",
                            aa, bb, cc, dd, ee, ff, gg, hh, ii,
                            ao.value(), b, c,
                            d, e, ff,
                            g, ho.value(), i,
                            // error
                            f, f, ff,
                            sum));
                  } else {
                    Database::Square square = {
                      aa, bb, cc,
                      dd, ee, ff,
                      gg, hh, ii
                    };
                    almost2_queue->WaitAdd(square);
                  }
                }
                return;
              }
              const uint64_t f = fo.value();

              const uint64_t hh = sum - (bb + ee);
              const auto ho = Sqrt64Opt(hh);
              if (!ho.has_value()) {
                const uint64_t aa = sum - (bb + cc);
                INCREMENT(rejected_hh);
                Database::Square square = {
                  aa, bb, cc,
                  dd, ee, ff,
                  gg, hh, ii
                };
                almost2_queue->WaitAdd(square);
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
                        "sum: %llu\n",
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
                             "sum: %llu\n"
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


  void GPUWaysThread(int thread_idx) {
    for (;;) {
      std::optional<std::vector<std::pair<uint64_t, uint32_t>>> batchopt =
        ways_queue->WaitGet();

      if (!batchopt.has_value()) {
        // Done!
        status.Printf("GPU ways thread " APURPLE("%d") " done!\n", thread_idx);
        fflush(stdout);
        return;
      }

      auto batch = std::move(batchopt.value());
      const int real_batch_size = batch.size();

      // Last batch can be incomplete.
      CHECK(batch.size() <= GPU_WAYS_HEIGHT);
      while (batch.size() < GPU_WAYS_HEIGHT) {
        // Fill with dummy values.
        batch.push_back(GPUMethod::dummy);
      }
      std::vector<std::vector<std::pair<uint64_t, uint64_t>>> res =
        ways_gpu->GetWays(batch);

      // Rejoin with the number. PERF: We could avoid some copying here
      // if it's a bottleneck.
      std::vector<TryMe> trybatch;
      trybatch.reserve(GPU_WAYS_HEIGHT);
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
      if (trybatch.size() == GPU_WAYS_HEIGHT) {
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
  // Failed factoring on GPU.
  uint64_t cpu_factored = 0;

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
    return done_ineligible_cpu + done_ineligible_gpu +
      done_gpu_filtered + done_full_try;
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
    uint64_t tested = eligible_triples +
      done_ineligible_cpu + done_ineligible_gpu;
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
    const int64_t other2 = READ(other_almost2);

    const int64_t bfactored = READ(batches_factored);

    const int64_t prefiltered_size =
      // Each item represents a chunk of this many bits = numbers.
      EPOCH_GPU_CHUNK * 8 *
      prefiltered_queue->Size();
    const int64_t factor_size = factor_queue->Size();
    const int64_t factored_size =
      // Each GPUFactored item is this many individual numbers.
      GPU_FACTOR_HEIGHT *
      factored_queue->Size();
    const int64_t ways_size = ways_queue->Size();
    const int64_t try_size = try_queue->Size();


    string line2 =
      StringPrintf("%lld " AGREY("rf")
                   " %lld " AGREY("rff") " "
                   AORANGE("%lld") " " AGREY("almost2") " "
                   ACYAN("%lld") " " AGREY("other2")
                   " %lld " AGREY("raa")
                   "\n",
                   rf, rff, rhh, other2, raa);
    #define ARROW AFGCOLOR(50, 50, 50, "\xE2\x86\x92") " "

    auto FormatNum = [](uint64_t n) {
        if (n > 1'000'000) {
          double m = n / 1000000.0;
          if (m >= 100.0) {
            return StringPrintf("%dM", (int)std::round(m));
          } else if (m > 10.0) {
            return StringPrintf("%.1fM", m);
          } else {
            // TODO: Integer division. color decimal place and suffix.
            return StringPrintf("%.2fM", m);
          }
        } else {
          return Util::UnsignedWithCommas(n);
        }
      };
    string line3 = StringPrintf(
        "Q: "
        AFGCOLOR(200, 80,  80,  "%s") " pre " ARROW
        AFGCOLOR(200, 80,  200, "%s") " fact " ARROW
        AFGCOLOR(200, 200, 80,  "%s") " nways " ARROW
        AFGCOLOR(80,  80,  200, "%s") " ways " ARROW
        AFGCOLOR(80,  200, 200, "%s") " try " ARROW
        AFGCOLOR(80,  200, 80,  "%s") " done\n",
        FormatNum(prefiltered_size).c_str(),
        FormatNum(factor_size).c_str(),
        FormatNum(factored_size).c_str(),
        FormatNum(ways_size).c_str(),
        FormatNum(try_size).c_str(),
        FormatNum(done).c_str());

    // other stuff
    string line4 =
      StringPrintf(
          ARED("%llu") " big  "
          AGREEN("%s") " stolen  "
          ACYAN("%s") " try-filtered  "
          AGREEN("%s") " complete batches\n",
          too_big,
          FormatNum(stolen).c_str(),
          FormatNum(done_gpu_filtered).c_str(),
          FormatNum(batches_completely_filtered).c_str());

    double cpu_pct = (100.0 * cpu_factored) / (bfactored * GPU_FACTOR_HEIGHT);
    string line5 =
      StringPrintf(AWHITE("%s") " batches factored. "
                   AORANGE("%s") " numbers on CPU (%.2f%%)\n",
                   FormatNum(bfactored).c_str(),
                   FormatNum(cpu_factored).c_str(),
                   cpu_pct);

    // Since 80%+ are immediately rejected (prefilter) we don't show
    // those in the fraction here.
    uint64_t denom = epoch_size - done_ineligible_gpu;
    uint64_t numer = done - done_ineligible_gpu;

    string line6 = [&](){
      double frac = numer / (double)denom;
      double spe = numer > 0 ? sec / numer : 1.0;
      double remaining_sec = (denom - numer) * spe;
      string eta = ANSI::Time(remaining_sec);
      int eta_len = ANSI::StringWidth(eta);

      int bar_width = 70 - 2 - 1 - eta_len;
      // Number of characters that get background color.
      // int filled_width = std::clamp((int)(bar_width * frac), 0, bar_width);

      string bar_info =
        StringPrintf("%llu / %llu (%.2f%%) ", numer, denom, frac * 100.0);

      int total_width = 0;
      std::vector<pair<uint32_t, int>> bgcolors;
      auto AddWidthOf = [&](uint32_t color, int64_t c) {
          if (c == 0) return;
          double f = std::clamp(c / (double)denom, 0.0, 1.0);
          int w = std::max((int)std::round(f * bar_width), 1);
          total_width += w;
          bgcolors.emplace_back(color, w);
        };

      AddWidthOf(0x400000FF, prefiltered_size);
      AddWidthOf(0x400070FF, factor_size);
      AddWidthOf(0x404000FF, factored_size);
      AddWidthOf(0x000040FF, ways_size);
      AddWidthOf(0x005050FF, try_size);
      AddWidthOf(0x008000FF, numer);
      // Padding.
      if (total_width < bar_width) {
        AddWidthOf(0x202020FF, bar_width - total_width);
      }

      std::vector<pair<uint32_t, int>> fgcolors =
        {{0xFFFFFFBB, bar_width}};

      return StringPrintf(
          AWHITE("[") "%s" AWHITE("]") " %s\n",
          ANSI::Composite(bar_info, fgcolors, bgcolors).c_str(),
          eta.c_str());
      }();

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
    Periodically img_per(0.250);
    for (;;) {
      // XXX would be nice to have an efficient Periodically::Await
      using namespace std::chrono_literals;
      std::this_thread::sleep_for(125ms);

      if (img.get() != nullptr) {
        img_per.RunIf([&](){
            MutexLock ml(&m);

            // Size of each queue.
            const int64_t prefiltered_size =
              // Each item represents a chunk of this many bits = numbers.
              EPOCH_GPU_CHUNK * 8 *
              prefiltered_queue->Size();
            const int64_t factor_size = factor_queue->Size();
            const int64_t factored_size =
              // Each GPUFactored item is this many individual numbers.
              GPU_FACTOR_HEIGHT *
              factored_queue->Size();
            const int64_t ways_size = ways_queue->Size();
            const int64_t try_size = try_queue->Size();
            const int64_t done = NumDone();

            struct Phase {
              uint32_t color;
              const char *name;
              int64_t value;
            };

            // Same colors as status bar. TODO: Consolidate.
            std::vector<Phase> phases = {
              Phase{
                .color = 0xC85050FF,
                .name = "pre",
                .value = prefiltered_size,
              },
              Phase{
                .color = 0xC850C8FF,
                .name = "fact",
                .value = factor_size,
              },
              Phase{
                .color = 0xC8C850FF,
                .name = "nways",
                .value = factored_size,
              },
              Phase{
                .color = 0x5050C8FF,
                .name = "ways",
                .value = ways_size,
              },
              Phase{
                .color = 0x50C8C8FF,
                .name = "try",
                .value = try_size,
              },
              Phase{
                .color = 0x50C850FF,
                .name = "done",
                .value = done,
              },
            };

            // Get the fractions other than pending.
            // double HEIGHT_SCALE = HEIGHT / EPOCH_SIZE;
            auto HeightOf = [epoch_size](double ctr) {
                return (int)std::round((ctr / epoch_size) * HEIGHT);
              };

            int remain = HEIGHT;
            for (const Phase &phase : phases) {
              remain -= HeightOf(phase.value);
            }

            // Now draw the column.
            int y = 0;
            for (int u = 0; u < remain; u++)
              img->SetPixel32(xpos, y++, 0x333333FF);
            for (int p = phases.size() - 1; p >= 0; p--) {
              int pheight = HeightOf(phases[p].value);
              uint32_t pcolor = phases[p].color;
              for (int u = 0; u < pheight; u++) {
                img->SetPixel32(xpos, y++, pcolor);
              }
            }
            xpos++;
          });
      }

      // XXX Since we can often skip the Try phase now, maybe this
      // should be in its own thread.
      status_per.RunIf([&](){
          PrintStats(epoch_size);
        });

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
        almost2_queue->MarkDone();
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
              [this, &batch](int major_idx) {
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

          try_comp.reset(new AutoParallelComp(12, 1000, false));
          try_endgame = false;
        }

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
        if (ways_queue->IsDone()) {
          status.Printf("Steal thread finds queue empty while idle.\n");
          return;
        }

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

        status.Printf("Stealing %lld with %d threads\n",
                      batchopt.value().size(), num_threads);
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

  void BatchFactorsThread(uint64_t epoch_start, uint64_t epoch_size) {
    for (;;) {
      std::optional<std::pair<uint64_t, std::vector<uint8_t>>> batchopt =
        prefiltered_queue->WaitGet();

      if (!batchopt.has_value()) {
        status.Printf("Batch factors thread is done.\n");
        return;
      }

      int64_t local_ineligible = 0;

      // Expand the bitmask and the start into a dense vector of factors.
      std::vector<uint64_t> tofactor;
      const auto &[start, bitmask] = batchopt.value();
      for (int byte_idx = 0; byte_idx < bitmask.size(); byte_idx++) {
        const uint64_t base_sum = start + byte_idx * 8;
        const uint8_t byte = bitmask[byte_idx];
        // PERF might want to do multiple bytes at a time...


        // Note: This can overcount if we go past the epoch size.
        local_ineligible += std::popcount<uint8_t>(byte);
        for (int i = 0; i < 8; i++) {
          // Skip ones that were filtered.
          if (byte & (1 << (7 - i))) continue;
          const uint64_t sum = base_sum + i;

          if (sum >= epoch_start + epoch_size) continue;
          tofactor.push_back(sum);
        }
      }

      // The representation of the expanded factors is much larger
      // than the input, and fast to produce. So don't eagerly fill
      // the queue; that just results in higher peak memory usage.
      static constexpr int TARGET_GPU_FACTOR_PENDING = 4;
      factor_queue->WaitUntilFewer(TARGET_GPU_FACTOR_PENDING);

      factor_queue->WaitAddVec(tofactor);
      tofactor.clear();

      {
        MutexLock ml(&m);
        // Work done in this thread
        done_ineligible_gpu += local_ineligible;
        // XXX count num prefiltered (bitmask.size() * 8)
      }
    }
  }

  void GPUFactorThread() {
    /*
    FactorizeGPU factorize_gpu(
        cl, GPU_FACTOR_HEIGHT,
        FactorizeGPU::IsPrimeRoutine::GENERAL,
        false,
        false,
        true,
        1063);
    */
    FactorizeGPU factorize_gpu(
        cl, GPU_FACTOR_HEIGHT,
        GPU_FACTOR_TUNING);

    /*
    FactorizeGPU factorize_gpu(
        cl, GPU_FACTOR_HEIGHT,
        FactorizeGPU::IsPrimeRoutine::GENERAL,
        false,
        true,
        false,
        2243);
    */

    for (;;) {
      std::optional<vector<uint64_t>> batchopt =
        factor_queue->WaitGet();

      if (!batchopt.has_value()) {
        status.Printf("Factor queue is done.\n");
        return;
      }

      vector<uint64_t> nums = std::move(batchopt.value());
      batchopt.reset();

      // Add padding if not full width.
      // PERF: It would actually be pretty easy for this kernel
      // to support short calls.
      int original_size = nums.size();
      // Something easy to factor.
      while (nums.size() < GPU_FACTOR_HEIGHT)
        nums.push_back(2);

      // Run on GPU.
      std::pair<std::vector<uint64_t>, std::vector<uint8>> res =
        factorize_gpu.Factorize(nums);

      // Trim padding.
      nums.resize(original_size);
      res.first.resize(original_size * FactorizeGPU::MAX_FACTORS);
      res.second.resize(original_size);

      GPUFactored f{
        .nums = std::move(nums),
        .factors = std::move(res.first),
        .num_factors = std::move(res.second),
      };

      factored_queue->WaitAdd(std::move(f));
      // XXX counters etc.

      batches_factored++;
    }
  }

  // Takes factored numbes and computes the number of ways they
  // can be written as a sum of squares. Filters out numbers
  // with too few. Executes numbers with too many for GPU; but
  // most get added to the ways_queue to be done on the GPU.
  void NWaysThread() {
    for (;;) {
      std::optional<GPUFactored> batchopt =
        factored_queue->WaitGet();

      if (!batchopt.has_value()) {
        status.Printf("Factored queue is done.\n");
        return;
      }

      // We have a short GPUFactored at the end, so this code has to
      // handle anything. But ideally this should divide the common case of
      // GPU_FACTOR_HEIGHT.
      //
      // Note that this code has two exceptional and slow conditions:
      // Needing to factor on CPU, and/or needing to do nways on the CPU.
      // As this gets larger, we increase the risk of straggler tasks.
      //
      // Batching to 8 didn't make a difference in the end-to-end speed
      // here, but it did make the parallelism less broken-looking.
      static constexpr int ROLL_SIZE = 32;

      const GPUFactored &gpu_factored = batchopt.value();

      // Three parallel arrays.
      const int batch_size = gpu_factored.nums.size();
      const int num_rolls = (batch_size / ROLL_SIZE) +
        ((batch_size % ROLL_SIZE == 0) ? 0 : 1);

      // status.Printf("Batch size %d. num_rolls %d.\n", batch_size, num_rolls);

      nways_comp->
        ParallelComp(
            num_rolls,
            [&](int roll_idx) {

              int local_triple_pending_ways = 0;
              int local_cpu_factored = 0;
              int local_eligible_triples = 0;
              int local_too_big = 0;
              int local_pending_try = 0;
              int local_done_ineligible_cpu = 0;

              std::vector<std::pair<uint64_t, uint32_t>> ways_todo;
              ways_todo.reserve(ROLL_SIZE);

              for (int r = 0; r < ROLL_SIZE; r++) {
                const int idx = roll_idx * ROLL_SIZE + r;
                // Batch might not be divisible by roll :(
                if (idx >= batch_size) break;

                const uint64_t num = gpu_factored.nums[idx];
                const uint8_t num_factors_byte = gpu_factored.num_factors[idx];

                uint64_t bases[15];
                uint8_t exponents[15];
                int nf = 0;

                if (num_factors_byte == 0xFF) {
                  // GPU factoring failed. Factor on CPU.
                  // PERF: Can make use of the partial factoring here.
                  nf = Factorization::FactorizePreallocated(
                      num, bases, exponents);
                  local_cpu_factored++;
                } else {
                  // copy and collate factors.
                  for (int f = 0; f < num_factors_byte; f++) {
                    uint64_t factor =
                      gpu_factored.factors[idx * FactorizeGPU::MAX_FACTORS + f];
                    // Search from the end because usually we have repeated
                    // factors consecutively. My belief is that this array is
                    // generally very small, so sorted data structures are
                    // actually worse.
                    for (int slot = nf - 1; slot >= 0; slot--) {
                      if (bases[slot] == factor) {
                        exponents[slot]++;
                        goto next_factor;
                      }
                    }

                    // not found. insert at end.
                    bases[nf] = factor;
                    exponents[nf] = 1;
                    nf++;

                  next_factor:;
                  }
                }

                // Using the existing factoring, and skipping
                // tests we know were already done on GPU.
                const int nways = ChaiWahWuFromFactors(
                    num, bases, exponents, nf);

                if (nways >= 3) {
                  local_eligible_triples++;
                  if (nways > GPUMethod::MAX_WAYS) {
                    // Do on CPU.
                    std::vector<std::pair<uint64_t, uint64_t>> ways =
                      NSoks2(num, nways);
                    TryMe tryme;
                    tryme.num = num;
                    tryme.squareways = std::move(ways);
                    // This is rare, so we don't try to batch them
                    // within the roll. (Could consider accumulating
                    // them at the batch level?)
                    try_queue->WaitAdd(std::move(tryme));

                    local_too_big++;
                    local_pending_try++;
                  } else {
                    ways_todo.emplace_back(num, nways);
                  }
                } else {
                  local_done_ineligible_cpu++;
                }
              }

              ways_queue->WaitAddVec(std::move(ways_todo));

              {
                MutexLock ml(&m);
                cpu_factored += local_cpu_factored;
                done_ineligible_cpu += local_done_ineligible_cpu;
                too_big += local_too_big;
                eligible_triples += local_eligible_triples;
                pending_try += local_pending_try;
                triple_pending_ways += local_triple_pending_ways;
              }
            });
    }
  }

  void RunEpoch(uint64_t epoch_start, uint64 epoch_size) {
    CHECK(cl != nullptr);

    status.Printf(
        AWHITE("==") " Start epoch " APURPLE("%s") "+" ACYAN("%s") AWHITE("==") "\n",
        Util::UnsignedWithCommas(epoch_start).c_str(),
        Util::UnsignedWithCommas(epoch_size).c_str());
    pending = epoch_size;

    work_stealing_threads = STEADY_WORK_STEALING_THREADS;

    std::vector<std::thread> gpu_ways_threads;
    for (int i = 0; i < NUM_GPU_WAYS_THREADS; i++)
      gpu_ways_threads.emplace_back(&GPUWaysThread, this, i + 1);
    std::thread steal_thread(&StealThread, this);
    std::thread status_thread(&StatusThread, this, epoch_start, epoch_size);

    std::thread batch_factors_thread(&BatchFactorsThread,
                                     this, epoch_start, epoch_size);
    std::thread gpu_factor_thread(&GPUFactorThread, this);
    std::thread nways_thread(&NWaysThread, this);
    std::thread try_thread(&TryThread, this);

    ResetCounters();

    // How many sums we do with each GPU chunk.
    constexpr size_t EPOCH_CHUNK = EPOCH_GPU_CHUNK * 8;
    CHECK(epoch_size % EPOCH_CHUNK == 0) << epoch_size << " % " << EPOCH_CHUNK;
    {
      Timer eligible_gpu_timer;
      // Now we generate chunks of the given size, until we've covered
      // the epoch.
      int num_batches = 0;
      for (uint64_t u = 0; u < epoch_size; u += EPOCH_CHUNK) {
        num_batches++;
        uint64_t base = epoch_start + u;
        std::vector<uint8_t> bitmask = eligiblefilter_gpu->Filter(base);
        prefiltered_queue->WaitAdd(std::make_pair(base, std::move(bitmask)));
      }
      status.Printf("Did GPU eligible (%d batches of %lld) in %s\n",
                    num_batches, EPOCH_CHUNK,
                    ANSI::Time(eligible_gpu_timer.Seconds()).c_str());
    }
    prefiltered_queue->MarkDone();

    batch_factors_thread.join();
    status.Printf("Batch factors thread done.\n");
    factor_queue->MarkDone();

    gpu_factor_thread.join();
    status.Printf("GPU factoring thread done.\n");
    factored_queue->MarkDone();

    nways_thread.join();
    status.Printf("Nways thread done.\n");
    ways_queue->MarkDone();

    {
      MutexLock ml(&m);
      nways_is_done = true;
      work_stealing_threads = ENDGAME_WORK_STEALING_THREADS;
    }

    status.Printf("Set steal threads to " ABLUE("%d") ". "
                  "Waiting for CPU/GPU Ways threads.\n",
                  ENDGAME_WORK_STEALING_THREADS);

    steal_thread.join();

    // Now actually wait for gpu thread, which should be done very shortly
    // since there's no more work.
    for (auto &gpu_thread : gpu_ways_threads) gpu_thread.join();
    gpu_ways_threads.clear();

    try_queue->MarkDone();

    const uint64_t num_stolen = ReadWithLock(&m, &stolen);
    status.Printf(ABLUE("%llu") " nums were stolen by CPU.\n",
                  num_stolen);

    status.Printf("Waiting for Try thread.\n");
    {
      MutexLock ml(&m);
      try_endgame = true;
    }
    try_thread.join();

    // Write squares.

    {
      string squares;
      // Maybe should filter out squares whose gcd isn't 1, or reduce them?
      while (std::optional<Database::Square> so = almost2_queue->WaitGet()) {
        const auto &[aa, bb, cc, dd, ee, ff, gg, hh, ii] = so.value();
        StringAppendF(&squares,
                      // For easy parsing. Everything is its squared version.
                      "(!) %llu %llu %llu %llu %llu %llu %llu %llu %llu\n",
                      aa, bb, cc, dd, ee, ff, gg, hh, ii);
      }
      Interesting(squares);
    }

    {
      MutexLock ml(&m);
      should_die = true;
    }
    status.Printf("Done! Join status thread.\n");
    status_thread.join();

    status.Printf(AGREEN("Done with epoch!") "\n");

    status.Printf(AWHITE("NWays autoparallel histo") ":\n");
    status.Emit(nways_comp->HistoString());
    status.Printf(AWHITE("Endgame Try autoparallel histo") ":\n");
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


static void PrintGaps(const Database &db) {
  int64_t max_agap = 0, max_hgap = 0;
  db.ForEveryZeroVec(
      [&max_agap](int64_t x0, int64_t y0,
                  int64_t x1, int64_t y1,
                  int64_t iceptx) {
        max_agap = std::max(max_agap, x1 - x0);
      },
      [&max_hgap](int64_t x0, int64_t y0,
                  int64_t x1, int64_t y1,
                  int64_t iceptx) {
        max_hgap = std::max(max_hgap, x1 - x0);
      });

  printf("Max agap: %lld.\n"
         "Max hgap: %lld.\n",
         max_agap, max_hgap);
}

static std::pair<uint64_t, uint64_t> PredictNextNewton() {
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

      // Consider the appropriate action on the island unless it's
      // already done.
      using enum Database::IslandZero;
      if (iceptx > 0) {
        switch (db.IslandHZero(iceptx)) {
        case NONE: {
          // If none, explore that point.

          // Round to avoid fragmentation.
          int64_t c = iceptx;
          c /= MAX_EPOCH_SIZE;
          c *= MAX_EPOCH_SIZE;

          Consider(dist, c, MAX_EPOCH_SIZE);
          PrVec("NEW");
          break;
        }
        case HAS_ZERO:
          PrVec("REALLY DONE");
          break;

        case NO_POINTS:
        case GO_LEFT: {
          auto go = db.NextGapBefore(iceptx, MAX_EPOCH_SIZE);
          if (go.has_value()) {
            Consider(dist, go.value().first, go.value().second);
            PrVec("BEFORE");
          }
          break;
        }

        case GO_RIGHT: {
          auto nga = db.NextGapAfter(iceptx, MAX_EPOCH_SIZE);
          Consider(dist, nga.first, nga.second);
          PrVec("AFTER");
          break;
        }
        }
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

static std::pair<uint64_t, uint64_t> PredictNextRegression(bool predict_h) {
  MutexLock ml(&file_mutex);
  // PERF don't keep loading this
  Database db = Database::FromInterestingFile(INTERESTING_FILE);
  PrintGaps(db);

  printf("Get zero for " AWHITE("%s") " using regression...\n",
         predict_h ? "H" : "A");
  const auto &[azeroes, hzeroes] = db.GetZeroes();
  const int64_t iceptx = predict_h ?
    Predict::NextInDenseSeries(hzeroes) :
    Predict::NextInDensePrefixSeries(db, azeroes);
  printf("Predict next zero at: " ABLUE("%lld") "\n", iceptx);

  if (!db.IsComplete(iceptx)) {
    // If none, explore that point.

    // Round to avoid fragmentation.
    int64_t c = iceptx;
    c /= MAX_EPOCH_SIZE;
    c *= MAX_EPOCH_SIZE;

    printf("Haven't done it yet, so run " APURPLE("%lld") "+\n", c);
    return make_pair(c, MAX_EPOCH_SIZE);
  } else {

    bool really_done = false;
    // Prefer extending to the left until we have some evidence, since
    // this seems to usually be an overestimate.
    bool extend_right = false;
    int64_t closest_dist = 999999999999;

    // XXX this is wrong for predict_h = false. ForEverVec is specifically
    // looping over h vecs.
    // TODO: Use Island
    db.ForEveryHVec(iceptx,
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

    // a has an upward slope, so we need to reverse the direction
    // if we are looking for a zeroes.
    // XXX clean this up
    if (!predict_h) extend_right = !extend_right;

    if (really_done) {
      // ???
      printf(ARED("Unexpected") ": We have a zero here but regression "
             "predicted it elsewhere?");
      return db.NextGapAfter(0, MAX_EPOCH_SIZE);
    }

    if (extend_right) {
      printf("Extend " AGREEN("right") ". Closest %lld\n", closest_dist);
      return db.NextGapAfter(iceptx, MAX_EPOCH_SIZE);
    } else {
      auto go = db.NextGapBefore(iceptx, MAX_EPOCH_SIZE);
      if (go.has_value()) {
        printf("Extend " AGREEN("left") ". Closest %lld\n", closest_dist);
        return go.value();
      } else {
        printf("Extend " AORANGE("right") " (no space). "
               "Closest %lld\n", closest_dist);
        return db.NextGapAfter(iceptx, MAX_EPOCH_SIZE);
      }
    }
  }
}

static std::pair<uint64_t, uint64_t> PredictNextClose() {
  MutexLock ml(&file_mutex);
  // PERF don't keep loading this
  Database db = Database::FromInterestingFile(INTERESTING_FILE);
  PrintGaps(db);

  const auto &[azeroes, hzeroes] = db.GetZeroes();
  printf("Get close calls...\n");
  static constexpr int64_t MAX_INNER_SUM = 10'000'000'000'000'000LL;
  std::vector<std::pair<int64_t, double>> close =
    Predict::FutureCloseCalls(db, azeroes, hzeroes, MAX_INNER_SUM);

  // Top close calls..
  printf("Closest:\n");
  for (int i = 0; i < close.size(); i++) {
    const auto [iceptx, score] = close[i];

    using enum Database::IslandZero;
    if (iceptx > 0) {
      printf("  %s (dist %f) ",
             Util::UnsignedWithCommas(iceptx).c_str(), score);

      switch (db.IslandHZero(iceptx)) {
      case NONE: {
        // If none, explore that point.

        // Round to avoid fragmentation.
        int64_t c = iceptx;
        c /= MAX_EPOCH_SIZE;
        c *= MAX_EPOCH_SIZE;

        printf(AGREEN("NEW") "\n");
        return make_pair(c, MAX_EPOCH_SIZE);
      }
      case HAS_ZERO:
        printf(AGREY("DONE") "\n");
        break;

      case NO_POINTS:
      case GO_LEFT: {
        auto go = db.NextGapBefore(iceptx, MAX_EPOCH_SIZE);
        if (go.has_value()) {
          printf(ABLUE("BEFORE") "\n");
          return go.value();
        }
        break;
      }

      case GO_RIGHT: {
        printf(AYELLOW("AFTER") "\n");
        return db.NextGapAfter(iceptx, MAX_EPOCH_SIZE);
      }
      }
    }
  }

  printf(ARED("No candidates beneath %lld!") "\n", MAX_INNER_SUM);
  return db.NextGapAfter(0, MAX_EPOCH_SIZE);
}



static void Run() {
  for (;;) {
    const auto [epoch_start, epoch_size] = GetNext(MAX_EPOCH_SIZE);
    // const auto [epoch_start, epoch_size] = PredictNextRegression(true);
    // const auto [epoch_start, epoch_size] = PredictNextClose();
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

