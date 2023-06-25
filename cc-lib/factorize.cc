
#include "factorize.h"

#include <cstdlib>
#include <array>
#include <cstdint>
#include <vector>
#include <utility>
#include <cmath>
#include <cassert>

#include "base/logging.h"

using namespace std;

static uint64_t Sqrt64(uint64_t n) {
  if (n == 0) return 0;
  uint64_t r = std::sqrt((double)n);
  return r - (r * r - 1 >= n);
}

// PERF: Gaps between primes are <= 250 all the
// way up to 387096383. So we could have a lot more here.
// XXX coreutils has 5000 primes, and might depend on that for the
// correctness of the Lucas primality test?
static constexpr array<uint8_t, 999> PRIME_DELTAS = {
1,2,2,4,2,4,2,4,6,2,6,4,2,4,6,6,2,6,4,2,6,4,6,8,4,2,4,2,4,14,4,6,
2,10,2,6,6,4,6,6,2,10,2,4,2,12,12,4,2,4,6,2,10,6,6,6,2,6,4,2,10,14,4,2,
4,14,6,10,2,4,6,8,6,6,4,6,8,4,8,10,2,10,2,6,4,6,8,4,2,4,12,8,4,8,4,6,
12,2,18,6,10,6,6,2,6,10,6,6,2,6,6,4,2,12,10,2,4,6,6,2,12,4,6,8,10,8,10,8,
6,6,4,8,6,4,8,4,14,10,12,2,10,2,4,2,10,14,4,2,4,14,4,2,4,20,4,8,10,8,4,6,
6,14,4,6,6,8,6,12,4,6,2,10,2,6,10,2,10,2,6,18,4,2,4,6,6,8,6,6,22,2,10,8,
10,6,6,8,12,4,6,6,2,6,12,10,18,2,4,6,2,6,4,2,4,12,2,6,34,6,6,8,18,10,14,4,
2,4,6,8,4,2,6,12,10,2,4,2,4,6,12,12,8,12,6,4,6,8,4,8,4,14,4,6,2,4,6,2,
6,10,20,6,4,2,24,4,2,10,12,2,10,8,6,6,6,18,6,4,2,12,10,12,8,16,14,6,4,2,4,2,
10,12,6,6,18,2,16,2,22,6,8,6,4,2,4,8,6,10,2,10,14,10,6,12,2,4,2,10,12,2,16,2,
6,4,2,10,8,18,24,4,6,8,16,2,4,8,16,2,4,8,6,6,4,12,2,22,6,2,6,4,6,14,6,4,
2,6,4,6,12,6,6,14,4,6,12,8,6,4,26,18,10,8,4,6,2,6,22,12,2,16,8,4,12,14,10,2,
4,8,6,6,4,2,4,6,8,4,2,6,10,2,10,8,4,14,10,12,2,6,4,2,16,14,4,6,8,6,4,18,
8,10,6,6,8,10,12,14,4,6,6,2,28,2,10,8,4,14,4,8,12,6,12,4,6,20,10,2,16,26,4,2,
12,6,4,12,6,8,4,8,22,2,4,2,12,28,2,6,6,6,4,6,2,12,4,12,2,10,2,16,2,16,6,20,
16,8,4,2,4,2,22,8,12,6,10,2,4,6,2,6,10,2,12,10,2,10,14,6,4,6,8,6,6,16,12,2,
4,14,6,4,8,10,8,6,6,22,6,2,10,14,4,6,18,2,10,14,4,2,10,14,4,8,18,4,6,2,4,6,
2,12,4,20,22,12,2,4,6,6,2,6,22,2,6,16,6,12,2,6,12,16,2,4,6,14,4,2,18,24,10,6,
2,10,2,10,2,10,6,2,10,2,10,6,8,30,10,2,10,8,6,10,18,6,12,12,2,18,6,4,6,6,18,2,
10,14,6,4,2,4,24,2,12,6,16,8,6,6,18,16,2,4,6,2,6,6,10,6,12,12,18,2,6,4,18,8,
24,4,2,4,6,2,12,4,14,30,10,6,12,14,6,10,12,2,4,6,8,6,10,2,4,14,6,6,4,6,2,10,
2,16,12,8,18,4,6,12,2,6,6,6,28,6,14,4,8,10,8,12,18,4,2,4,24,12,6,2,16,6,6,14,
10,14,4,30,6,6,6,8,6,4,2,12,6,4,2,6,22,6,2,4,18,2,4,12,2,6,4,26,6,6,4,8,
10,32,16,2,6,4,2,4,2,10,14,6,4,8,10,6,20,4,2,6,30,4,8,10,6,6,8,6,12,4,6,2,
6,4,6,2,10,2,16,6,20,4,12,14,28,6,20,4,18,8,6,4,6,14,6,6,10,2,10,12,8,10,2,10,
8,12,10,24,2,4,8,6,4,8,18,10,6,6,2,6,10,12,2,10,6,6,6,8,6,10,6,2,6,6,6,10,
8,24,6,22,2,18,4,8,10,30,8,18,4,2,10,6,2,6,4,18,8,12,18,16,6,2,12,6,10,2,10,2,
6,10,14,4,24,2,16,2,10,2,10,20,4,2,4,8,16,6,6,2,12,16,8,4,6,30,2,10,2,6,4,6,
6,8,6,4,12,6,8,12,4,14,12,10,24,6,12,6,2,22,8,18,10,6,14,4,2,6,10,8,6,4,6,30,
14,10,2,12,10,2,16,2,18,24,18,6,16,18,6,2,18,4,6,2,10,8,10,6,6,8,4,6,2,10,2,12,
4,6,6,2,12,4,14,18,4,6,20,4,8,6,4,8,4,14,6,4,14,12,4,2,30,4,24,6,6,12,12,14,
6,4,2,4,18,6,12,
};

