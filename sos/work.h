
// 666x666 grid for finding multiples.
// TODO: A better name!

#ifndef _WORK_H
#define _WORK_H

#include <unordered_set>
#include <cmath>
#include <iostream>
#include <vector>
#include <memory>
#include <cstdint>

#include "image.h"
#include "base/logging.h"

// #define ECHECK(s) CHECK(s)
#define ECHECK(s) if (0) std::cout << ""

// Since we already have a solution with overall error (|m| + |n|) 333,
// we only try candidates in [-333, 333].
struct Work {
  // This sentinel is used when Alpertron determines one or the other
  // equation is not solvable at all. Since it's composite, it can be
  // distguished from any valid entry (would be prime) for a modulus
  // found with mod.exe.
  static constexpr uint32_t NOSOL_ALPERTRON = 0xFF0000FF;
  // Eliminated because one of the equations has finite solutions,
  // and we can determine that the other cannot have a compatible
  // solution.
  static constexpr uint32_t NOSOL_ALPERTRON_FINITE = 0xFFFF00FF;

  // We store the whole rectangle (and have eliminated a lot of it) but
  // focus on the cells we actually care about, since they would improve
  // the best record:
  static inline bool Eligible(int m, int n) {
    return abs(m) + abs(n) < 333;
  }

  inline uint32_t &PrimeAt(int m, int n) {
    ECHECK(m >= -333 && m <= 333 &&
           n >= -333 && n <= 333) << m << "," << n;
    const int y = m + 333;
    const int x = n + 333;
    return prime[y * WIDTH + x];
  }

  void SetNoSolAt(int m, int n, uint32_t p) {
    ECHECK(m >= -333 && m <= 333 &&
           n >= -333 && n <= 333) << m << "," << n;
    const int y = m + 333;
    const int x = n + 333;
    if (nosol[y * WIDTH + x] == 0 && p != 0)
      remaining--;
    else if (nosol[y * WIDTH + x] != 0 && p == 0)
      remaining++;
    nosol[y * WIDTH + x] = p;
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

    // Get remaining rows, cols.
    std::unordered_set<int> rows, cols;
    for (int y = 0; y < HEIGHT; y++) {
      const int m = y - 333;
      for (int x = 0; x < WIDTH; x++) {
        const int n = x - 333;
        if (Eligible(m, n) && GetNoSolAt(m, n) == 0) {
          rows.insert(m);
          cols.insert(n);
        }
      }
    }

    for (int y = 0; y < HEIGHT; y++) {
      const int m = y - 333;

      const bool row_remains = rows.contains(m);

      for (int x = 0; x < WIDTH; x++) {
        const int n = x - 333;

        const bool col_remains = cols.contains(n);

        prime_image.SetPixel32(x, y, PrimeAt(m, n));
        uint32_t ns = GetNoSolAt(m, n);
        nosol_image.SetPixel32(x, y, ns);

        if (ns == 0) {
          eliminated.SetPixel32(x, y, 0x000000FF);
        } else {
          uint8_t r = m < 0 ? 0xFF : 0x7F;
          uint8_t g = n < 0 ? 0x7F : 0xFF;

          int s = Eligible(m, n) ? 0 : 1;

          uint8_t b = 0;
          if (!row_remains || !col_remains) {
            b = 0x8f;
            s++;
          }

          eliminated.SetPixel(x, y, r >> s, g >> s, b, 0xFF);
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

#endif
