
// Trying to find integers a,b,c that solve
// 222121 a^2 - b^2 + m = 0
// 360721 a^2 - c^2 + n = 0
// for small m,n. This program finds (m, n) that have
// no solutions by solving the equations mod p.

#include <cstdint>
#include <array>
#include <memory>
#include <optional>

#include "base/logging.h"
#include "base/stringprintf.h"

#include "image.h"
#include "threadutil.h"
#include "ansi.h"
#include "timer.h"
#include "periodically.h"
#include "factorization.h"
#include "atomic-util.h"
#include "arcfour.h"
#include "randutil.h"
#include "numbers.h"
#include "vector-util.h"
#include "work-queue.h"
#include "util.h"

#include "sos-gpu.h"
#include "clutil.h"
#include "mod-util.h"
#include "auto-histo.h"

constexpr bool SELF_CHECK = false;

// #define

DECLARE_COUNTERS(tries, eliminated,
                 u3_, u4_, u5_, u6_, u7_, u8_);

static CL *cl = nullptr;

// Slow.
#if DEPTH_HISTO
static std::mutex histo_mutex;
static AutoHisto *depth_histo = nullptr;
#endif


// A full test: Run every residue mod p to see if there
// is no solution. We only do this if the the quick pass
// doesn't find a solution.
// prime, m, n
using FullRun = ModQuickPassGPU::FullRun;

struct Mod {
  // These are slow, so we do them one at a time.
  std::unique_ptr<WorkQueue<FullRun>> full_queue;

  // Primes for which to run a quick pass on the entire batch.
  static constexpr int QUICK_HEIGHT = 16384;
  std::unique_ptr<BatchedWorkQueue<uint64_t>> quick_queue;


  std::mutex should_die_mutex;
  bool should_die = false;

  // Iterates over primes on GPU and puts them in the queue.
  void PrimeThread(uint64_t start_prime) {
    static constexpr bool VERBOSE = false;
    static constexpr int ISPRIME_HEIGHT = 65536 * 4;
    static constexpr bool SELF_CHECK = false;
    IsPrimeGPU isprime_gpu(cl, ISPRIME_HEIGHT);

    for (;;) {
      if (VERBOSE) {
        printf("Prime thread top %llu\n", start_prime);
      }
      // Get some primes using GPU.
      // Note if we start this process over from the beginning, p cannot
      // be small (and this will abort). We can use a CPU process to get
      // started.
      std::vector<uint8_t> prime_bytemask =
        isprime_gpu.GetPrimes(start_prime);

      std::vector<uint64_t> batch;
      for (int i = 0; i < prime_bytemask.size(); i++) {
        if (prime_bytemask[i]) {
          uint64_t prime = start_prime + (i * 2);

          if (SELF_CHECK) {
            CHECK(Factorization::IsPrime(prime));
          }

          batch.push_back(prime);
        }
      }

      start_prime += ISPRIME_HEIGHT * 2;

      if (VERBOSE) {
        printf("Got %d primes from %llu\n", (int)batch.size(), start_prime);
      }

      // Don't get too far ahead of the consumer threads.
      // (We could deadlock here if there are no consumers because
      //  should_die?)
      if (!batch.empty()) {
        quick_queue->WaitUntilFewer(12);
        quick_queue->WaitAddVec(batch);
      }

      if (VERBOSE) {
        printf("Prime thread: Added.\n");
      }

      {
        MutexLock ml(&should_die_mutex);
        if (should_die) {
          printf("Prime thread ending.\n");
          quick_queue->MarkDone();
          return;
        }
      }
    }
  }