static constexpr int ComputeDeltas() {
  int sum = 2;
  for (uint8_t d : PRIME_DELTAS) sum += d;
  return sum;
}
// (XXX or is this the last prime in the list..?)
static constexpr int FIRST_OMITTED_PRIME = ComputeDeltas();

static void
factor_using_pollard_rho (uint64_t n, unsigned long int a,
                          std::vector<std::pair<uint64_t, int>> *factors);
static bool prime_p (uint64_t n);

// Add the factor, or increment its exponent if it is the
// one already at the end. Normally we are inserting in ascending
// order so this keeps the list compact. But NormalizeFactors below
// can also fix it up they are added out-of-order (Pollard-Rho).
auto PushFactor = [](std::vector<std::pair<uint64_t, int>> *factors,
                     uint64_t b) {
    if (!factors->empty() &&
        factors->back().first == b) {
      factors->back().second++;
    } else {
      factors->emplace_back(b, 1);
    }
  };

static void NormalizeFactors(std::vector<std::pair<uint64_t, int>> *factors) {
  if (factors->empty()) return;
  // Sort by base.
  std::sort(factors->begin(), factors->end(),
            [](const auto &a, const auto &b) {
              return a.first < b.first;
            });

  // Now dedupe by summing.
  std::vector<std::pair<uint64_t, int>> out;
  out.reserve(factors->size());
  for (const auto &[p, e] : *factors) {
    if (!out.empty() &&
        out.back().first == p) {
      out.back().second += e;
    } else {
      out.emplace_back(p, e);
    }
  }
  *factors = std::move(out);
}

