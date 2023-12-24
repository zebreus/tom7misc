
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
#include "hashing.h"

#include "sos-gpu.h"
#include "clutil.h"
#include "mod-util.h"
#include "mod-gpu.h"
#include "auto-histo.h"

constexpr bool SELF_CHECK = false;

// #define

DECLARE_COUNTERS(done_quick, done_full, eliminated,
                 u4_, u5_, u6_, u7_, u8_);

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

    CHECK(p >= ModQuickPassGPU::MIN_PRIME) << p << " vs "
                                           << ModQuickPassGPU::MIN_PRIME;
    CHECK(p & 1) << p;

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

    int current_width = 0;
    std::unique_ptr<ModQuickPassGPU> quick_gpu;

    // PERF: This actually redoes p, rather than skipping to the
    // next prime to try. No harm in that, though.

    uint64_t recent_prime = p;

    // AutoHisto full_histo(1000);

    double full_seconds = 0.0;

    for (;;) {
      // Invariants...

      if (batch.empty()) {
        printf("Eliminated everything!\n");
        work->Save();
        break;
      }

      // Dynamically resize quick pass GPU.
      if (quick_gpu.get() == nullptr ||
          batch.size() != current_width) {
        quick_gpu = std::make_unique<ModQuickPassGPU>(cl,
                                                      QUICK_HEIGHT,
                                                      batch.size());
      }

      save_per.RunIf([&]() {
#if DEPTH_HISTO
          printf("Depth histo:\n"
                 "%s\n",
                 depth_histo->SimpleANSI(20).c_str());
#endif

          /*
          printf("Full histo:\n"
                 "%s\n",
                 full_histo.SimpleANSI(20).c_str());
          */

          work->Save();
          printf(AWHITE("Saved") ".\n");
        });

      status_per.RunIf([&work, &run_time, &dps_done, &dps_timer, &batch,
                        full_seconds, recent_prime]() {
          double sec = run_time.Seconds();
          uint64_t dq = done_quick.Read();
          uint64_t df = done_full.Read();
          double dps = dps_done / dps_timer.Seconds();

          auto LargeCount = [](uint64_t c) {
              if (c < 100000000ULL) return Util::UnsignedWithCommas(c);
              else return StringPrintf("%.1fB", c / 1000000000.0);
            };

          printf(ABLUE("%s") "+" AORANGE("%s") " ("
                 AWHITE("%.2fM") "/s) "
                 "Elim " AGREEN("%llu") " in %s (%s). "
                 "P: " APURPLE("%s") " "
                 "Left: " ACYAN("%d") "\n",
                 LargeCount(dq).c_str(),
                 LargeCount(df).c_str(),
                 dps / 1000000.0,
                 eliminated.Read(),
                 ANSI::Time(sec).c_str(),
                 ANSI::Time(full_seconds).c_str(),
                 LargeCount(recent_prime).c_str(),
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
      if (SELF_CHECK) {
        for (const uint64_t prime : primes) {
          CHECK(Factorization::IsPrime(prime));
        }
      }
      // printf("Got %lld primes\n", (int64_t)primes.size());

      // Do quick pass on GPU.
      std::vector<FullRun> full =
        quick_gpu->Run(primes, batch);

      // full_histo.Observe(full.size());
      done_quick += primes.size() * batch.size() - full.size();

      Timer full_timer;
      ParallelApp(
          full,
          [&](const FullRun &full_run) {
            {
              MutexLock ml(&mut);
              printf("Full run: (" ABLUE("%d") "," ACYAN("%d") ") mod "
                     APURPLE("%llu") "\n",
                     full_run.m, full_run.n, full_run.prime);

              if (work->GetNoSolAt(full_run.m, full_run.n) != 0) {
                printf(ARED(" ... already solved?") "\n");
              }
            }

            SolutionFinder<false> finder(full_run.prime);

            // Now solve the simultaneous equations mod p.
            if (!finder.HasSolutionModP(full_run.m, full_run.n)) {
              // Rare; ok for this to be slow.
              MutexLock ml(&mut);
              printf("\n\nNO SOL!\n");
              // Could be a duplicate in the batch of primes; keep
              // the first one.
              if (work->GetNoSolAt(full_run.m, full_run.n) == 0) {
                done.emplace_back(full_run.m, full_run.n);
                work->SetNoSolAt(full_run.m, full_run.n, full_run.prime);
                eliminated++;
              }
            }
          }, 8);
      full_seconds += full_timer.Seconds();

      done_full += full.size();
      dps_done += primes.size() * batch.size();

      recent_prime = primes.back();

      for (const auto &[m, n] : batch) {
        work->PrimeAt(m, n) = primes.back();
      }

      if (!done.empty()) {
        printf("Removing %d/%d:", (int)done.size(), (int)full.size());
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

          SolutionFinder<true> finder(p);

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
static void DoWorkCPU(Work *work,
                      uint64_t p,
                      std::vector<std::pair<int, int>> batch) {
  printf("Do CPU Work. p=%llu\n", p);

  Periodically save_per(60);
  Periodically status_per(5);

  std::vector<std::pair<int, int>> todo;

  Timer run_time;
  while (work->Remaining() > 0) {

    // Maybe shrink the list if we eliminated something.
    if (todo.empty() || work->Remaining() < todo.size()) {
      todo.clear();
      for (int m = -333; m <= 333; m++) {
        for (int n = -333; n <= 333; n++) {
          if (work->GetNoSolAt(m, n) == 0) {
            todo.emplace_back(m, n);
          }
        }
      }
    }

    save_per.RunIf([&work]() {
        work->Save();
        printf(AWHITE("Saved") ".\n");
      });

    status_per.RunIf([&work, &run_time, &todo]() {
        std::optional<uint64_t> recent_min;
        for (const auto &[m, n] : todo) {
          const uint64_t p = work->PrimeAt(m, n);
          if (!recent_min.has_value() ||
              p < recent_min.value()) {
            recent_min.emplace(p);
          }
        }

        double sec = run_time.Seconds();
        uint64_t done = done_full.Read();
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
               work->Remaining());
      });

    static constexpr int DEPTH = 12;

    std::vector<std::pair<int, int>> res =
      ParallelMap(todo, [&work](const std::pair<int, int> &mn) ->
      std::pair<int, int> {
          const auto &[m, n] = mn;
          // Already ruled this one out.
          if (work->GetNoSolAt(m, n)) return {-1, -1};

          uint64_t last_p = work->PrimeAt(m, n);
          for (int i = 0; i < DEPTH; i++) {
            // Otherwise, get old upper bound.
            const uint64_t p = Factorization::NextPrime(last_p);

            SolutionFinder<true> finder(p);

            // Now solve the simultaneous equations mod p.
            if (!finder.HasSolutionModP(m, n)) {
              eliminated++;
              return {p, p};
            }

            last_p = p;
          }

          done_full += DEPTH;
          return {-1, last_p};
        }, 8);

    // Write results without lock.
    for (int i = 0; i < todo.size(); i++) {
      const auto &[m, n] = todo[i];
      const auto &[sol, p] = res[i];
      if (sol != -1) work->SetNoSolAt(m, n, sol);
      if (p != -1) work->PrimeAt(m, n) = p;
    }
  }
}

// Several pieces of code assume that the moduli are not small,
// and that the batch is not enormous. So we start by running
// small moduli on the CPU, which eliminates a lot of cells.

inline int64_t Rem(int64_t a, int64_t b) {
  a = a % b;
  if (a < 0) a += b;
  return a;
}


// Cold start is slow (like n^3) because we can't use various tricks,
// but we do end up eliminating a lot of points by using non-prime
// moduli.
constexpr int64_t COLD_START_TO = 997;
static Work ColdStart() {
  Work work;

  Timer timer;
  Periodically save_per(60.0, false);
  Periodically status_per(1.0);

  int64_t cold_elim = 0;

  printf(ACYAN("Cold start") ":\n\n");

  // Note that we try all moduli here, not just primes.
  for (int64_t modulus = 2;
       modulus < COLD_START_TO;
       modulus++) {

    save_per.RunIf([&]() {
        work.Save();
        printf("\nSaved.\n\n");
      });
    status_per.RunIf([&]() {
        printf(ANSI_UP
               "%s\n",
               ANSI::ProgressBar(
                   modulus,
                   COLD_START_TO,
                   StringPrintf("Cold start elim %lld", cold_elim),
                   timer.Seconds()).c_str());
      });


    const int64_t max_coord = Work::MAXIMUM;
    // Note that for negative coordinates, this will
    // actually be a "large" residue.
    const int64_t min_coord = Rem(Work::MINIMUM, modulus);

    const bool verbose = false; // modulus == 7;

    if (verbose)
    printf("For modulus %lld, max %lld, min %lld\n",
           modulus, max_coord, min_coord);

    // We'll loop over every pair, so it is helpful to
    // avoid duplicates, even though this takes a few
    // steps.
    const std::vector<int64_t> squares_mod = SquaresModM(modulus);

    if (verbose) {
      printf("%lld squares mod %lld, %.1f%%:\n",
             (int64_t)squares_mod.size(),
             (int64_t)modulus,
             (squares_mod.size() * 100.0) / modulus);
      for (int64_t s : squares_mod) {
        printf("%lld ", s);
      }
      printf("\n");
    }

    std::mutex mutex;
    std::unordered_set<std::pair<int64_t, int64_t>,
      Hashing<std::pair<int64_t, int64_t>>> reachable_mn;

    ParallelApp(
        squares_mod,
        [modulus, min_coord, max_coord, verbose,
         &mutex, &squares_mod, &reachable_mn](int64_t xx) {
          // We're really trying b=y and c=y at the
          // same time here, with a=x.
          // It's easy to have sign errors on m,n. This is the
          // canonical form of the equations:
          // 222121 a^2 - b^2 + m = 0
          // 360721 a^2 - c^2 + n = 0
          // 222121 a^2 - b^2 = -m
          // 360721 a^2 - c^2 = -n

          const int64_t x1 = (222121 * xx); // % modulus;
          const int64_t x2 = (360721 * xx); // % modulus;

          const bool local_verbose = verbose && xx == 4;
          if (local_verbose) {
            printf("With xx=%lld, x1=%lld, x2=%lld\n",
                   xx, x1, x2);
          }

          std::unordered_set<
            std::pair<int, int>, Hashing<std::pair<int, int>>>
            local_reachable_mn;

          // Now try all (y^2, z^2) pairs. These are the residues
          // reachable for the same x^2.
          // for (int64_t yy : squares_mod) {
          for (int64_t y = 0; y < modulus; y++) {
            const int64_t yy = y * y;
            int64_t mres = Rem( -(x1 - yy), modulus);
            if (local_verbose) {
              printf("  y=%lld, y^2=%lld. (%lld - %lld) "
                     "gives mres=" ABLUE("%lld") "%s\n",
                     y, yy, x1, yy,
                     mres,
                     mres == 1 ? " **" : "");
            }
            if (mres <= max_coord || mres >= min_coord) {
              // for (int64_t zz : squares_mod) {
              for (int64_t z = 0; z < modulus; z++) {
                const int64_t zz = z * z;
                int64_t nres = Rem( -(x2 - zz), modulus);
                if (local_verbose) {
                  printf("    z=%lld, z^2=%lld (%lld - %lld) "
                         "gives nres=" APURPLE("%lld") "%s\n",
                         z, zz, x2, zz, nres,
                         (mres == 1 && nres == 5) ? " ###" : "");
                }
                // If the pair is of interest, record it.
                if (nres <= max_coord || nres >= min_coord) {
                  local_reachable_mn.insert(std::pair(mres, nres));
                } else {
                  if (local_verbose) printf(ARED("boring n. SKIPPED") "\n");
                }
              }
            } else {
              if (local_verbose) printf(ARED("boring m. SKIPPED") "\n");
            }

          }

          MutexLock ml(&mutex);
          for (const auto &mn : local_reachable_mn) reachable_mn.insert(mn);
        },
        8);

    if (verbose) {
      printf("All reachable residues:\n");
      for (const auto &[mres, nres] : reachable_mn) {
        printf("(%lld,%lld) ", mres, nres);
      }
      printf("\n");
      for (int nres = 0; nres < modulus; nres++) {
        for (int mres = 0; mres < modulus; mres++) {
          if (reachable_mn.contains(std::make_pair(mres, nres))) {
            printf(ABGCOLOR(120, 120, 0, "R"));
          } else {
            printf(" ");
          }
        }
        printf("\n");
      }
    }

    // Now are there are any m,n that were never reachable?
    for (int64_t m = Work::MINIMUM; m <= Work::MAXIMUM; m++) {
      const int64_t mres = Rem(m, modulus);
      for (int64_t n = Work::MINIMUM; n <= Work::MAXIMUM; n++) {
        if (work.GetNoSolAt(m, n) == 0) {
          const int64_t nres = Rem(n, modulus);

          if (!reachable_mn.contains(std::make_pair(mres, nres))) {
            work.SetNoSolAt(m, n, modulus);

            if (verbose) {
              printf("Rejected %lld,%lld (res %lld,%lld) with modulus %lld\n",
                     m, n, mres, nres, modulus);
            }

            // Verify when it's fast, since I had bugs in here before. :(
            if (modulus < 300) {
            auto so = SimpleSolve(m, n, modulus);
            if (so.has_value()) {
              const auto &[a, b, c] = so.value();
              printf("(" ABLUE("%lld") "," APURPLE("%lld") ") was eliminated "
                     "by " AYELLOW("%lld") ",\n"
                     "but it has solution "
                     ARED("%lld") ", " ACYAN("%lld") ", "
                     AORANGE("%lld") "!\n",
                     m, n, modulus,
                     a, b, c);
              printf(
                  "222121 * " ARED("%lld") "^2 - " ACYAN("%lld") "^2 - "
                  ABLUE("%lld") " = 0  (mod %lld)\n"
                  "360721 * " ARED("%lld") "^2 - " AORANGE("%lld") "^2 - "
                  APURPLE("%lld") " = 0  (mod %lld)\n",
                  a, b, m, modulus,
                  a, c, n, modulus);
              printf("So we should have had:\n"
                     "x1 = 222121 * " ARED("%lld") "^2 = %lld\n"
                     "x2 = 360721 * " ARED("%lld") "^2 = %lld\n",
                     a, 222121 * a * a,
                     a, 360721 * a * a);
              /*
              printf("Then %lld - "
                     ACYAN("%lld") "^2 = %lld = %lld (mod %lld)\n",
              */
              CHECK(false);
            }
            }
            cold_elim++;
          }
        }
      }
    }

    {
      const uint64_t wrong_mod = work.GetNoSolAt(269, -64);
      if (wrong_mod != 0) {
        auto so = SimpleSolve(269, -64, wrong_mod);
        CHECK(so.has_value()) << wrong_mod;

        CHECK(wrong_mod == 0) << "This is know to have "
          "a solution! mod: " << wrong_mod;
      }
    }
  }

  // Once we finish this loop, everything that's still eligible
  // is at MIN_PRIME.
  int64_t remain = 0, eligible_remain = 0;
  for (int64_t m = Work::MINIMUM; m <= Work::MAXIMUM; m++) {
    for (int64_t n = Work::MINIMUM; n <= Work::MAXIMUM; n++) {
      if (work.GetNoSolAt(m, n) == 0) {
        work.PrimeAt(m, n) = COLD_START_TO;
        remain++;
        if (Work::Eligible(m, n)) eligible_remain++;
      }
    }
  }

  printf("Remaining: %lld. Eligible: %lld\n", remain, eligible_remain);

  work.Save();
  return work;
}

int main(int argc, char **argv) {
  ANSI::Init();

  cl = new CL;

  Work work;
  if (!Work::Exists()) {
    printf(AYELLOW("Note: ") "No previous files; cold start!\n");
    work = ColdStart();
  } else {
    work.Load();
  }

  #if DEPTH_HISTO
  depth_histo = new AutoHisto(1000000);
  #endif

  const auto &[p, todo] = CatchUp(&work);
  printf("Points remaining: %d\n", (int)todo.size());
  printf("Next prime: %llu\n", p);

  // work.Save();

  if (p < ModQuickPassGPU::MIN_PRIME) {
    // XXX this should skip/exit if we reach GPU_PRIME.
    DoWorkCPU(&work, p, todo);
  }

  Mod mod;
  mod.DoWorkBatch(&work, p, todo);

  return 0;
}


