
// Trying to find integers x,y that solve
// 222121 x^2 - y^2 + m = 0
// 360721 x^2 - y^2 + n = 0
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

#include "base/stringprintf.h"

DECLARE_COUNTERS(tries, eliminated,
                 u3_, u4_, u5_, u6_, u7_, u8_);

// Since we already have a solution with overall error (|m| + |n|) 333,
// we only try candidates in [-333, 333].
struct Work {

  inline uint32_t &PrimeAt(int m, int n) {
    CHECK(m >= -333 && m <= 333 &&
          n >= -333 && n <= 333) << m << "," << n;
    const int y = m + 333;
    const int x = n + 333;
    return prime[y * WIDTH + x];
  }

  inline uint32_t &NoSolAt(int m, int n) {
    CHECK(m >= -333 && m <= 333 &&
          n >= -333 && n <= 333) << m << "," << n;
    const int y = m + 333;
    const int x = n + 333;
    return nosol[y * WIDTH + x];
  }

  Work() {
    printf("Init??\n");
    fflush(stdout);
    return;
    for (int y = 0; y < HEIGHT; y++) {
      const int m = y - 333;
      for (int x = 0; x < WIDTH; x++) {
        const int n = x - 333;
        PrimeAt(m, n) = 0;
        NoSolAt(m, n) = 0;
      }
    }
  }

  void Save() {
    ImageRGBA prime_image(WIDTH, HEIGHT);
    ImageRGBA nosol_image(WIDTH, HEIGHT);
    for (int y = 0; y < HEIGHT; y++) {
      const int m = y - 333;
      for (int x = 0; x < WIDTH; x++) {
        const int n = x - 333;

        prime_image.SetPixel32(x, y, PrimeAt(m, n));
        nosol_image.SetPixel32(x, y, NoSolAt(m, n));
      }
    }

    prime_image.Save("mod-prime.png");
    nosol_image.Save("mod-nosol.png");
  }

  void Load() {
    std::unique_ptr<ImageRGBA> prime_image(
        ImageRGBA::Load("mod-prime.png"));

    std::unique_ptr<ImageRGBA> nosol_image(
        ImageRGBA::Load("mod-nosol.png"));

    if (prime_image.get() == nullptr ||
        nosol_image.get() == nullptr) {
      printf("Note: Couldn't load work from disk.\n");
    }


    for (int y = 0; y < HEIGHT; y++) {
      const int m = y - 333;
      for (int x = 0; x < WIDTH; x++) {
        const int n = x - 333;

        PrimeAt(m, n) = prime_image->GetPixel32(x, y);
        NoSolAt(m, n) = nosol_image->GetPixel32(x, y);
      }
    }
  }

private:
  static constexpr int WIDTH = 667;
  static constexpr int HEIGHT = 667;

  // The largest prime that we've tried (or zero).
  std::array<uint32_t, WIDTH * HEIGHT> prime;
  // If nonzero, then we checked that there is no solution mod this
  // prime. This means there's no solution on integers.
  std::array<uint32_t, WIDTH * HEIGHT> nosol;
};

inline uint32_t Mod(int64_t a, uint32_t b) {
  int64_t r = a % b;
  if (r < 0) r += b;
  return r;
}

// XXX PERF: We can of course do much better than just trying
// all x,y!!
static bool HasSolutionModP(int m, int n, uint32_t p) {

  m = Mod(m, p);
  n = Mod(n, p);

  bool has_solution = false;
  ParallelComp(
      p,
      [m, n, p, &has_solution](int64_t x) {
        const int64_t xx = x * x;

        // 222121 x^2 - y^2 + m = 0
        // 360721 x^2 - y^2 + n = 0

        const int64_t x1 = Mod(222121 * xx, p);
        const int64_t x2 = Mod(360721 * xx, p);

        for (int64_t y = 0; y < p; y++) {
          int64_t yy = y * y;

          int64_t r1 = Mod(x1 - yy, p);
          if (r1 != m) continue;

          int64_t r2 = Mod(x2 - yy, p);
          if (r2 != n) continue;

          // Otherwise, x,y is a solution.
          has_solution = true;
          // PERF: Some way to end such a parallel search
          // early!
          return;
        }
      }, 8);

  return has_solution;
}

static void DoWork() {
  printf("Startup..\n");

  Work work;
  // work.Load();

  Periodically save_per(60);
  Periodically status_per(5);

  ArcFour rc(StringPrintf("mod.%lld", time(nullptr)));
  Timer run_time;

  for (;;) {
    save_per.RunIf([&work]() {
        work.Save();
        printf(AWHITE("Saved") ".\n");
      });

    status_per.RunIf([&run_time]() {
        printf("Did " ABLUE("%llu") " primes and "
               "eliminated " AGREEN("%llu") " pairs in %s\n",
               tries.Read(), eliminated.Read(),
               ANSI::Time(run_time.Seconds()).c_str());
      });

    // We should probably be more systematic but I want
    // to start by getting some picture of the space.
    int m = RandTo(&rc, 667) - 333;
    int n = RandTo(&rc, 667) - 333;

    // XXX can actually reject outside the diamond |m|+|n|<333

    // Already ruled this one out.
    if (work.NoSolAt(m, n)) continue;

    // Otherwise, get old upper bound.
    uint32_t ub = work.PrimeAt(m, n);

    const uint32_t p = Factorization::NextPrime(ub + 1);
    CHECK(p != 0);

    printf("Try %d, %d, %d\n", m, n, p);

    // Now solve the simultaneous equations mod p.
    if (!HasSolutionModP(m, n, p)) {
      work.NoSolAt(m, n) = p;
      eliminated++;
    }
    // Either way, set the new upper bound.
    work.PrimeAt(m, n) = p;
    tries++;
  }

}

int main(int argc, char **argv) {
  printf("hi?\n");

  ANSI::Init();

  DoWork();

  return 0;
}