  // New version of the above that runs a "small" set of remaining
  // cells, all of which have the same p.
  void DoWorkBatch(
      Work *work,
      uint64_t p, std::vector<std::pair<int, int>> batch) {

    full_queue.reset(new WorkQueue<FullRun>());
    quick_queue.reset(new BatchedWorkQueue<uint64_t>(QUICK_HEIGHT));

    std::thread prime_thread(&Mod::PrimeThread, this, p);

    Periodically save_per(60);
    Periodically status_per(5);

    std::mutex mut;

    std::vector<std::pair<int, int>> done;

    Timer run_time;
    Timer dps_timer;
    int64_t dps_done = 0;

    // Check that the work is as expected.
    for (const auto &[m, n] : batch) {
      CHECK(work->GetNoSolAt(m, n) == 0);
      CHECK(Work::Eligible(m, n));
      CHECK(p == work->PrimeAt(m, n));
    }

    // PERF: This actually redoes p, rather than skipping to the
    // next prime to try. No harm in that, though.

    uint64_t recent_prime = p;

    for (;;) {
      // Invariants...

      if (batch.empty()) {
        printf("Eliminated everything!\n");
        work->Save();
        break;
      }

      save_per.RunIf([&work]() {
#if DEPTH_HISTO
          printf("Depth histo:\n"
                 "%s\n",
                 depth_histo->SimpleANSI(20).c_str());
#endif

          work->Save();
          printf(AWHITE("Saved") ".\n");
        });

      status_per.RunIf([&work, &run_time, &dps_done, &dps_timer, &batch,
                        recent_prime]() {
          double sec = run_time.Seconds();
          uint64_t done = tries.Read();
          double dps = dps_done / dps_timer.Seconds();

          printf(ABLUE("%s") " at " AWHITE("%.2fM") "/s "
                 "Elim " AGREEN("%llu") " in %s. "
                 "P: " APURPLE("%llu") " "
                 "Left: " ACYAN("%d") "\n",
                 Util::UnsignedWithCommas(done).c_str(),
                 dps / 1000000.0,
                 eliminated.Read(),
                 ANSI::Time(sec).c_str(),
                 recent_prime,
                 (int)batch.size());

          dps_timer.Reset();
          dps_done = 0;
        });

      std::optional<std::vector<uint64_t>> oprimes =
        quick_queue->WaitGet();
      if (!oprimes.has_value()) {
        printf("This should never happen, since we won't run out of "
               "primes and we only stop the primes thread if we exit "
               "the consumer loop.\n");
        break;
      }

      std::vector<uint64_t> primes = std::move(oprimes.value());
      // printf("Got %lld primes\n", (int64_t)primes.size());

      for (const uint64_t prime : primes) {
        recent_prime = prime;
        if (SELF_CHECK) {
          CHECK(Factorization::IsPrime(prime));
        }

        SolutionFinder finder(prime);

        // PERF: Do on GPU!
        // Note that this is fast on CPU because we often find a
        // solution right away (see depth histo). So GPU code should
        // probably be separated into a pre-pass (just try like 32)
        // and full pass.
        ParallelApp(batch, [&work, &mut, &done, &finder, prime](
            const std::pair<int, int> &mn) {
            const auto &[m, n] = mn;

            // Now solve the simultaneous equations mod p.
            if (!finder.HasSolutionModP(m, n)) {
              // Rare; ok for this to be slow.
              MutexLock ml(&mut);
              // Could be a duplicate in the batch of primes; keep
              // the first one.
              if (work->GetNoSolAt(m, n) == 0) {
                done.emplace_back(m, n);
                work->SetNoSolAt(m, n, prime);
                eliminated++;
              }
            }
          }, 8);

        tries += batch.size();
        dps_done += batch.size();
        // PERF only need to do this for the last prime...
        for (const auto &[m, n] : batch) {
          work->PrimeAt(m, n) = prime;
        }
      }

      if (!done.empty()) {
        printf("Removing:");
        for (const auto &[m, n] : done) {
          printf(" (" AGREEN("%d") "," AGREEN("%d") ")", m, n);
        }
        printf("\n");
        std::vector<std::pair<int, int>> batch_new;
        batch_new.reserve(batch.size() - 1);
        for (const auto &mn : batch) {
          if (!VectorContains(done, mn)) {
            batch_new.push_back(mn);
          }
        }

        batch = std::move(batch_new);
        done.clear();
      }
    }


    {
      MutexLock ml(&should_die_mutex);
      should_die = true;
    }

    // Note potential deadlock here (see PrimeThread). But
    // if we get here we'll rejoice!
    prime_thread.join();
  }

};

// Transition routine. Makes sure every non-eliminated cell is at the
// same prime p. Get that p and the remaining cells.
static std::pair<uint64_t, std::vector<std::pair<int, int>>> CatchUp(
    Work *work) {
  CHECK(work->Remaining() > 0) << "Work is already totally done!";

  Timer timer;
  // Get the maximum p.
  std::optional<uint64_t> catch_up_p = 0;
  for (int m = -333; m <= 333; m++) {
    for (int n = -333; n <= 333; n++) {
      if (work->GetNoSolAt(m, n) == 0 && Work::Eligible(m, n)) {
        uint64_t p = work->PrimeAt(m, n);
        if (!catch_up_p.has_value() || p > catch_up_p.value()) {
          catch_up_p.emplace(p);
        }
      }
    }
  }

  CHECK(catch_up_p.has_value()) << "No eligible cells remain!";
  const uint64_t target_p = catch_up_p.value();
  printf("\nTarget p: " APURPLE("%s") "\n",
         Util::UnsignedWithCommas(target_p).c_str());

  // Now run any stragglers until we've caught up.
  // We could do these in batch or whatever if we have to repeat
  // this process often, but currently we expect everything to
  // already be caught up.

  int64_t caught_up = 0;
  for (int m = -333; m <= 333; m++) {
    for (int n = -333; n <= 333; n++) {
      if (work->GetNoSolAt(m, n) == 0 && Work::Eligible(m, n)) {
        uint64_t p = work->PrimeAt(m, n);
        while (p < target_p) {
          p = Factorization::NextPrime(p);

          SolutionFinder finder(p);

          caught_up++;
          // Now solve the simultaneous equations mod p.
          if (!finder.HasSolutionModP(m, n)) {
            // What luck!

            work->SetNoSolAt(m, n, p);
            work->PrimeAt(m, n) = p;
            break;
          }

          work->PrimeAt(m, n) = p;
        }
      }
    }
  }

  printf("Ran " ABLUE("%lld") ". Caught up to " APURPLE("%llu")
         " in %s.\n",
         caught_up,
         target_p, ANSI::Time(timer.Seconds()).c_str());


  std::vector<std::pair<int, int>> todo;
  for (int m = -333; m <= 333; m++) {
    for (int n = -333; n <= 333; n++) {
      if (work->GetNoSolAt(m, n) == 0 && Work::Eligible(m, n)) {
        uint64_t p = work->PrimeAt(m, n);
        CHECK(p == target_p) << p << " vs " << target_p;
        todo.emplace_back(m, n);
      }
    }
  }

  printf("There are " ABLUE("%d") " remaining.\n", (int)todo.size());

  return std::make_pair(target_p, std::move(todo));
}

