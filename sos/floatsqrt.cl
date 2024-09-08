
typedef uchar uint8_t;
typedef uchar3 uint8_t3;
typedef int int32_t;
typedef uint uint32_t;
typedef uint3 uint32_t3;
typedef ulong uint64_t;
typedef long int64_t;
typedef atomic_uint atomic_uint32_t;

#define STATIC_ASSERT(cond) char unused_[0 - !(cond)];

// Defined by wrapper.
// #define MAX_OUTPUT_SIZE (63 * 3)

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
  uint32_t r = (uint32_t)sqrt((float)n);
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

__kernel void CheckAll(__global atomic_uint32_t *restrict out_size,
                       __global int64_t *restrict out) {

  const uint32_t n = get_global_id(0);

  uint32_t a32 = Sqrt32Error(n);
  uint32_t a64 = Sqrt64Error(n);

  if (a32 != a64) {
    const uint32_t out_idx = atomic_add(out_size, 1);
    if (out_idx > MAX_OUTPUT_SIZE) {
      atomic_add(out_size, -1);
      return;
    }
    out[out_idx * 3 + 0] = n;
    out[out_idx * 3 + 1] = a32;
    out[out_idx * 3 + 2] = a64;
  }
}
