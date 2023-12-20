
// Utilities for mod/solve.

#ifndef _MOD_UTIL_H
#define _MOD_UTIL_H

#include <unordered_set>
#include <cmath>
#include <iostream>
#include <vector>
#include <memory>
#include <cstdint>

#include "image.h"
#include "montgomery64.h"
#include "base/logging.h"

// #define ECHECK(s) CHECK(s)
#define ECHECK(s) if (0) std::cout << ""

// TODO: A better name!
// Since we already have a solution with overall error (|m| + |n|) 333,
// we only try candidates in [-333, 333].
struct Work {
  // This sentinel is used when Alpertron determines one or the other
  // equation is not solvable at all. Since it's composite, it can be
  // distguished from any valid entry (would be prime) for a modulus
  // found with mod.exe.
  static constexpr uint64_t NOSOL_ALPERTRON = 0x00000000'FF0000FF;
  // Eliminated because one of the equations has finite solutions,
  // and we can determine that the other cannot have a compatible
  // solution.
  static constexpr uint64_t NOSOL_ALPERTRON_FINITE = 0x00000000'FFFF00FF;

  // We store the whole rectangle (and have eliminated a lot of it) but
  // focus on the cells we actually care about, since they would improve
  // the best record:
  static inline bool Eligible(int m, int n) {
    return abs(m) + abs(n) < 333;
  }

  inline uint64_t &PrimeAt(int m, int n) {
    ECHECK(m >= -333 && m <= 333 &&
           n >= -333 && n <= 333) << m << "," << n;
    const int y = m + 333;
    const int x = n + 333;
    return prime[y * WIDTH + x];
  }