static std::vector<std::pair<uint64_t, int>>
InternalFactorize(uint64_t x, bool use_pr) {
  // TODO: Allow as argument
  uint64_t max_factor = x;

  // Simple trial division.
  // It would not be hard to incorporate Fermat's method too,
  // for cases that the number has factors close to its square
  // root too (this may be common?).

  // Factors in increasing order.
  std::vector<std::pair<uint64_t, int>> factors;

  if (x <= 1) return {};

  // Current value being factored; as we find factors, we divide
  // this target.
  uint64_t cur = x;
  // First, using the prime list.
  uint32_t prime = 2;
  for (int delta_idx = 0; delta_idx < (int)PRIME_DELTAS.size(); /* in loop */) {
    if (prime > max_factor)
      break;

    uint64_t rem = cur % prime;
    if (rem == 0) {
      cur = cur / prime;
      if (max_factor > cur)
        max_factor = cur;
      PushFactor(&factors, prime);
      // Common for the number to just have small factors.
      // XXX actually, if it's <= prime we are done, right?
      if (cur == 1) return factors;
      // But don't get next prime, as it may appear as a
      // factor many times.
    } else {
      prime += PRIME_DELTAS[delta_idx++];
    }
  }

  if (use_pr) {
    if (prime_p(cur)) {
      PushFactor(&factors, cur);
    } else {
      factor_using_pollard_rho (cur, 1, &factors);
      // Pollard-rho does not generate factors in ascending order.
      NormalizeFactors(&factors);
    }
    return factors;
  }

  // Otherwise, use the much slower trial division algorithm.

  // Once we exhausted the prime list, do the same
  // but with odd numbers up to the square root. Note
  // that the last prime is not actually tested above; it's
  // the initial value for this loop.
  uint64_t divisor = prime;
  uint64_t sqrtcur = Sqrt64(cur) + 1;
  for (;;) {
    // skip if a multiple of 3.
    if (divisor > max_factor)
      break;

    if (divisor > sqrtcur)
      break;

    const uint64_t rem = cur % divisor;
    if (rem == 0) {
      cur = cur / divisor;
      sqrtcur = Sqrt64(cur) + 1;
      PushFactor(&factors, divisor);
      // But don't increment i, as it may appear as a
      // factor many times.
    } else {
      do {
        // Always skip even numbers; this is trivial.
        divisor += 2;
        // But also skip divisors with small factors. Modding by
        // small constants can be compiled into multiplication
        // tricks, so this is faster than doing another proper
        // loop.
      } while ((divisor % 3) == 0 ||
               (divisor % 5) == 0 ||
               (divisor % 7) == 0 ||
               (divisor % 11) == 0 ||
               (divisor % 13) == 0 ||
               (divisor % 17) == 0);
    }
  }

  // And the number itself, which we now know is prime
  // (unless we reached the max factor).
  if (cur != 1) {
    PushFactor(&factors, cur);
  }

  return factors;
}

/* This code derives from the GPL factor.c, which is part of GNU
   coreutils. Copyright (C) 1986-2023 Free Software Foundation, Inc.

   (This whole file is distributed under the terms of the GPL; see
   COPYING.)

   Contributors: Paul Rubin, Jim Meyering, James Youngman, Torbjörn
   Granlund, Niels Möller.
*/

// Assumptions from ported code.
static_assert(sizeof(uintmax_t) == sizeof(uint64_t));
static constexpr int W_TYPE_SIZE = 8 * sizeof (uint64_t);
static_assert(W_TYPE_SIZE == 64);

// TODO: continue uintmax_t -> uint64_t.
// XXX macros to templates etc.

#define sub_ddmmss(rh, rl, ah, al, bh, bl)                             \
  do {                                                                  \
    uintmax_t _cy;                                                      \
    _cy = (al) < (bl);                                                  \
    (rl) = (al) - (bl);                                                 \
    (rh) = (ah) - (bh) - _cy;                                           \
  } while (0)

#define rsh2(rh, rl, ah, al, cnt)                                       \
  do {                                                                  \
    (rl) = ((ah) << (W_TYPE_SIZE - (cnt))) | ((al) >> (cnt));           \
    (rh) = (ah) >> (cnt);                                               \
  } while (0)

#define ge2(ah, al, bh, bl)                                             \
  ((ah) > (bh) || ((ah) == (bh) && (al) >= (bl)))

# define udiv_qrnnd(q, r, n1, n0, d)                                    \
  do {                                                                  \
    uintmax_t __d1, __d0, __q, __r1, __r0;                              \
                                                                        \
    assert ((n1) < (d));                                                \
    __d1 = (d); __d0 = 0;                                               \
    __r1 = (n1); __r0 = (n0);                                           \
    __q = 0;                                                            \
    for (unsigned int __i = W_TYPE_SIZE; __i > 0; __i--)                \
      {                                                                 \
        rsh2 (__d1, __d0, __d1, __d0, 1);                               \
        __q <<= 1;                                                      \
        if (ge2 (__r1, __r0, __d1, __d0))                               \
          {                                                             \
            __q++;                                                      \
            sub_ddmmss (__r1, __r0, __r1, __r0, __d1, __d0);            \
          }                                                             \
      }                                                                 \
    (r) = __r0;                                                         \
    (q) = __q;                                                          \
  } while (0)