// Old, slower routine that is only useful for getting started on the
// CPU.
static void DoWork() {
  printf("Startup..\n");

  Work work;
  work.Load();

  Periodically save_per(60);
  Periodically status_per(5);

  ArcFour rc(StringPrintf("mod.%lld", time(nullptr)));

  std::vector<std::pair<int, int>> todo;

  Timer run_time;
  while (work.Remaining() > 0) {

    // Maybe transition to an explicit list, or shrink the list.
    if (work.Remaining() < 65536 && work.Remaining() < todo.size()) {
      todo.clear();
      for (int m = -333; m <= 333; m++) {
        for (int n = -333; n <= 333; n++) {
          if (work.GetNoSolAt(m, n) == 0) {
            todo.emplace_back(m, n);
          }
        }
      }
    }

    save_per.RunIf([&work]() {
        work.Save();
        printf(AWHITE("Saved") ".\n");
      });

    if (todo.empty()) {
      for (int i = 0; i < 32768; i++) {
        // We should probably be more systematic but I want
        // to start by getting some picture of the space.
        // Note, duplicates are quite inefficient here...
        int m = RandTo(&rc, 667) - 333;
        int n = RandTo(&rc, 667) - 333;
        // XXX can actually reject outside the diamond |m|+|n|<333
        todo.emplace_back(m, n);
      }
    }

    status_per.RunIf([&work, &run_time, &todo]() {
        std::optional<uint64_t> recent_min;
        for (const auto &[m, n] : todo) {
          const uint64_t p = work.PrimeAt(m, n);
          if (!recent_min.has_value() ||
              p < recent_min.value()) {
            recent_min.emplace(p);
          }
        }

        double sec = run_time.Seconds();
        uint64_t done = tries.Read();
        double dps = done / sec;

        printf(ABLUE("%s") " at " AWHITE("%.2fM") "/s "
               "Elim " AGREEN("%llu") " in %s. "
               "P: " APURPLE("%llu") " "
               "Left: " ACYAN("%d") "\n",
               Util::UnsignedWithCommas(done).c_str(),
               dps / 1000000.0,
               eliminated.Read(),
               ANSI::Time(sec).c_str(),
               recent_min.value_or(0),
               work.Remaining());
      });

    static constexpr int DEPTH = 12;

    // PERF: We could synchronize all the primes and then just
    // convert to Montgomery form once (including the coefficients).
    // This would also save us a lot of NextPrime calls.
    //
    // PERF: Suited to GPU!
    std::vector<std::pair<int, int>> res =
      ParallelMap(todo, [&work](const std::pair<int, int> &mn) ->
      std::pair<int, int> {
          const auto &[m, n] = mn;
          // Already ruled this one out.
          if (work.GetNoSolAt(m, n)) return {-1, -1};

          uint64_t last_p = work.PrimeAt(m, n);
          for (int i = 0; i < DEPTH; i++) {
            // Otherwise, get old upper bound.
            const uint64_t p = Factorization::NextPrime(last_p);

            SolutionFinder finder(p);

            // Now solve the simultaneous equations mod p.
            if (!finder.HasSolutionModP(m, n)) {
              eliminated++;
              return {p, p};
            }

            last_p = p;
          }

          tries += DEPTH;
          return {-1, last_p};
        }, 8);

    // Write results without lock.
    for (int i = 0; i < todo.size(); i++) {
      const auto &[m, n] = todo[i];
      const auto &[sol, p] = res[i];
      if (sol != -1) work.SetNoSolAt(m, n, sol);
      if (p != -1) work.PrimeAt(m, n) = p;
    }
  }
}


int main(int argc, char **argv) {
  ANSI::Init();

  cl = new CL;
  #if DEPTH_HISTO
  depth_histo = new AutoHisto(1000000);
  #endif

  // Early
  // DoWork();

  Work work;
  work.Load();
  const auto &[p, todo] = CatchUp(&work);
  // work.Save();

  Mod mod;
  mod.DoWorkBatch(&work, p, todo);

  return 0;
}