  void SetNoSolAt(int m, int n, uint64_t p) {
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

  uint64_t GetNoSolAt(int m, int n) const {
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
    // 64-bit, so using two pixels per cell.
    ImageRGBA prime_image(WIDTH * 2, HEIGHT);
    ImageRGBA nosol_image(WIDTH * 2, HEIGHT);
    // This is just for looking at, so it is square.
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

        const auto [ph, pl] = Unpack64(PrimeAt(m, n));
        prime_image.SetPixel32(x * 2 + 0, y, ph);
        prime_image.SetPixel32(x * 2 + 1, y, pl);
        uint64_t ns = GetNoSolAt(m, n);
        const auto [nh, nl] = Unpack64(ns);
        nosol_image.SetPixel32(x * 2 + 0, y, nh);
        nosol_image.SetPixel32(x * 2 + 1, y, nl);

        if (ns == 0) {
          eliminated.SetPixel32(x, y, 0x000000FF);
        } else {
          uint8_t r = m < 0 ? 0xFF : 0x7F;
          uint8_t g = n < 0 ? 0x7F : 0xFF;

          int s = Eligible(m, n) ? 0 : 1;

          uint8_t b = 0x6f;
          if (!row_remains || !col_remains) {
            b = 0x8f;
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

    CHECK(prime_image->Height() == HEIGHT);
    CHECK(nosol_image->Height() == HEIGHT);

    CHECK(prime_image->Width() == nosol_image->Width());

    // Perhaps load old format.
    if (prime_image->Width() == WIDTH) {
      prime_image = Widen(*prime_image);
      nosol_image = Widen(*nosol_image);
    }

    CHECK(prime_image->Width() == WIDTH * 2);
    CHECK(nosol_image->Width() == WIDTH * 2);

    for (int y = 0; y < HEIGHT; y++) {
      const int m = y - 333;
      for (int x = 0; x < WIDTH; x++) {
        const int n = x - 333;

        PrimeAt(m, n) = Pack64(prime_image->GetPixel32(x * 2 + 0, y),
                               prime_image->GetPixel32(x * 2 + 1, y));

        SetNoSolAt(m, n, Pack64(nosol_image->GetPixel32(x * 2 + 0, y),
                                nosol_image->GetPixel32(x * 2 + 1, y)));
      }
    }
  }

  int Remaining() const { return remaining; }

private:
  static constexpr int WIDTH = 667;
  static constexpr int HEIGHT = 667;

  std::unique_ptr<ImageRGBA> Widen(const ImageRGBA &img) {
    std::unique_ptr<ImageRGBA> wide =
      std::make_unique<ImageRGBA>(WIDTH * 2, HEIGHT);
    // Zeroes for high word.
    wide->Clear32(0x00000000);

    for (int y = 0; y < HEIGHT; y++) {
      for (int x = 0; x < WIDTH; x++) {
        wide->SetPixel32(x * 2 + 1, y, img.GetPixel32(x, y));
      }
    }
    return wide;
  }

  static inline std::pair<uint32_t, uint32_t> Unpack64(uint64_t w) {
    uint32_t h = w >> 32;
    uint32_t l = w;
    return std::make_pair(h, l);
  }

  static inline uint64_t Pack64(uint32_t h, uint32_t l) {
    return (uint64_t(h) << 32) | uint64_t(l);
  }

  int remaining = 0;

  // Both arrays are size WIDTH * HEIGHT.
  // The largest prime that we've tried (or zero).
  std::vector<uint64_t> prime;
  // If nonzero, then we checked that there is no solution mod this
  // prime. This means there's no solution on integers.
  std::vector<uint64_t> nosol;
};

struct SolutionFinder {
  const MontgomeryRep64 p;
  Montgomery64 coeff_1;
  Montgomery64 coeff_2;

  explicit SolutionFinder(uint64_t p_int) : p(p_int) {
    coeff_1 = p.ToMontgomery(222121);
    coeff_2 = p.ToMontgomery(360721);
  }

  bool HasSolutionModP(int64_t m_int, int64_t n_int) const {
    return HasSolutionModPInternal<-1>(m_int, n_int);
  }

  template<int NUM>
  bool QuickHasSolutionModP(int64_t m_int, int64_t n_int) const {
    static_assert(NUM > 0);
    return HasSolutionModPInternal<NUM>(m_int, n_int);
  }

private:
  // For large p, we find a solution very quickly; the depth
  // histogram looks like exponential falloff.
  template<int NUM_TO_CHECK>
  bool HasSolutionModPInternal(int64_t m_int, int64_t n_int) const {
    if (m_int < 0) m_int += p.modulus;
    if (n_int < 0) n_int += p.modulus;

    const Montgomery64 m = p.ToMontgomery(m_int);
    const Montgomery64 n = p.ToMontgomery(n_int);

    // Sub is a little faster, so pre-negate these.
    const Montgomery64 neg_m = p.Negate(m);
    const Montgomery64 neg_n = p.Negate(n);

    const int64_t limit = [&]() {
        if constexpr (NUM_TO_CHECK < 0) {
          return p.Modulus();
        } else {
          return NUM_TO_CHECK;
        }
      }();

    for (int64_t idx = 0; idx < limit; idx++) {
      // Get one of the residues. We don't care what order we
      // do them in; we just need to try them all.
      Montgomery64 a = p.Nth(idx);
      Montgomery64 aa = p.Mult(a, a);

      // 222121 a^2 - (-m) = b^2
      // 360721 a^2 - (-n) = c^2

      Montgomery64 a1m = p.Sub(p.Mult(coeff_1, aa), neg_m);
      Montgomery64 a2n = p.Sub(p.Mult(coeff_2, aa), neg_n);

      // Compute Euler criteria. a^((p-1) / 2) must be 1.
      // Since p is odd, we can just shift down by one
      // to compute (p - 1)/2.
      const auto &[r1, r2] = p.Pows(std::array<Montgomery64, 2>{a1m, a2n},
                                    p.Modulus() >> 1);

      bool sol1 = p.Eq(r1, p.One()) || p.Eq(a1m, p.Zero());
      bool sol2 = p.Eq(r2, p.One()) || p.Eq(a2n, p.Zero());

      if (sol1 && sol2) {
        #if DEPTH_HISTO
        MutexLock ml(&histo_mutex);
        depth_histo->Observe(idx);
        #endif
        return true;
      }
    }

    return false;
  }

};

#endif
