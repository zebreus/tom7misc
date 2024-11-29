
typedef uchar uint8_t;
typedef uchar3 uint8_t3;
typedef int int32_t;
typedef uint uint32_t;
typedef uint3 uint32_t3;
typedef ulong uint64_t;
typedef long int64_t;
typedef atomic_uint atomic_uint32_t;

inline static uint64_t Sqrt64(uint64_t n) {
  uint64_t r = (uint64_t)sqrt((double)n);
  return r - (r * r >= n + 1);
}

inline uint64_t Sqrt64Error(uint64_t aa) {
  uint64_t a1 = Sqrt64(aa);
  uint64_t aa1 = a1 * a1;
  uint64_t aa2 = aa1 + (a1 << 1) + 1;

  return min(abs_diff(aa, aa1),
             abs_diff(aa, aa2));
}

// Unrolled.
#define ONE_CELL(q) do {                        \
    int err = Sqrt64Error(q);                   \
    if (err != 0) {                             \
      not_square++;                             \
      total_err += err;                         \
    }                                           \
  } while (0)

__kernel void BruteX(const int64_t n, const int64_t y,
                     volatile __global atomic_uint32_t *restrict out_size,
                     __global int64_t *restrict out) {

  const int64_t x = get_global_id(0);

  // PERF: By construction we should have b and f being squares
  // (zero error), so we can skip computing anything about them here.
  const uint64_t a = n + 2 * x + y;
  // const uint64_t b = n;
  const uint64_t c = n + x + 2 * y;
  // const uint64_t d = n + 2 * y;
  const uint64_t e = n + x + y;
  const uint64_t f = n + 2 * x;
  const uint64_t g = n + x;
  const uint64_t h = n + 2 * x + 2 * y;
  const uint64_t i = n + y;

  int64_t total_err = 0;
  uint8_t not_square = 0;
  ONE_CELL(a);
  // ONE_CELL(b);
  ONE_CELL(c);
  // ONE_CELL(d);
  ONE_CELL(e);
  ONE_CELL(f);
  ONE_CELL(g);
  ONE_CELL(h);
  ONE_CELL(i);

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
    printf("[n=%lld,y=%lld] pushed %lld at index %d\n",
           n, y,
           x, out_idx);
    */
    out[out_idx] = x;
  }
}
