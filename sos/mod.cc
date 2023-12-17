
// Trying to find integers a,b,c that solve
// 222121 a^2 - b^2 + m = 0
// 360721 a^2 - c^2 + n = 0
// for small m,n. This program finds (m, n) that have
// no solutions by solving the equations mod p.

#include <cstdint>
#include <array>

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

#include "util.h"
#include "base/logging.h"
#include "base/stringprintf.h"

#include "sos-gpu.h"
#include "clutil.h"
#include "work.h"

constexpr bool SELF_CHECK = false;

DECLARE_COUNTERS(tries, eliminated,
                 u3_, u4_, u5_, u6_, u7_, u8_);

static CL *cl = nullptr;

struct SolutionFinder {
  const MontgomeryRep64 p;
  Montgomery64 coeff_1;
  Montgomery64 coeff_2;

  explicit SolutionFinder(uint64_t p_int) : p(p_int) {
    coeff_1 = p.ToMontgomery(222121);
    coeff_2 = p.ToMontgomery(360721);
  }

  bool HasSolutionModP(int m_int, int n_int) const {
    const Montgomery64 m = p.ToMontgomery(m_int);
    const Montgomery64 n = p.ToMontgomery(n_int);

    // Sub is a little faster, so pre-negate these.
    const Montgomery64 neg_m = p.Negate(m);
    const Montgomery64 neg_n = p.Negate(n);

    for (int64_t idx = 0; idx < p.Modulus(); idx++) {
      // Get one of the residues. We don't care what order we
      // do them in; we just need to try them all.
      Montgomery64 a = p.Nth(idx);
      Montgomery64 aa = p.Mult(a, a);

      // 222121 a^2 - (-m) = b^2
      // 360721 a^2 - (-n) = c^2

      Montgomery64 a1m = p.Sub(p.Mult(coeff_1, aa), neg_m);
      Montgomery64 a2n = p.Sub(p.Mult(coeff_2, aa), neg_n);

      // Compute Euler criteria. a^((p-1) / 2) must be 1.
      // Since p odd prime, we can just shift down by one
      // to compute (p - 1)/2.
      const auto &[r1, r2] = p.Pows(std::array<Montgomery64, 2>{a1m, a2n},
                                    p.Modulus() >> 1);

      bool sol1 = p.Eq(r1, p.One()) || p.Eq(a1m, p.Zero());
      bool sol2 = p.Eq(r2, p.One()) || p.Eq(a2n, p.Zero());

      if (sol1 && sol2) {
        return true;
      }
    }

    return false;
  }

};



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
        std::optional<uint32_t> recent_min;
        for (const auto &[m, n] : todo) {
          const uint32_t p = work.PrimeAt(m, n);
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
               "P: " APURPLE("%d") " "
               "Left: " ACYAN("%d") "\n",
               Util::UnsignedWithCommas(done).c_str(),
               dps / 1000000.0,
               eliminated.Read(),
               ANSI::Time(sec).c_str(),
               (int)recent_min.value_or(0),
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

          uint32_t last_p = work.PrimeAt(m, n);
          for (int i = 0; i < DEPTH; i++) {
            // Otherwise, get old upper bound.
            const uint32_t p = Factorization::NextPrime(last_p);

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

// New version of the above that runs a "small" set of remaining
// cells, all of which have the same p.
static void DoWorkBatch(
    Work *work,
    uint64_t p, std::vector<std::pair<int, int>> batch) {

  static constexpr int ISPRIME_HEIGHT = 65536;
  IsPrimeGPU isprime_gpu(cl, ISPRIME_HEIGHT);

  Periodically save_per(60);
  Periodically status_per(5);

  std::mutex mut;

  std::vector<std::pair<int, int>> done;

  Timer run_time;
  Timer dps_timer;
  int64_t dps_done = 0;

  // PERF: This actually redoes p, rather than skipping to the
  // next prime to try. No harm in that, though.

  while (!batch.empty()) {
    // Invariants...
    if (SELF_CHECK) {
      for (const auto &[m, n] : batch) {
        CHECK(work->GetNoSolAt(m, n) == 0);
        CHECK(Work::Eligible(m, n));
        CHECK(p == work->PrimeAt(m, n));
      }
    }

    save_per.RunIf([&work]() {
        work->Save();
        printf(AWHITE("Saved") ".\n");
      });

    status_per.RunIf([&work, &run_time, &dps_done, &dps_timer, &batch, p]() {

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
               p,
               (int)batch.size());

        dps_timer.Reset();
        dps_done = 0;
      });

    // Get some primes using GPU.
    // Note if we start this process over from the beginning, p cannot
    // be small (and this will abort). We can use a CPU process to get
    // started.
    std::vector<uint8_t> prime_bytemask = isprime_gpu.GetPrimes(p);

    for (int i = 0; i < prime_bytemask.size(); i++) {
      if (prime_bytemask[i]) {
        uint64_t prime = p + (i * 2);

        if (prime > 0x80000000ULL) {
          work->Save();
          CHECK(false) << "Need to expand database to support 64-bit primes!";
        }

        SolutionFinder finder(prime);

        // PERF: Do on GPU!
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
    }

    // Skip that whole batch of primes then.
    p += prime_bytemask.size() * 2;

    if (!done.empty()) {
      printf("Removing:");
      for (const auto &[m, n] : done) {
        printf(" (" AGREEN("%d") "," AGREEN("%d") ")", m, n);
      }
      printf("\n");
      std::vector<std::pair<int, int>> batch_new;
      batch_new.reserve(batch.size() - 1);
      for (const auto &p : batch) {
        if (!VectorContains(done, p)) {
          batch_new.push_back(p);
        }
      }

      batch = std::move(batch_new);
      done.clear();
    }
  }

}

// Make sure every non-eliminated cell is at the same
// prime p. Get that p and the cells.
static std::pair<uint64_t, std::vector<std::pair<int, int>>> CatchUp(
    Work *work) {
  CHECK(work->Remaining() > 0) << "Work is already totally done!";

  Timer timer;
  // Get the maximum p.
  std::optional<uint32_t> catch_up_p = 0;
  for (int m = -333; m <= 333; m++) {
    for (int n = -333; n <= 333; n++) {
      if (work->GetNoSolAt(m, n) == 0 && Work::Eligible(m, n)) {
        uint32_t p = work->PrimeAt(m, n);
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
  // this process often.

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

int main(int argc, char **argv) {
  ANSI::Init();

  cl = new CL;

  // Early
  // DoWork();

  Work work;
  work.Load();
  const auto &[p, todo] = CatchUp(&work);
  // work.Save();

  DoWorkBatch(&work, p, todo);

  return 0;
}
