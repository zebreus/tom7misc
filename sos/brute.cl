
typedef uchar uint8_t;
typedef uchar3 uint8_t3;
typedef uint uint32_t;
typedef uint3 uint32_t3;
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
  uint64_t a2 = a1 + 1;
  uint64_t aa1 = a1 * a1;
  // Compute a2 * a2 = (a1 + 1) * (a1 + 1) =
  // a1^2 + 2a1 + 1
  uint64_t aa2 = aa1 + (a1 << 1) + 1;
  return min(max(aa, aa1) - min(aa, aa1),
             max(aa, aa2) - min(aa, aa2));
}

// As above, but 32 bit.
// I think float sqrt may not be enough precision?
// We could do an exhaustive check.
inline static uint32_t Sqrt32(uint32_t n) {
  uint32_t r = (uint32_t)sqrt((double)n);
  return r - (r * r >= n + 1);
}

inline uint32_t Sqrt32Error(uint32_t aa) {
  uint32_t a1 = Sqrt32(aa);
  uint32_t a2 = a1 + 1;
  uint32_t aa1 = a1 * a1;
  uint32_t aa2 = aa1 + (a1 << 1) + 1;
  return min(max(aa, aa1) - min(aa, aa1),
             max(aa, aa2) - min(aa, aa2));
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
  14,             // 8
  15,             // 9
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
is probably faster than exiting early. It's slightly extra
tricky because we run this for y in [y_start, y_end).

*/

  // Unrolled.
#define ONE_CELL(q) do {                        \
    int err = Sqrt64Error(q);                   \
    if (err != 0) {                             \
      not_square++;                             \
      total_err += err;                         \
    }                                           \
  } while (0)

#define VECTORIZE 1

#define PACK3(q, a, b, c) \
  uint32_t3 q ## i = (uint3)(a, b, c); \
  double3 q = convert_double3(q ## i);

#define SQRT3(q) \
  uint32_t3 q ## aroot = convert_uint3(sqrt(q)); \
  uint32_t3 q ## sq = q ## aroot * q ## aroot; \
  uint32_t3 q ## root = q ## aroot - (convert_uint3(q ## sq >= q ## i) & 1);

#define ERR3(q) \
  uint32_t3 q ## a2 = q ## root + (uint3)(1, 1, 1); \
  uint32_t3 q ## aa1 = q ## root * q ## root; \
  uint32_t3 q ## aa2 = q ## aa1 + (q ## root << 1) + 1; \
  uint32_t3 q ## err = min(max(q ## i, q ## aa1) - \
                           min(q ## i, q ## aa1),  \
                           max(q ## i, q ## aa2) - \
                           min(q ## i, q ## aa2)); \
  uint8_t3 q ## notsq = (q ## err > 0) & 1;   \

#define SUM_ERR(t,  q, r, s)                          \
  uint32_t3 t = q ## err + r ## err + s ## err;

#define SUM_NOTSQ(u,  q, r, s) \
  uint8_t3 u = q ## notsq + r ## notsq + s ## notsq;

// Note that vector comparisons < > return -1, not 1, when true.

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

#if VECTORIZE
  PACK3(q,   a, b, c);
  PACK3(r,   d, e, f);
  PACK3(s,   g, h, i);
  SQRT3(q);
  SQRT3(r);
  SQRT3(s);
  ERR3(q);
  ERR3(r);
  ERR3(s);
  // Sum columns.
  SUM_ERR(t,    q, r, s);
  uint32_t total_err = t.x + t.y + t.z;
  SUM_NOTSQ(u,  q, r, s);
  uint8_t not_square = u.x + u.y + u.z;
#else

  // PERF: might be possible to vectorize some of this
  // (especially the square root) if the compiler doesn't.
  ONE_CELL(a);
  ONE_CELL(b);
  ONE_CELL(c);
  ONE_CELL(d);
  ONE_CELL(e);
  ONE_CELL(f);
  ONE_CELL(g);
  ONE_CELL(h);
  ONE_CELL(i);
#endif

  if (total_err < INTERESTING_THRESHOLD[not_square]) {
    const uint32_t out_idx = atomic_add(out_size, 2);
    out[out_idx] = x;
    out[out_idx + 1] = y;
  }
}
