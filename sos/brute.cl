
typedef uchar uint8_t;
typedef uint uint32_t;
typedef ulong uint64_t;
typedef long int64_t;
typedef atomic_uint atomic_uint32_t;

#define STATIC_ASSERT(cond) char unused_[0 - !(cond)];


// PERF! 32 bit versions.
inline static uint64_t Sqrt64(uint64_t n) {
  uint64_t r = (uint64_t)sqrt((double)n);
  return r - (r * r >= n + 1);
}

inline uint64_t Sqrt64Error(uint64_t aa) {
  uint64_t a1 = Sqrt64(aa);
  uint64_t a2 = a1 + 1ULL;
  uint64_t aa1 = a1 * a1;
  uint64_t aa2 = a2 * a2;
  return std::min(std::max(aa, aa1) - std::min(aa, aa1),
                  std::max(aa, aa2) - std::min(aa, aa2));
}

// Threshold to flag as interesting if total error is less
// than this amount.
static uint32_t INTERESTING_THRESHOLD[10] = {
  999999999,      // 0
  999999999,      // 1
  1000,           // 2
  100,            // 3
  20,             // 4
  5,              // 5
  6,              // 6
  9,              // 7
  16,             // 8
  35,             // 9
};

/*

We want to try every (x, y) with x < y.

      -- x ->
     0 1 2 3 4
 | 0 . . . . .
 | 1 a . . . .
 y 2 b c . . .
 | 3 d e f . .
 v 4 g h i j .

TODO: We can reassemble this into a dense 2D rectangle, which
is probably faster than exiting early.

*/
// Writes bitmask to out.
__kernel void BruteXY(// aka "base"
                      const uint32_t n,
                      __global atomic_uint32_t *restrict out_size,
                      __global uint32_t *restrict out) {

  const uint32_t x = get_global_id(0);
  const uint32_t y = get_global_id(1);

  if (x >= y) return;

  // PERF: Probably the compiler can do this, but there are
  // many common subexpressions.
  const uint32_t a = n + 2 * x + y;
  const uint32_t b = n;
  const uint32_t c = n + x + 2 * y;
  const uint32_t d = n + 2 * y;
  const uint32_t e = n + x + y;
  const uint32_t f = n + 2 * x;
  const uint32_t g = n + x;
  const uint32_t h = n + 2 * x + 2 * y;
  const uint32_t i = n + y;

  uint8_t not_square = 0;
  // As inputs become very large this could overflow in
  // principle, but the worst thing that happens here is
  // that we erroneously flag the square as interesting.
  // So we stick with 32 bits.
  uint32_t total_err = 0;

  // Unrolled.
#define ONE_CELL(q) do {                        \
    int err_ ## q = SqrtError(q);               \
    if (err != 0) {                             \
      not_square++;                             \
      total_err += err;                         \
    }                                           \
  } while (0)

  ONE_CELL(a);
  ONE_CELL(b);
  ONE_CELL(c);
  ONE_CELL(d);
  ONE_CELL(e);
  ONE_CELL(f);
  ONE_CELL(g);
  ONE_CELL(h);
  ONE_CELL(i);

  if (total_err < INTERESTING_THRESHOLD[not_square]) {
    const uint32_t out_idx = atomic_add(out_size, 2);
    out[out_idx] = x;
    out[out_idx + 1] = y;
  }
}