/* x B (mod n).  */
#define redcify(r_prim, r, n)                                           \
  do {                                                                  \
    [[maybe_unused]] uintmax_t _redcify_q;          \
    udiv_qrnnd (_redcify_q, r_prim, r, 0, n);                           \
  } while (0)

/* Requires that a < n and b <= n */
#define submod(r,a,b,n)                                                 \
  do {                                                                  \
    uintmax_t _t = - (uintmax_t) (a < b);                               \
    (r) = ((n) & _t) + (a) - (b);                                       \
  } while (0)

#define addmod(r,a,b,n)                                                 \
  submod ((r), (a), ((n) - (b)), (n))

# define __ll_B ((uintmax_t) 1 << (W_TYPE_SIZE / 2))
# define __ll_lowpart(t)  ((uintmax_t) (t) & (__ll_B - 1))
# define __ll_highpart(t) ((uintmax_t) (t) >> (W_TYPE_SIZE / 2))

# define umul_ppmm(w1, w0, u, v)                                        \
  do {                                                                  \
    uintmax_t __x0, __x1, __x2, __x3;                                   \
    unsigned long int __ul, __vl, __uh, __vh;                           \
    uintmax_t __u = (u), __v = (v);                                     \
                                                                        \
    __ul = __ll_lowpart (__u);                                          \
    __uh = __ll_highpart (__u);                                         \
    __vl = __ll_lowpart (__v);                                          \
    __vh = __ll_highpart (__v);                                         \
                                                                        \
    __x0 = (uintmax_t) __ul * __vl;                                     \
    __x1 = (uintmax_t) __ul * __vh;                                     \
    __x2 = (uintmax_t) __uh * __vl;                                     \
    __x3 = (uintmax_t) __uh * __vh;                                     \
                                                                        \
    __x1 += __ll_highpart (__x0);/* This can't give carry.  */    \
    __x1 += __x2;   /* But this indeed can.  */   \
    if (__x1 < __x2)    /* Did we get it?  */     \
      __x3 += __ll_B;   /* Yes, add it in the proper pos.  */ \
                                                                        \
    (w1) = __x3 + __ll_highpart (__x1);                                 \
    (w0) = (__x1 << W_TYPE_SIZE / 2) + __ll_lowpart (__x0);             \
  } while (0)


/* Entry i contains (2i+1)^(-1) mod 2^8.  */
static constexpr unsigned char binvert_table[128] =
{
  0x01, 0xAB, 0xCD, 0xB7, 0x39, 0xA3, 0xC5, 0xEF,
  0xF1, 0x1B, 0x3D, 0xA7, 0x29, 0x13, 0x35, 0xDF,
  0xE1, 0x8B, 0xAD, 0x97, 0x19, 0x83, 0xA5, 0xCF,
  0xD1, 0xFB, 0x1D, 0x87, 0x09, 0xF3, 0x15, 0xBF,
  0xC1, 0x6B, 0x8D, 0x77, 0xF9, 0x63, 0x85, 0xAF,
  0xB1, 0xDB, 0xFD, 0x67, 0xE9, 0xD3, 0xF5, 0x9F,
  0xA1, 0x4B, 0x6D, 0x57, 0xD9, 0x43, 0x65, 0x8F,
  0x91, 0xBB, 0xDD, 0x47, 0xC9, 0xB3, 0xD5, 0x7F,
  0x81, 0x2B, 0x4D, 0x37, 0xB9, 0x23, 0x45, 0x6F,
  0x71, 0x9B, 0xBD, 0x27, 0xA9, 0x93, 0xB5, 0x5F,
  0x61, 0x0B, 0x2D, 0x17, 0x99, 0x03, 0x25, 0x4F,
  0x51, 0x7B, 0x9D, 0x07, 0x89, 0x73, 0x95, 0x3F,
  0x41, 0xEB, 0x0D, 0xF7, 0x79, 0xE3, 0x05, 0x2F,
  0x31, 0x5B, 0x7D, 0xE7, 0x69, 0x53, 0x75, 0x1F,
  0x21, 0xCB, 0xED, 0xD7, 0x59, 0xC3, 0xE5, 0x0F,
  0x11, 0x3B, 0x5D, 0xC7, 0x49, 0x33, 0x55, 0xFF
};


