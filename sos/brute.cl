
typedef uchar uint8_t;
typedef uchar3 uint8_t3;
typedef int int32_t;
typedef uint uint32_t;
typedef uint3 uint32_t3;
typedef ulong uint64_t;
typedef long int64_t;
typedef atomic_uint atomic_uint32_t;

inline static uint32_t Sqrt32(uint32_t n) {
  uint32_t r = (uint32_t)sqrt((float)n);
  return r - (r * r >= n + 1);
}

// This produces the same integer error as the 64-bit
// version up to 0xFFFF0000. Beyond that we get errors,
// but we don't expect any of the numbers to get that
// high here. See floatsqrt.* for an exhaustive check.
inline uint32_t Sqrt32Error(uint32_t aa) {
  uint32_t a1 = Sqrt32(aa);
  // uint32_t a2 = a1 + 1;
  uint32_t aa1 = a1 * a1;
  uint32_t aa2 = aa1 + (a1 << 1) + 1;

  return min(abs_diff(aa, aa1),
             abs_diff(aa, aa2));
}

/*

We want to try every (x, y) with x < y
and y >= 0 and x >= -base/2.

      -- x ->
     .. -3 -2 -1  0  1  2  3  4
 | 0     a  b  c  .  .  .  .  .
 | 1     d  e  f  g  .  .  .  .
 y 2     h  i  j  k  l  .  .  .
 | 3     m  n  o  p  q  r  .  .
 v 4     s  t  u  v  w  x  y  .

PERF: On the upper triangle we exit early, since
we do not need x >= y. It would be nice to
pack this into a 2D rectangle, although it is
tricky because (among other things) we run negative x.

*/

// Unrolled.
#define ONE_CELL(q) do {                        \
    int err = Sqrt32Error(q);                   \
    if (err != 0) {                             \
      not_square++;                             \
      total_err += err;                         \
    }                                           \
  } while (0)

__kernel void BruteXY(// aka "base"
                      const uint32_t n,
                      volatile __global atomic_uint32_t *restrict out_size,
                      __global int64_t *restrict out) {

  const uint32_t xoff = get_global_id(0);
  const int32_t x = (int32_t)xoff - ((int32_t)n / 2);
  const uint32_t y = get_global_id(1);

  if (x >= y) return;

  // Note we use unsigned values here, since all legal squares
  // will have nonzero entries. Since x is at least -n/2, none
  // of these should actually be negative. But if they are, the
  // worst that would happen is that we flag a nonsense square
  // and then reject it in the C++ code.
  //
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

  // As inputs become very large the error could overflow in
  // principle, but the worst thing that happens here is that we
  // erroneously flag the square as interesting. So we stick with 32
  // bits.

  // Note: I tried vectorizing this but it didn't do any better
  // than the compiler and it was quite fiddly.
  uint32_t total_err = 0;
  uint8_t not_square = 0;
  ONE_CELL(a);
  ONE_CELL(b);
  ONE_CELL(c);
  ONE_CELL(d);
  ONE_CELL(e);
  ONE_CELL(f);
  ONE_CELL(g);
  ONE_CELL(h);
  ONE_CELL(i);


  if (total_err < 0) {
    printf("OOPS! %d %d %d\n%d %d %d\n%d %d %d = %d\n",
           a, b, c, d, e, f, g, h, i, total_err);
    return;
  }

  if (total_err < INTERESTING_THRESHOLD[not_square]) {
    // Don't bother with degenerate squares. These get filtered
    // out on the C++ side, but they often also appear all at
    // once in a kernel execution, exhausting the output buffer.
    if (x == y || x == 0 || y == 0 || -x == y ||
        x == 2 * y || y == 2 * x) {
      return;
    }

    const uint32_t out_idx = atomic_add(out_size, 1);
    if (out_idx >= MAX_INTERESTING) {
      // We don't actually write anything, so don't count it.
      atomic_add(out_size, -1);
      return;
    }

    /*
    printf("@%d/%d (%d, %d,%d):\n%d %d %d\n%d %d %d\n%d %d %d = %d\n",
           out_idx, MAX_INTERESTING, n, x, y,
           a, b, c, d, e, f, g, h, i, total_err);
    */

    out[out_idx * 2] = x;
    out[out_idx * 2 + 1] = y;
  }
}
