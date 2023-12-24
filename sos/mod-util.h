
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

  static constexpr int WIDTH = 667;
  static constexpr int HEIGHT = 667;

  static constexpr int MINIMUM = -333;
  static constexpr int MAXIMUM = +333;

  // from (x,y) in [0, WIDTH) and [0, HEIGHT) to (m, n), centered on zero
  static inline std::pair<int, int> ToMN(int x, int y) {
    return std::make_pair(x - 333, y - 333);
  }

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

  static bool Exists() {
    return Util::ExistsFile("mod-nosol.png") &&
      Util::ExistsFile("mod-prime.png");
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

template<bool ALLOW_SMALL_PRIMES>
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
  bool HasSolutionModPInternal(int64_t m_orig, int64_t n_orig) const {
    int64_t m_int = m_orig;
    int64_t n_int = n_orig;
    if constexpr (ALLOW_SMALL_PRIMES) {
      m_int = m_int % (int64_t)p.modulus;
      n_int = n_int % (int64_t)p.modulus;
    }

    if (m_int < 0) m_int += (int64_t)p.modulus;
    if (n_int < 0) n_int += (int64_t)p.modulus;

    const Montgomery64 m = p.ToMontgomery(m_int);
    const Montgomery64 n = p.ToMontgomery(n_int);

    // Sub is a little faster, so pre-negate these.
    const Montgomery64 neg_m = p.Negate(m);
    const Montgomery64 neg_n = p.Negate(n);

    const int64_t limit = [&]() {
        if constexpr (NUM_TO_CHECK < 0) {
          return p.Modulus();
        } else {
          if constexpr (ALLOW_SMALL_PRIMES) {
            return std::min((uint64_t)NUM_TO_CHECK, (uint64_t)p.modulus);
          } else {
            return NUM_TO_CHECK;
          }
        }
      }();

    for (int64_t idx = 0; idx < limit; idx++) {
      // Get one of the residues. We don't care what order we
      // do them in; we just need to try them all.
      Montgomery64 a = p.Nth(idx);
      Montgomery64 aa = p.Mult(a, a);

      // 222121 a^2 - b^2 + m = 0
      // 360721 a^2 - c^2 + n = 0
      // 222121 a^2 + m = b^2
      // 360721 a^2 + n = c^2
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
        /*
        printf("For (%lld,%lld) mod %llu solution with a=%llu^2\n",
               m_orig, n_orig, p.modulus,
               p.ToInt(a));
        */
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

// Return all the quadratic residues mod m in an arbitrary order.
// Linear time.
inline std::vector<int64_t> SquaresModM(int64_t modulus) {
  std::unordered_set<int64_t> squares_mod_set;
  // PERF: don't need to check "negative" i.
  for (int64_t i = 0; i < modulus; i++) {
    squares_mod_set.insert((i * i) % modulus);
  }

  std::vector<int64_t> squares_mod;
  squares_mod.reserve(squares_mod_set.size());
  for (const int64_t s : squares_mod_set)
    squares_mod.push_back(s);
  return squares_mod;
}

// Exhaustively search for (a, b, c) that solve
// 222121 a^2 - b^2 + m = 0  mod p
// 360721 a^2 - c^2 + n = 0  mod p
// (p need not be prime).
// XXX to cc
inline
std::optional< std::tuple<int64_t, int64_t, int64_t>>
SimpleSolve(int64_t m_signed, int64_t n_signed, int64_t p) {
  auto ModP = [p](int64_t x) {
      x = x % p;
      if (x < 0) x += p;
      return x;
    };

  // Move these to the right hand side, which means negating them.
  // Convert into a non-negative remainder mod p.
  const int64_t neg_m = ModP(-m_signed);
  const int64_t neg_n = ModP(-n_signed);

  auto SolveOne = [p, &ModP](int64_t saa, int64_t d) ->
    std::optional<int64_t> {
      for (int64_t x = 0; x < p; x++) {
        int64_t xx = ModP(saa - x * x);
        if (xx == d) return {x};
      }
      return std::nullopt;
  };

  /*
  printf("Solving (%lld,%lld) mod %lld, which is (%lld,%lld)\n",
         m_signed, n_signed, p, m, n);
  */

  for (int64_t a = 0; a < p; a++) {
    const int64_t aa = (a * a) % p;

    if (a > 0 && a % 10000 == 0) printf("%lld/%lld\n", a, p);

    const int64_t aa1 = (222121LL * aa) % p;
    auto bo = SolveOne(aa1, neg_m);
    if (!bo.has_value()) continue;

    const int64_t aa2 = (360721LL * aa) % p;
    auto co = SolveOne(aa2, neg_n);
    if (!co.has_value()) continue;

    // Verify that it is indeed a solution.
    const int64_t b = bo.value();
    const int64_t c = co.value();

    // Verify that the solution does indeed work.
    CHECK(ModP(222121 * a * a - b * b + n_signed) == 0);
    CHECK(ModP(360721 * a * a - c * c + m_signed) == 0);

    return {std::make_tuple(a, bo.value(), co.value())};
  }
  /*
  CHECK(false) << "No solution for (" << m_signed << "," << n_signed << ") "
               << " mod " << p;
  */
  return std::nullopt;
}


#endif