#define binv(inv,n)                                                     \
  do {                                                                  \
    uintmax_t  __n = (n);                                               \
    uintmax_t  __inv;                                                   \
                                                                        \
    __inv = binvert_table[(__n / 2) & 0x7F]; /*  8 */                   \
    if (W_TYPE_SIZE > 8)   __inv = 2 * __inv - __inv * __inv * __n;     \
    if (W_TYPE_SIZE > 16)  __inv = 2 * __inv - __inv * __inv * __n;     \
    if (W_TYPE_SIZE > 32)  __inv = 2 * __inv - __inv * __inv * __n;     \
                                                                        \
    if (W_TYPE_SIZE > 64)                                               \
      {                                                                 \
        int  __invbits = 64;                                            \
        do {                                                            \
          __inv = 2 * __inv - __inv * __inv * __n;                      \
          __invbits *= 2;                                               \
        } while (__invbits < W_TYPE_SIZE);                              \
      }                                                                 \
                                                                        \
    (inv) = __inv;                                                      \
  } while (0)

/* Modular two-word multiplication, r = a * b mod m, with mi = m^(-1) mod B.
   Both a and b must be in redc form, the result will be in redc form too.  */
static inline uint64_t
mulredc (uint64_t a, uint64_t b, uint64_t m, uint64_t mi)
{
  uint64_t rh, rl, q, th, xh;
  [[maybe_unused]] uint64_t tl;

  umul_ppmm (rh, rl, a, b);
  q = rl * mi;
  umul_ppmm (th, tl, q, m);
  xh = rh - th;
  if (rh < th)
    xh += m;

  return xh;
}

static uint64_t
powm (uint64_t b, uint64_t e, uint64_t n, uint64_t ni, uint64_t one) {
  uint64_t y = one;

  if (e & 1)
    y = b;

  while (e != 0) {
    b = mulredc (b, b, n, ni);
    e >>= 1;

    if (e & 1)
      y = mulredc (y, b, n, ni);
  }

  return y;
}

#define HIGHBIT_TO_MASK(x)                                              \
  (((intmax_t)-1 >> 1) < 0                                              \
   ? (uintmax_t)((intmax_t)(x) >> (W_TYPE_SIZE - 1))                    \
   : ((x) & ((uintmax_t) 1 << (W_TYPE_SIZE - 1))                        \
      ? UINTMAX_MAX : (uintmax_t) 0))

static uint64_t
gcd_odd (uint64_t a, uint64_t b) {
  if ((b & 1) == 0) {
    uint64_t t = b;
    b = a;
    a = t;
  }
  if (a == 0)
    return b;

  /* Take out least significant one bit, to make room for sign */
  b >>= 1;

  for (;;) {
    uint64_t t;
    uint64_t bgta;

    while ((a & 1) == 0)
      a >>= 1;
    a >>= 1;

    t = a - b;
    if (t == 0)
      return (a << 1) + 1;

    bgta = HIGHBIT_TO_MASK (t);

    /* b <-- min (a, b) */
    b += (bgta & t);

    /* a <-- |a - b| */
    a = (t ^ bgta) - bgta;
  }
}

static bool
millerrabin (uintmax_t n, uintmax_t ni, uintmax_t b, uintmax_t q,
             unsigned int k, uintmax_t one) {
  uintmax_t y = powm (b, q, n, ni, one);

  /* -1, but in redc representation.  */
  uintmax_t nm1 = n - one;

  if (y == one || y == nm1)
    return true;

  for (unsigned int i = 1; i < k; i++) {
    y = mulredc (y, y, n, ni);

    if (y == nm1)
      return true;
    if (y == one)
      return false;
  }
  return false;
}

/* Lucas's prime test. The number of iterations vary greatly; up to a
   few dozen have been observed. The average seem to be about 2. */
