
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

#include "base/stringprintf.h"

DECLARE_COUNTERS(tries, eliminated,
                 u3_, u4_, u5_, u6_, u7_, u8_);

// #define ECHECK(s) CHECK(s)
#define ECHECK(s) if (0) std::cout << ""

// Since we already have a solution with overall error (|m| + |n|) 333,
// we only try candidates in [-333, 333].
struct Work {

  inline uint32_t &PrimeAt(int m, int n) {
    ECHECK(m >= -333 && m <= 333 &&
           n >= -333 && n <= 333) << m << "," << n;
    const int y = m + 333;
    const int x = n + 333;
    return prime[y * WIDTH + x];
  }

  void SetNoSolAt(int m, int n, uint32_t count) {
    ECHECK(m >= -333 && m <= 333 &&
           n >= -333 && n <= 333) << m << "," << n;
    const int y = m + 333;
    const int x = n + 333;
    if (nosol[y * WIDTH + x] == 0 && count != 0)
      remaining--;
    else if (nosol[y * WIDTH + x] != 0 && count == 0)
      remaining++;
    nosol[y * WIDTH + x] = count;
  }

  uint32_t GetNoSolAt(int m, int n) const {
    ECHECK(m >= -333 && m <= 333 &&
           n >= -333 && n <= 333) << m << "," << n;
    const int y = m + 333;
    const int x = n + 333;
    return nosol[y * WIDTH + x];
  }

  Work() {
    prime.resize(WIDTH * HEIGHT);
    nosol.resize(WIDTH * HEIGHT);
    for (int y = 0; y < HEIGHT; y++) {
      const int m = y - 333;
      for (int x = 0; x < WIDTH; x++) {
        const int n = x - 333;
        PrimeAt(m, n) = 0;
        SetNoSolAt(m, n, 0);
      }
    }
    remaining = WIDTH * HEIGHT;
  }

  void Save() {
    ImageRGBA prime_image(WIDTH, HEIGHT);
    ImageRGBA nosol_image(WIDTH, HEIGHT);
    ImageRGBA eliminated(WIDTH, HEIGHT);
    for (int y = 0; y < HEIGHT; y++) {
      const int m = y - 333;
      for (int x = 0; x < WIDTH; x++) {
        const int n = x - 333;

        prime_image.SetPixel32(x, y, PrimeAt(m, n));
        uint32_t ns = GetNoSolAt(m, n);
        nosol_image.SetPixel32(x, y, ns);

        if (ns == 0) {
          eliminated.SetPixel32(x, y, 0x000000FF);
        } else {
          uint8_t r = m < 0 ? 0xFF : 0x7F;
          uint8_t g = n < 0 ? 0x7F : 0xFF;

          eliminated.SetPixel(x, y, r, g, 0x00, 0xFF);
        }
      }
    }

    prime_image.Save("mod-prime.png");
    nosol_image.Save("mod-nosol.png");
    eliminated.ScaleBy(2).Save("mod-eliminated.png");
  }

  void Load() {
    std::unique_ptr<ImageRGBA> prime_image(
        ImageRGBA::Load("mod-prime.png"));

    std::unique_ptr<ImageRGBA> nosol_image(
        ImageRGBA::Load("mod-nosol.png"));

    if (prime_image.get() == nullptr ||
        nosol_image.get() == nullptr) {
      printf("Note: Couldn't load work from disk.\n");
      return;
    }

    for (int y = 0; y < HEIGHT; y++) {
      const int m = y - 333;
      for (int x = 0; x < WIDTH; x++) {
        const int n = x - 333;

        PrimeAt(m, n) = prime_image->GetPixel32(x, y);
        SetNoSolAt(m, n, nosol_image->GetPixel32(x, y));
      }
    }
  }

  int Remaining() const { return remaining; }

private:
  static constexpr int WIDTH = 667;
  static constexpr int HEIGHT = 667;

  int remaining = 0;

  // Both arrays are size WIDTH * HEIGHT.
  // The largest prime that we've tried (or zero).
  std::vector<uint32_t> prime;
  // If nonzero, then we checked that there is no solution mod this
  // prime. This means there's no solution on integers.
  std::vector<uint32_t> nosol;
};

inline uint32_t Mod(int64_t a, uint32_t b) {
  int64_t r = a % b;
  if (r < 0) r += b;
  return r;
}

// XXX PERF: We can of course do much better than just trying
// all a, b, c!!
//
// PERF: With the enumeration approach, another thing we can do
// is try all the values for (a, b, c) in Zp and record as solvable
// the -m and -n that we do hit.
static bool HasSolutionModP(int m, int n, uint32_t p) {

  m = Mod(m, p);
  n = Mod(n, p);

  for (int64_t a = 0; a < p; a++) {
    const int64_t aa = a * a;

    // PERF: I think there is an efficient way to compute
    // square roots mod p. So we could do like 222121a^2 + m = b^2.
    //
    // 222121 a^2 - b^2 + m = 0
    // 360721 a^2 - c^2 + n = 0

    const int64_t a1m = Mod(222121 * aa + m, p);
    const int64_t a2n = Mod(360721 * aa + n, p);

    // So we have e.g. a1m = 222121 a^2 + m
    // and we want to know if this is a square (mod p).
    // But we check both equations at the same time to
    // save the loop overhead (mainly the Mod).
    auto AreSquaresModP = [p](int64_t bm, int64_t cn) {
        // two-bit bitmask
        uint8_t squares = 0b00;
        for (int64_t y = 0; y < p; y++) {
          int64_t yy = y * y;
          int64_t r = Mod(yy, p);

          squares = squares |
            ((r == bm) << 1) |
            (r == cn);

          // Or compare doing this outside the loop.
          if (squares == 0b11) return true;
        }
        return false;
      };

    if (AreSquaresModP(a1m, a2n)) {
      return true;
    }
  }

  return false;
}

static void DoWork() {
  printf("Startup..\n");

  Work work;
  work.Load();

  Periodically save_per(60);
  Periodically status_per(5);

  ArcFour rc(StringPrintf("mod.%lld", time(nullptr)));
  Timer run_time;

  std::vector<std::pair<int, int>> todo;

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

        printf("Did " ABLUE("%llu") ". "
               "Elim " AGREEN("%llu") " in %s. "
               "Recent: " APURPLE("%d") " "
               "Remain: " ACYAN("%d") "\n",
               tries.Read(), eliminated.Read(),
               ANSI::Time(run_time.Seconds()).c_str(),
               (int)recent_min.value_or(0),
               work.Remaining());
      });

    std::vector<std::pair<int, int>> res =
      ParallelMap(todo, [&work](const std::pair<int, int> &mn) ->
      std::pair<int, int> {
          const auto &[m, n] = mn;
          // Already ruled this one out.
          if (work.GetNoSolAt(m, n)) return {-1, -1};

          // Otherwise, get old upper bound.
          const uint32_t ub = work.PrimeAt(m, n);

          const uint32_t p = Factorization::NextPrime(ub + 1);

          // Now solve the simultaneous equations mod p.
          if (!HasSolutionModP(m, n, p)) {
            eliminated++;
            return {p, p};
          }

          tries++;
          return {-1, p};
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

  DoWork();

  return 0;
}