static bool prime_p (uint64_t n) {
  // printf("prime_p(%llu)?\n", n);
  int k;
  uint64_t a_prim, one, ni;

  if (n <= 1)
    return false;

  /* We have already sieved out small primes.  */
  if (n < (uint64_t) FIRST_OMITTED_PRIME * FIRST_OMITTED_PRIME)
    return true;

  /* Precomputation for Miller-Rabin.  */
  uint64_t q = n - 1;
  for (k = 0; (q & 1) == 0; k++)
    q >>= 1;

  binv (ni, n);                 /* ni <- 1/n mod B */
  redcify (one, 1, n);
  addmod (a_prim, one, one, n); /* i.e., redcify a = 2 */

  /* Perform a Miller-Rabin test, which finds most composites quickly.  */
  if (!millerrabin (n, ni, a_prim, q, k, one))
    return false;

  std::vector<std::pair<uint64_t, int>> factors =
    /* Factor n-1 for Lucas.  */
    Factorize::PrimeFactorization(n - 1);

  /* Loop until Lucas proves our number prime, or Miller-Rabin proves our
     number composite.  */
  uint64_t a = 2;
  for (uint8_t delta : PRIME_DELTAS) {
    bool is_prime = true;
    for (const auto &[p, e_] : factors) {
      is_prime = powm (a_prim, (n - 1) / p, n, ni, one) != one;
      if (!is_prime) break;
    }

    if (is_prime)
      return true;

    // Establish new base.
    a += delta;

    /* The following is equivalent to redcify (a_prim, a, n).  It runs faster
       on most processors, since it avoids udiv_qrnnd.  If we go down the
       udiv_qrnnd_preinv path, this code should be replaced.  */
    {
      uint64_t s1, s0;
      umul_ppmm (s1, s0, one, a);
      if (s1 == 0) [[likely]] {
        a_prim = s0 % n;
      } else {
        [[maybe_unused]] uint64_t dummy;
        udiv_qrnnd (dummy, a_prim, s1, s0, n);
      }
    }

    if (!millerrabin (n, ni, a_prim, q, k, one))
      return false;
  }

  // We exhausted the ptab table. Is this actually an error? Perhaps
  // the gnu code knows that the table is enough for any 64-bit int?

  CHECK(false) << "Lucas prime test failure.  This should not happen";
  return false;
}

static void
factor_using_pollard_rho (uint64_t n, unsigned long int a,
                          std::vector<std::pair<uint64_t, int>> *factors) {
  // printf("pr(%llu, %lu)\n", n, a);
  uint64_t x, z, y, P, t, ni, g;

  unsigned long int k = 1;
  unsigned long int l = 1;

  redcify (P, 1, n);
  // i.e., redcify(2)
  addmod (x, P, P, n);
  y = z = x;

  while (n != 1) {
    assert (a < n);

    // Mysterious "fixme" comment in factor.c:
    // "when could we use old 'ni' value?"
    binv (ni, n);

    for (;;) {
      do {
        x = mulredc (x, x, n, ni);
        addmod (x, x, a, n);

        submod (t, z, x, n);
        P = mulredc (P, t, n, ni);

        if (k % 32 == 1) {
          if (gcd_odd (P, n) != 1)
            goto factor_found;
          y = x;
        }
      }
      while (--k != 0);

      z = x;
      k = l;
      l = 2 * l;
      for (unsigned long int i = 0; i < k; i++) {
        x = mulredc (x, x, n, ni);
        addmod (x, x, a, n);
      }
      y = x;
    }

  factor_found:
    do {
      y = mulredc (y, y, n, ni);
      addmod (y, y, a, n);

      submod (t, z, y, n);
      g = gcd_odd (t, n);
    } while (g == 1);

    if (n == g) {
      /* Found n itself as factor.  Restart with different params.  */
      factor_using_pollard_rho (n, a + 1, factors);
      return;
    }

    n = n / g;

    if (!prime_p (g))
      factor_using_pollard_rho (g, a + 1, factors);
    else
      PushFactor(factors, g);

    if (prime_p (n)) {
      PushFactor(factors, n);
      break;
    }

    x = x % n;
    z = z % n;
    y = y % n;
  }
}

std::vector<std::pair<uint64_t, int>>
Factorize::PrimeFactorization(uint64_t x) {
  return InternalFactorize(x, true);
}

std::vector<std::pair<uint64_t, int>>
Factorize::ReferencePrimeFactorization(uint64_t x) {
  return InternalFactorize(x, false);
}
